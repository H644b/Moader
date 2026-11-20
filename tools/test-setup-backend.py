#!/usr/bin/env python3
"""Isolated setup and command-wrapper checks; no network, console or real compiler."""
import hashlib, importlib.util, io, json, os, pathlib, stat, subprocess, sys, tarfile, tempfile, unittest, zipfile
from unittest.mock import patch
sys.dont_write_bytecode = True


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, pathlib.Path(__file__).with_name(file))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module); return module


backend = load('setup_backend', 'setup-backend.py')
wrapper = load('install_from_rpc', 'install-from-rpc.py')
devkit = load('setup_devkit', 'setup-devkit.py')


def zip_bytes(files):
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return result.getvalue()


class DevkitHostTests(unittest.TestCase):
    def test_native_pair_selection_keeps_compiler_and_libraries(self):
        names = devkit.package_names(True)
        self.assertEqual(set(devkit.package_names()), set(devkit.NAMES))
        self.assertEqual(set(devkit.NAMES) - set(names), {'switch-tools', 'general-tools'})
        self.assertIn('devkita64-gcc', names)
        self.assertIn('switch-zlib', names)

    def test_toolchain_vars_403_uses_only_exact_digest_mirror(self):
        payload = b'pinned helper fixture'
        expected = hashlib.sha256(payload).hexdigest()
        calls = []
        def open_fixture(request, timeout):
            self.assertEqual(timeout, 90)
            calls.append(request.full_url)
            if len(calls) == 1:
                raise devkit.urllib.error.HTTPError(request.full_url, 403, 'blocked', None, io.BytesIO())
            return io.BytesIO(payload)
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / 'helper.pkg.tar.zst'
            with patch.object(devkit, 'TOOLCHAIN_VARS_SHA256', expected), \
                 patch.object(devkit.urllib.request, 'urlopen', side_effect=open_fixture):
                self.assertEqual(devkit.verified_download(target, devkit.TOOLCHAIN_VARS_OFFICIAL,
                                                          expected, 'test-agent'), target)
            self.assertEqual(target.read_bytes(), payload)
        self.assertEqual(calls, [devkit.TOOLCHAIN_VARS_OFFICIAL, devkit.TOOLCHAIN_VARS_MIRROR])

    def test_toolchain_vars_mirror_keeps_checksum_gate(self):
        calls = []
        def open_fixture(request, timeout):
            calls.append(request.full_url)
            if len(calls) == 1:
                raise devkit.urllib.error.HTTPError(request.full_url, 403, 'blocked', None, io.BytesIO())
            return io.BytesIO(b'changed mirror bytes')
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / 'helper.pkg.tar.zst'
            with patch.object(devkit.urllib.request, 'urlopen', side_effect=open_fixture):
                with self.assertRaisesRegex(ValueError, 'checksum differs'):
                    devkit.verified_download(target, devkit.TOOLCHAIN_VARS_OFFICIAL,
                                            devkit.TOOLCHAIN_VARS_SHA256, 'test-agent')
            self.assertFalse(target.exists())
        self.assertEqual(calls, [devkit.TOOLCHAIN_VARS_OFFICIAL, devkit.TOOLCHAIN_VARS_MIRROR])

    def test_package_extracts_sdk_subtree_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            archive = root / 'sdk.tar'
            with tarfile.open(archive, 'w') as out:
                folder = tarfile.TarInfo('opt/devkitpro/'); folder.type = tarfile.DIRTYPE
                out.addfile(folder)
                member = tarfile.TarInfo('opt/devkitpro/devkitA64/bin/compiler'); member.size = 8
                out.addfile(member, io.BytesIO(b'compiler'))
                metadata = tarfile.TarInfo('.PKGINFO'); metadata.size = 4
                out.addfile(metadata, io.BytesIO(b'meta'))
            sdk = root / 'sdk'; sdk.mkdir()
            devkit.extract_package(archive, sdk)
            self.assertEqual((sdk / 'devkitA64/bin/compiler').read_bytes(), b'compiler')
            self.assertFalse((sdk / '.PKGINFO').exists())

    def test_linux_package_selection_preserves_shared_and_mac_pins(self):
        with patch.object(devkit.platform, 'system', return_value='Linux'), patch.object(devkit.platform, 'machine', return_value='x86_64'):
            self.assertEqual(devkit.host()[0], 'linux-x86_64')
        linux = devkit.register('linux-x86_64')
        mac = devkit.register('macos-arm64')
        for name in ('devkita64-gcc', 'devkita64-binutils', 'switch-tools', 'general-tools'):
            self.assertIn('/linux/x86_64/', linux[name]['url'])
            self.assertNotEqual(linux[name]['SHA256SUM'][0], mac[name]['SHA256SUM'][0])
        for name in ('devkitA64', 'devkita64-newlib', 'devkita64-rules', 'dkp-toolchain-vars', 'switch-zlib'):
            self.assertEqual(linux[name], mac[name])

    def test_unsupported_host_and_changed_cached_package_stop(self):
        with patch.object(devkit.platform, 'system', return_value='Linux'), patch.object(devkit.platform, 'machine', return_value='aarch64'), self.assertRaisesRegex(ValueError, 'Linux x86_64'):
            devkit.host()
        with tempfile.TemporaryDirectory() as directory:
            file = pathlib.Path(directory) / 'package.pkg.tar.zst'
            file.write_bytes(b'changed')
            with patch.object(devkit.urllib.request, 'urlopen', side_effect=AssertionError('Cached file must not download')), self.assertRaisesRegex(ValueError, 'checksum differs'):
                devkit.verified_download(file, 'https://example.invalid/package', hashlib.sha256(b'expected').hexdigest(), 'test')


class SeedsowLauncherTests(unittest.TestCase):
    def test_wsl_windows_node_is_rejected_before_gui_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            node = root / 'node.exe'
            node.write_text('#!/bin/sh\nif [ "$1" = "--version" ]; then echo v24.0.0; elif [ "$1" = "-p" ]; then echo win32; else exit 66; fi\n')
            node.chmod(0o755)
            gui = root / 'gui'; gui.mkdir()
            result = subprocess.run(['bash', str(pathlib.Path(__file__).with_name('start-seedsow-gui.sh')), str(gui.resolve())],
                                    env={**os.environ, 'MOADER_NODE24': str(node)}, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn('use Linux node rather than node.exe', result.stderr)


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.base = pathlib.Path(self.temp.name); self.root = self.base / 'kit'; self.root.mkdir()
        self.network = patch.object(backend.urllib.request, 'urlopen', side_effect=AssertionError('No network in tests')).start(); self.addCleanup(patch.stopall)
        self.hen = b'NSO0-tested-hen'
        self.engine = {'host/index.cjs': b'module.exports={};', 'switch2/ro/payload/main': b'payload'}
        self.official = {name: ('official-' + name).encode() for name in ('ro/main', 'hen/main', 'game/main', 'bootstrap.bin', 'hbmenu.nro', 'programs/ounceshell.nro', 'licenses/COPYING')}
        self.kaihen = self.base / 'kaihen.zip'; self.kaihen.write_bytes(zip_bytes(self.official))
        self.engine_zip = self.base / 'engine.zip'
        files = {'ns2jb-' + backend.NS2JB_REV + '/engine/' + name: data for name, data in self.engine.items()}
        files.update({'ns2jb-' + backend.NS2JB_REV + '/runs/private': b'never copy', 'ns2jb-' + backend.NS2JB_REV + '/.git/config': b'never copy'})
        self.engine_zip.write_bytes(zip_bytes(files))
        self.programs = self.base / 'app.zip'; self.programs.write_bytes(zip_bytes({'switch/Moader.nro': b'\0' * 16 + b'NRO0' + b'app', 'kaihen/plugins/moader.nso': b'NSO0service'}))
        self.hen_file = self.base / 'hen'; self.hen_file.write_bytes(self.hen)
        patch.object(backend, 'HEN_SHA', backend.digest(self.hen)).start()
        patch.object(backend, 'ENGINE_TREE_SHA', backend.tree_sha(self.engine)).start()
        patch.object(backend, 'KAIHEN_SHA', backend.digest(self.kaihen.read_bytes())).start()
        self.put('software-builds/kaihen-mo.json', json.dumps({'hen_sha256': backend.HEN_SHA}).encode())
        self.put('software-builds/kaihen-mo.patch', b'patch')
        self.node = patch.object(backend, 'node_check', return_value='v24.14.0').start()

    def put(self, name, data):
        path = self.root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data); return path

    def args(self, *extra):
        return backend.parser().parse_args(['--root', str(self.root), '--offline', '--kaihen-archive', str(self.kaihen), '--engine-archive', str(self.engine_zip), '--programs-archive', str(self.programs), '--programs-sha256', backend.digest(self.programs.read_bytes()), '--hen-binary', str(self.hen_file), *extra])

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns) for p in self.root.rglob('*') if p.is_file()}

    def test_offline_provision_preserves_private_state_and_is_idempotent(self):
        self.put('state/private-session', b'keep'); self.put('build/install/prepared-old/manifest.json', b'keep')
        result = backend.setup(self.args()); self.assertTrue(result['readyForInstallerCheck']); self.assertFalse(result['consoleConnected'])
        self.assertEqual((self.root / 'vendor/kaihen-mo/hen/main').read_bytes(), self.hen)
        self.assertEqual(backend.tree_files(self.root / 'research/sources/ns2jb/engine'), self.engine)
        self.assertEqual(backend.tree_files(self.root / 'vendor/kaihen-mo/programs'),
                         {'ounceshell.nro': self.official['programs/ounceshell.nro']})
        self.assertEqual(result['bundledPrograms'], ['ounceshell.nro'])
        self.assertFalse((self.root / 'vendor/kaihen-mo/licenses').exists())
        self.assertFalse((self.root / 'research/sources/ns2jb/runs').exists())
        before = self.snapshot(); self.assertEqual(backend.setup(self.args())['newFiles'], 0); self.assertEqual(self.snapshot(), before)

    def test_schema3_provisions_exact_ro_hen_bootstrap_set(self):
        self.put('runtime/kaihen/ro/source/main.cpp', b'qualified RO source')
        ro = b'NSO0-qualified-ro'; bootstrap = b'qualified-bootstrap'
        ro_file = self.base / 'ro'; ro_file.write_bytes(ro)
        bootstrap_file = self.base / 'bootstrap'; bootstrap_file.write_bytes(bootstrap)
        components = {name: {'bytes': len(data), 'sha256': backend.digest(data)} for name, data in
                      {'ro/main': ro, 'hen/main': self.hen, 'bootstrap.bin': bootstrap}.items()}
        descriptor = {'schema': 3, 'base_commit': '3695f364f9270686aa72cc39627bb1281752b2d3',
                      'release_base': '20261117.1', 'patch': 'software-builds/kaihen-mo.patch',
                      'protocol_version': 8, 'native_build_passed': True,
                      'console_retest_passed': False,
                      'source_tree_algorithm': backend.SOURCE_TREE_ALGORITHM,
                      'source_tree_sha256': backend.kaihen_source_sha(self.root),
                      'patch_sha256': backend.digest(b'patch'),
                      'hen_sha256': components['hen/main']['sha256'], 'components': components}
        self.put('runtime/kaihen/libkaihen/include/kaihen/protocol.h', b'#define HEN_PROTOCOL_VERSION 8\n')
        descriptor['source_tree_sha256'] = backend.kaihen_source_sha(self.root)
        self.put('software-builds/kaihen-mo.json', json.dumps(descriptor).encode())
        args = self.args('--ro-binary', str(ro_file), '--bootstrap-binary', str(bootstrap_file))
        result = backend.setup(args)
        self.assertEqual(result['kaihenProofSchema'], 3)
        self.assertEqual(result['roSha256'], backend.digest(ro))
        self.assertEqual((self.root / 'vendor/kaihen-mo/ro/main').read_bytes(), ro)
        descriptor['components'].pop('ro/main')
        self.put('software-builds/kaihen-mo.json', json.dumps(descriptor).encode())
        with self.assertRaisesRegex(ValueError, 'RO/HEN/bootstrap'):
            backend.proof(self.root)

    def test_minimal_kit_downloads_only_two_pinned_inputs_and_keeps_hen(self):
        self.put('vendor/kaihen-mo/hen/main', self.hen)
        for name, data in backend.programs(backend.zip_files(self.programs)).items():
            self.put(name, data)
        tested = self.snapshot(); calls = []
        def fetch(url, target, expected=None, offline=False):
            calls.append((url, expected)); self.assertFalse(offline)
            data = self.kaihen.read_bytes() if url == backend.KAIHEN_URL else self.engine_zip.read_bytes()
            target.write_bytes(data); return target
        args = backend.parser().parse_args(['--root', str(self.root)])
        with patch.object(backend, 'download', side_effect=fetch), patch.object(backend, 'build_inputs', side_effect=AssertionError()):
            result = backend.setup(args)
        self.assertEqual(calls, [(backend.KAIHEN_URL, backend.KAIHEN_SHA), (backend.ENGINE_URL, None)])
        self.assertEqual(result['bundledPrograms'], ['ounceshell.nro'])
        self.assertTrue(all(self.snapshot()[name] == value for name, value in tested.items()))
        self.assertEqual(backend.tree_files(self.root / 'vendor/kaihen-mo/programs'),
                         {'ounceshell.nro': self.official['programs/ounceshell.nro']})

    def test_optional_existing_programs_preserved_and_reported(self):
        backend.setup(self.args()); self.put('vendor/kaihen-mo/programs/ounceshell.nro', self.official['programs/ounceshell.nro'])
        self.assertEqual(backend.check_root(self.root)['bundledPrograms'], ['ounceshell.nro'])
        self.put('vendor/kaihen-mo/programs/moader-diagnostics.nro', b'\0' * 16 + b'NRO0diagnostics')
        self.assertEqual(backend.check_root(self.root)['bundledPrograms'], ['moader-diagnostics.nro', 'ounceshell.nro'])
        self.put('vendor/kaihen-mo/programs/empty.nro', b'')
        with self.assertRaisesRegex(ValueError, 'size bounds'):
            backend.check_root(self.root)

    def test_civ7_normal_setup_keeps_legacy_source_and_tested_native_inputs(self):
        backend.setup(self.args())
        before = self.snapshot()
        selected = {'engine/shared/rpc.cjs': b'authenticated transport',
                    'targets/civ7/target.json': b'{"name":"civ7"}',
                    'bridge/civ_bridge.js': b'bridge'}
        archive = self.base / 'civ7.zip'; archive.write_bytes(zip_bytes(selected))
        with patch.object(backend, 'CIV7_TREE_SHA', backend.tree_sha(selected)):
            args = self.args('--host-profile', 'civ7-s8n', '--engine-archive', str(archive))
            result = backend.setup(args)
            self.assertEqual(result['hostProfile'], 'civ7-s8n')
            self.assertTrue(result['readyForInstallerCheck']); self.assertFalse(result['consoleConnected'])
            self.assertTrue(all(self.snapshot()[name] == value for name, value in before.items()))
            self.assertEqual(backend.tree_files(self.root / 'research/sources/civ7-fs-kaihen'), selected)
            snapshot = self.snapshot()
            with patch.object(backend, 'download', side_effect=AssertionError()):
                self.assertEqual(backend.setup(args)['newFiles'], 0)
            with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()):
                backend.setup(self.args('--host-profile', 'civ7-s8n', '--check'))
            self.assertEqual(self.snapshot(), snapshot)
            self.put('research/sources/civ7-fs-kaihen/targets/civ7/target.json', b'wrong')
            with self.assertRaisesRegex(ValueError, 'pinned source tree'):
                backend.setup(self.args('--host-profile', 'civ7-s8n', '--check'))
            self.assertEqual(backend.check_root(self.root)['hostProfile'], 'smilebasic-kurumuz')

    def test_check_is_readonly_and_never_downloads_or_builds(self):
        backend.setup(self.args()); before = self.snapshot()
        with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'build_inputs', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()):
            backend.setup(self.args('--check'))
        self.assertEqual(self.snapshot(), before)

    def test_seedsow_setup_and_check_preserve_existing_native_and_other_profile_inputs(self):
        backend.setup(self.args()); before = self.snapshot()
        descriptor = json.loads((backend.ROOT / 'config/seedsow-provider.json').read_text())
        selected = {descriptor['engineRelativePath'] + '/shared/sdk-layout.json': b'{"SDK":1}',
                    'tools/seedsow-bridge.cjs': b'never import', 'console/n2h_rpc.lua': b'never execute', 'env.txt': b'fixture'}
        descriptor['files'] = {name: backend.digest(data) for name, data in selected.items()}
        descriptor['sourceTreeSha256'] = backend.tree_sha(selected)
        metadata = self.base / 'config/seedsow-provider.json'; metadata.parent.mkdir(); metadata.write_text(json.dumps(descriptor))
        archive = self.base / 'seedsow.zip'
        archive.write_bytes(zip_bytes({'seedsow-fs-kaihen-' + backend.SEEDSOW_REV + '/' + name: data for name, data in selected.items()}))
        with patch.object(backend, 'ROOT', self.base), patch.object(backend, 'SEEDSOW_TREE_SHA', descriptor['sourceTreeSha256']):
            args = self.args('--host-profile', 'seedsow-malev0', '--engine-archive', str(archive))
            with patch.object(backend.subprocess, 'run', side_effect=AssertionError()), patch.object(backend, 'download', side_effect=AssertionError()):
                result = backend.setup(args)
            self.assertEqual(result['hostProfile'], 'seedsow-malev0'); self.assertFalse(result['consoleConnected'])
            self.assertTrue(result['readyForInstallerCheck'])
            self.assertTrue(all(self.snapshot()[name] == value for name, value in before.items()))
            self.assertEqual(backend.tree_files(self.root / descriptor['sourceDirectory']), selected)
            saved = self.snapshot()
            with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()):
                backend.setup(self.args('--host-profile', 'seedsow-malev0', '--check'))
            self.assertEqual(self.snapshot(), saved)
            self.put(descriptor['sourceDirectory'] + '/console/n2h_rpc.lua', b'changed')
            with self.assertRaisesRegex(ValueError, 'pinned source tree'):
                backend.setup(self.args('--host-profile', 'seedsow-malev0', '--check'))
            self.assertEqual(backend.check_root(self.root)['hostProfile'], 'smilebasic-kurumuz')

    def test_imports_do_not_execute_or_create_outputs(self):
        with patch.object(subprocess, 'run', side_effect=AssertionError()), patch.object(pathlib.Path, 'mkdir', side_effect=AssertionError()), patch.object(pathlib.Path, 'write_bytes', side_effect=AssertionError()):
            load('backend_import_only', 'setup-backend.py')
            load('wrapper_import_only', 'install-from-rpc.py')

    def test_missing_inputs_check_stops_without_writes(self):
        before = self.snapshot()
        with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaises(ValueError):
            backend.setup(self.args('--check'))
        self.assertEqual(self.snapshot(), before)

    def test_tampered_vendor_stops_without_writes(self):
        backend.setup(self.args()); self.put('vendor/kaihen-mo/ro/main', b'changed'); before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            backend.setup(self.args())
        self.assertEqual(self.snapshot(), before)

    def test_existing_source_tree_is_never_replaced(self):
        self.put('research/sources/ns2jb/engine/local.cjs', b'changed'); before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Existing engine differs'):
            backend.setup(self.args())
        self.assertEqual(self.snapshot(), before)

    def test_wrong_hen_stops_before_publish(self):
        self.hen_file.write_bytes(b'wrong'); before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'tested bytes'):
            backend.setup(self.args())
        self.assertEqual(self.snapshot(), before)

    def test_wrong_official_archive_checksum_stops(self):
        self.kaihen.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Archive checksum'):
            backend.setup(self.args())

    def test_program_framing_rejected_before_publish(self):
        self.programs.write_bytes(zip_bytes({'switch/Moader.nro': b'not NRO', 'kaihen/plugins/moader.nso': b'NSO0'})); before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Invalid Moader NRO'):
            backend.setup(self.args())
        self.assertEqual(self.snapshot(), before)

    def test_proof_cannot_select_arbitrary_hen(self):
        self.put('software-builds/kaihen-mo.json', b'{"hen_sha256":"wrong"}')
        with self.assertRaisesRegex(ValueError, 'proof/hash'):
            backend.setup(self.args())

    def test_publish_preflights_all_existing_files(self):
        self.put('vendor/tested', b'original')
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            backend.publish_new(self.root, {'new': b'new', 'vendor/tested': b'changed'})
        self.assertFalse((self.root / 'new').exists())

    def test_symlink_destination_rejected(self):
        (self.root / 'vendor').symlink_to(self.base, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            backend.publish_new(self.root, {'vendor/file': b'data'})
        self.assertFalse((self.base / 'file').exists())

    def test_unsafe_zip_paths_links_and_duplicates(self):
        for name in ('../escape', '/absolute', 'a/../escape', 'a\\escape'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                backend.zip_files(io.BytesIO(zip_bytes({name: b'data'})))
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as archive:
            row = zipfile.ZipInfo('link'); row.external_attr = (stat.S_IFLNK | 0o777) << 16; archive.writestr(row, 'target')
        with self.assertRaisesRegex(ValueError, 'links/special'):
            backend.zip_files(io.BytesIO(data.getvalue()))
        data = zip_bytes({'a/b': b'one', 'a//b': b'two'})
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            backend.zip_files(io.BytesIO(data))

    def test_zip_size_bound(self):
        with patch.object(backend, 'MAX_FILE', 3), self.assertRaisesRegex(ValueError, 'bound'):
            backend.zip_files(io.BytesIO(zip_bytes({'large': b'1234'})))

    def test_source_tar_selection_and_link_refusal(self):
        target = self.base / 'engine.tar.gz'
        def make_tar(link=False):
            with tarfile.open(target, 'w:gz') as archive:
                for name, data in self.engine.items():
                    row = tarfile.TarInfo('ns2jb-' + backend.NS2JB_REV + '/engine/' + name); row.size = len(data); archive.addfile(row, io.BytesIO(data))
                if link:
                    row = tarfile.TarInfo('link'); row.type = tarfile.SYMTYPE; row.linkname = '/tmp'; archive.addfile(row)
        make_tar(); self.assertEqual(backend.engine_files(target), self.engine)
        make_tar(True)
        with self.assertRaisesRegex(ValueError, 'links/special'):
            backend.engine_files(target)

    def test_engine_pin_has_no_revision_fallback(self):
        self.engine_zip.write_bytes(zip_bytes({'engine/changed.cjs': b'changed'}))
        with self.assertRaisesRegex(ValueError, 'no fallback'):
            backend.engine_files(self.engine_zip)

    def test_offline_and_unapproved_url_refusal(self):
        with self.assertRaisesRegex(ValueError, 'Offline'):
            backend.download(backend.ENGINE_URL, self.base / 'out', offline=True)
        for url in ('http://github.com/x', 'https://example.com/x'):
            with self.assertRaisesRegex(ValueError, 'HTTPS'):
                backend.download(url, self.base / 'out')

    def test_unsupported_build_rejected_before_fetch_or_write(self):
        before = self.snapshot()
        with patch.object(backend.platform, 'system', return_value='Linux'), patch.object(backend.platform, 'machine', return_value='aarch64'), patch.object(backend, 'download', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'Linux x86_64'):
            backend.setup(self.args('--build'))
        self.assertEqual(self.snapshot(), before)

    def test_linux_x86_64_sdk_is_admitted_only_with_required_files(self):
        with patch.object(backend.platform, 'system', return_value='Linux'), patch.object(backend.platform, 'machine', return_value='x86_64'):
            with self.assertRaisesRegex(ValueError, 'Expected regular local file'):
                backend.sdk(self.root)
            for name in ('devkitA64/bin/aarch64-none-elf-g++', 'tools/bin/elf2nso',
                         'libounce/switch2_cpu.mk', 'libounce/lib/libounce_core.a'):
                self.put('vendor/devkitpro/' + name, b'pinned SDK fixture')
            self.assertEqual(backend.sdk(self.root), (self.root / 'vendor/devkitpro').resolve())

    def test_build_runs_only_in_private_copy(self):
        for name in ('include', 'source', 'native', 'branding', 'web', 'tests', 'runtime/kaihen'):
            self.put(name + '/source.txt', b'source')
        self.put('Makefile', b'make'); self.put('tools/embed-web.py', b'embed')
        self.put('runtime/kaihen/hen/build/do-not-copy', b'tested'); self.put('state/private', b'private')
        before = self.snapshot(); work = self.base / 'work'; work.mkdir(); calls = []
        def make(command, **kwargs):
            calls.append(command); self.assertTrue(kwargs['check']); self.assertNotIn('shell', kwargs)
            directory = pathlib.Path(command[2]); self.assertTrue(directory.is_relative_to(work))
            if directory.name == 'hen':
                (directory / 'build').mkdir(); (directory / 'build/main').write_bytes(self.hen)
                self.assertFalse((directory / 'build/do-not-copy').exists())
            else:
                (directory / 'dist').mkdir()
                for name, data in backend.programs(backend.zip_files(self.programs)).items():
                    (directory / name).write_bytes(data)
                self.assertFalse((directory / 'state').exists()); self.assertTrue((directory / 'tests/source.txt').exists())
        with patch.object(backend, 'sdk', return_value=self.base / 'sdk'), patch.object(backend.subprocess, 'run', side_effect=make):
            hen, app = backend.build_inputs(self.root, work)
        self.assertEqual(hen, self.hen); self.assertEqual(len(calls), 2); self.assertEqual(self.snapshot(), before)


class SourcesOnlyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='Moader source mode '); self.addCleanup(self.temp.cleanup)
        self.base = pathlib.Path(self.temp.name); self.root = self.base / 'clean checkout'; self.root.mkdir()
        self.engine = {'shared/rpc.cjs': b'module.exports={};', 'switch2/ro/payload/main': b'payload'}
        self.archive = self.base / 'upstream.zip'
        self.archive.write_bytes(zip_bytes({'engine/' + name: data for name, data in self.engine.items()} | {'runs/private': b'never copy', '.git/config': b'never copy'}))
        patch.object(backend, 'ENGINE_TREE_SHA', backend.tree_sha(self.engine)).start(); self.addCleanup(patch.stopall)
        for name in ('proof', 'sdk', 'build_inputs', 'check_root', 'node_check'):
            patch.object(backend, name, side_effect=AssertionError('Source mode must not use ' + name)).start()
        patch.object(backend.urllib.request, 'urlopen', side_effect=AssertionError('No network in fixture tests')).start()
        patch.object(backend.subprocess, 'run', side_effect=AssertionError('No subprocess in source mode')).start()

    def args(self, *extra):
        return backend.parser().parse_args(['--root', str(self.root), '--sources-only', *extra])

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns) for p in self.root.rglob('*') if p.is_file()}

    def test_offline_source_only_from_empty_checkout_and_idempotent_validation(self):
        result = backend.setup(self.args('--offline', '--engine-archive', str(self.archive)))
        self.assertEqual(result['mode'], 'sources-only'); self.assertFalse(result['consoleConnected'])
        self.assertEqual(result['newFiles'], len(self.engine)); self.assertEqual(result['engineFiles'], len(self.engine))
        self.assertNotIn('readyForInstallerCheck', result)
        self.assertEqual(set(self.snapshot()), {'research/sources/ns2jb/engine/' + name for name in self.engine})
        self.assertEqual(backend.tree_files(self.root / 'research/sources/ns2jb/engine'), self.engine)
        before = self.snapshot()
        with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()):
            self.assertEqual(backend.setup(self.args('--offline'))['newFiles'], 0)
        self.assertEqual(self.snapshot(), before)

    def test_source_download_uses_only_fixed_upstream_url(self):
        calls = []
        def fetch(url, target, expected=None, offline=False):
            calls.append((url, expected, offline)); target.write_bytes(self.archive.read_bytes()); return target
        with patch.object(backend, 'download', side_effect=fetch):
            backend.setup(self.args())
        self.assertEqual(calls, [(backend.ENGINE_URL, None, False)])
        self.assertFalse((self.root / 'research/artifacts').exists()); self.assertFalse((self.root / 'vendor').exists())
        self.assertFalse((self.root / 'dist').exists()); self.assertFalse((self.root / 'build').exists())

    def test_conflicting_modes_and_non_source_inputs_fail_before_work(self):
        options = [('--check',), ('--build',), ('--node', 'node'), ('--node', ''), ('--release-manifest', str(self.archive)), ('--kaihen-archive', str(self.archive)), ('--programs-archive', str(self.archive)), ('--programs-sha256', 'a' * 64), ('--programs-sha256', ''), ('--hen-binary', str(self.archive))]
        for option in options:
            with self.subTest(option=option), patch.object(backend, 'setup_sources', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'conflicting options'):
                backend.setup(self.args(*option))
        self.assertEqual(self.snapshot(), {})

    def test_offline_missing_archive_stops_without_outputs(self):
        with self.assertRaisesRegex(ValueError, 'Offline'):
            backend.setup(self.args('--offline'))
        self.assertEqual(list(self.root.iterdir()), [])

    def test_changed_existing_tree_stops_without_download_or_overwrite(self):
        target = self.root / 'research/sources/ns2jb/engine'; target.mkdir(parents=True); (target / 'private.cjs').write_bytes(b'keep')
        before = self.snapshot()
        with patch.object(backend, 'download', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'Existing engine differs'):
            backend.setup(self.args('--offline', '--engine-archive', str(self.archive)))
        self.assertEqual(self.snapshot(), before)

    def test_symlink_parent_and_non_directory_parent_fail_before_download(self):
        outside = self.base / 'outside'; outside.mkdir(); (self.root / 'research').symlink_to(outside, target_is_directory=True)
        with patch.object(backend, 'download', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'symlink'):
            backend.setup(self.args())
        self.assertEqual(list(outside.iterdir()), [])
        (self.root / 'research').unlink(); (self.root / 'research').write_bytes(b'keep')
        with patch.object(backend, 'download', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'parent directory'):
            backend.setup(self.args())
        self.assertEqual((self.root / 'research').read_bytes(), b'keep')

    def test_bad_pin_and_traversal_archive_never_publish(self):
        for files in ({'engine/changed.cjs': b'changed'}, {'engine/../escape': b'escape'}):
            with self.subTest(files=files):
                self.archive.write_bytes(zip_bytes(files))
                with patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaises(ValueError):
                    backend.setup(self.args('--offline', '--engine-archive', str(self.archive)))
                self.assertEqual(list(self.root.iterdir()), [])


class Civ7ProvisionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='Moader Civ7 sources '); self.addCleanup(self.temp.cleanup)
        self.base = pathlib.Path(self.temp.name); self.root = self.base / 'kit'; self.root.mkdir()
        self.files = {'engine/run.cjs': b'module.exports={};', 'engine/shared/rpc.cjs': b'authenticated transport',
                      'targets/civ7/target.json': b'{"name":"civ7"}', 'targets/civ7/app/thread-code.json': b'tagged',
                      'bridge/civ_bridge.js': b'bridge'}
        self.archive = self.base / 'civ7.zip'
        prefix = 'civ7-fs-kaihen-' + backend.CIV7_REV + '/'
        self.archive.write_bytes(zip_bytes({prefix + name: data for name, data in self.files.items()} |
            {prefix + '.git/config': b'private', prefix + 'runs/private': b'private', prefix + 'targets/smilebasic/target.json': b'unselected'}))
        patch.object(backend, 'CIV7_TREE_SHA', backend.tree_sha(self.files)).start(); self.addCleanup(patch.stopall)
        patch.object(backend.urllib.request, 'urlopen', side_effect=AssertionError('No network in synthetic tests')).start()
        patch.object(backend.subprocess, 'run', side_effect=AssertionError('No target or Node execution in source mode')).start()

    def args(self, *extra):
        return backend.parser().parse_args(['--root', str(self.root), '--host-profile', 'civ7-s8n', '--sources-only', '--offline', '--engine-archive', str(self.archive), *extra])

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns) for p in self.root.rglob('*') if p.is_file()}

    def test_selected_closure_includes_bridge_and_preserves_other_profiles(self):
        legacy = self.root / 'research/sources/ns2jb/engine/private'; legacy.parent.mkdir(parents=True); legacy.write_bytes(b'keep')
        result = backend.setup(self.args())
        self.assertEqual(result['hostProfile'], 'civ7-s8n'); self.assertEqual(result['engineRevision'], backend.CIV7_REV)
        self.assertEqual(result['engineFiles'], len(self.files)); self.assertFalse(result['consoleConnected'])
        selected = self.root / backend.source_profile('civ7-s8n')['directory']
        self.assertEqual(backend.tree_files(selected), self.files)
        self.assertEqual(legacy.read_bytes(), b'keep'); self.assertFalse((self.root / 'vendor').exists())

    def test_existing_full_checkout_is_readonly_and_only_selected_subtrees_are_qualified(self):
        backend.setup(self.args()); selected = self.root / backend.source_profile('civ7-s8n')['directory']
        for name in ('README.md', '.git/config', 'runs/private', 'targets/smilebasic/target.json'):
            file = selected / name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(b'keep')
        before = self.snapshot()
        with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()):
            self.assertEqual(backend.setup(self.args())['newFiles'], 0)
        self.assertEqual(self.snapshot(), before)

    def test_wrong_sdk_missing_bridge_and_extra_import_are_rejected_without_overwrite(self):
        backend.setup(self.args()); selected = self.root / backend.source_profile('civ7-s8n')['directory']
        for name, data in (('targets/civ7/target.json', b'wrong SDK'), ('bridge/civ_bridge.js', None), ('engine/shared/extra.cjs', b'extra')):
            with self.subTest(name=name):
                file = selected / name; old = file.read_bytes() if file.exists() else None
                if data is None: file.unlink()
                else: file.write_bytes(data)
                before = self.snapshot()
                with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaises(ValueError):
                    backend.setup(self.args())
                self.assertEqual(self.snapshot(), before)
                if old is None: file.unlink()
                else: file.write_bytes(old)

    def test_unqualified_source_provider_or_missing_bridge_never_publishes(self):
        for files in ({'engine/run.cjs': b'another provider'}, {name: data for name, data in self.files.items() if not name.startswith('bridge/')}):
            self.archive.write_bytes(zip_bytes(files))
            with patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'no fallback'):
                backend.setup(self.args())
        self.assertEqual(self.snapshot(), {})

    def test_profile_symlink_is_rejected_before_fetch(self):
        target = self.root / 'research/sources/civ7-fs-kaihen'; target.parent.mkdir(parents=True)
        outside = self.base / 'outside'; outside.mkdir(); target.symlink_to(outside, target_is_directory=True)
        with patch.object(backend, 'download', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'symlink'):
            backend.setup(self.args())
        self.assertEqual(list(outside.iterdir()), [])

    def test_real_public_pin_matches_shared_provider_descriptor(self):
        descriptor = json.loads((backend.ROOT / 'config/civ7-provider.json').read_text())
        self.assertEqual(descriptor['id'], 'civ7-s8n'); self.assertEqual(descriptor['revision'], backend.CIV7_REV)
        self.assertEqual(descriptor['archiveUrl'], backend.CIV7_URL)
        # setUp changes only the fixture hash; check the independent public literal.
        self.assertEqual(descriptor['sourceTreeSha256'], 'd83cadc568b65a724f32b848f1681540487a798b354ac3286f71e41e94a9ac75')
        self.assertIn('bridge/civ_bridge.js', descriptor['files'])


class SeedsowProvisionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='Moader Seedsow sources '); self.addCleanup(self.temp.cleanup)
        self.base = pathlib.Path(self.temp.name); self.root = self.base / 'kit'; self.root.mkdir()
        self.engine = 'ns2jb-main/ns2jb-main/engine'
        self.files = {self.engine + '/shared/sdk-functions.json': b'{"SDK":1}',
                      self.engine + '/shared/sdk-layout.json': b'{"layout":1}',
                      self.engine + '/switch2/app/thread-code.json': b'{"code":1}'}
        self.files.update({'tools/' + name + '.cjs': b'never import ' + name.encode() for name in
                          ('seedsow-bridge', 'console-rpc', 'console-host', 'console-http', 'refresh-mounts')})
        self.files.update({'console/' + name + '.lua': b'never execute ' + name.encode() for name in
                          ('n2h_rpc', 'n2h_pump', 'reload_rpc', 'n2h_accessor')})
        self.files['env.txt'] = b'fixture environment'
        self.descriptor = json.loads((backend.ROOT / 'config/seedsow-provider.json').read_text())
        self.descriptor['files'] = {name: backend.digest(data) for name, data in self.files.items()}
        self.descriptor['sourceTreeSha256'] = backend.tree_sha(self.files)
        self.descriptor['archiveSha256'] = 'a' * 64
        self.metadata = self.base / 'config/seedsow-provider.json'; self.metadata.parent.mkdir()
        self.metadata.write_text(json.dumps(self.descriptor))
        self.archive = self.base / 'seedsow.zip'
        self.prefix = 'seedsow-fs-kaihen-' + backend.SEEDSOW_REV + '/'
        self.write_zip()
        self.addCleanup(patch.stopall)
        patch.object(backend, 'ROOT', self.base).start()
        patch.object(backend, 'SEEDSOW_TREE_SHA', self.descriptor['sourceTreeSha256']).start()
        patch.object(backend, 'SEEDSOW_ARCHIVE_SHA', self.descriptor['archiveSha256']).start()
        patch.object(backend.urllib.request, 'urlopen', side_effect=AssertionError('No network in Seedsow tests')).start()
        patch.object(backend.subprocess, 'run', side_effect=AssertionError('No upstream, native or Node execution')).start()

    def write_zip(self, files=None, prefix=None):
        self.archive.write_bytes(zip_bytes({(self.prefix if prefix is None else prefix) + name: data
                                           for name, data in (self.files if files is None else files).items()}))

    def args(self, *extra):
        return backend.parser().parse_args(['--root', str(self.root), '--host-profile', 'seedsow-malev0',
                                           '--sources-only', '--offline', '--engine-archive', str(self.archive), *extra])

    def selected(self):
        return self.root / 'research/sources/seedsow-fs-kaihen'

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns)
                for p in self.root.rglob('*') if p.is_file()}

    def test_nested_sources_only_are_provisioned_and_other_inputs_preserved(self):
        private = ('research/sources/ns2jb/engine/private', 'research/sources/civ7-fs-kaihen/engine/private',
                   'vendor/kaihen-mo/hen/main', 'dist/Moader.nro', 'build/install/prepared-old/manifest.json')
        for name in private:
            file = self.root / name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(b'keep')
        before = self.snapshot()
        self.write_zip(self.files | {name: b'not source' for name in
            ('.git/config', 'runs/latest.txt', 'runs/switch-ABCDE/connection.json', 'tools/gui.cjs', 'env.local', 'bridge.pid', 'console-host.txt')})
        result = backend.setup(self.args())
        self.assertEqual(result['hostProfile'], 'seedsow-malev0'); self.assertEqual(result['engineFiles'], len(self.files))
        self.assertFalse(result['consoleConnected']); self.assertNotIn('readyForInstallerCheck', result)
        self.assertEqual(backend.tree_files(self.selected()), self.files)
        self.assertTrue(all(self.snapshot()[name] == value for name, value in before.items()))
        saved = self.snapshot()
        with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()):
            self.assertEqual(backend.setup(self.args())['newFiles'], 0)
        self.assertEqual(self.snapshot(), saved)

    def test_canonical_tar_requires_byte_and_selected_tree_pins(self):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode='w:gz') as archive:
            for name, content in self.files.items():
                row = tarfile.TarInfo(self.prefix + name); row.size = len(content); archive.addfile(row, io.BytesIO(content))
        self.archive = self.base / 'seedsow.tar.gz'; self.archive.write_bytes(data.getvalue())
        with self.assertRaisesRegex(ValueError, 'Archive checksum differs'):
            backend.setup(self.args())
        self.assertEqual(self.snapshot(), {})
        self.descriptor['archiveSha256'] = backend.digest(data.getvalue()); self.metadata.write_text(json.dumps(self.descriptor))
        with patch.object(backend, 'SEEDSOW_ARCHIVE_SHA', self.descriptor['archiveSha256']):
            self.assertEqual(backend.setup(self.args())['newFiles'], len(self.files))
        self.assertEqual(backend.tree_files(self.selected()), self.files)

    def test_missing_tampered_extra_engine_and_wrong_prefix_never_publish(self):
        variants = [self.files | {self.engine + '/shared/sdk-functions.json': b'tampered'},
                    {name: data for name, data in self.files.items() if name != 'tools/seedsow-bridge.cjs'},
                    self.files | {self.engine + '/shared/extra.cjs': b'extra'},
                    self.files | {self.engine + '/connection.json': b'generated runtime state'}]
        for files in variants:
            with self.subTest(files=list(set(files) ^ set(self.files))):
                self.write_zip(files)
                with patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'no fallback'):
                    backend.setup(self.args())
                self.assertEqual(self.snapshot(), {})
        for prefix in ('', 'seedsow-fs-kaihen-wrong-revision/', 'civ7-fs-kaihen-' + backend.CIV7_REV + '/'):
            self.write_zip(prefix=prefix)
            with patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'no fallback'):
                backend.setup(self.args())

    def test_full_checkout_is_readonly_and_changed_sources_are_never_replaced(self):
        backend.setup(self.args())
        for name in ('README.md', '.git/config', 'runs/latest.txt', 'tools/gui.cjs', 'console-host.txt'):
            file = self.selected() / name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(b'keep')
        before = self.snapshot()
        with patch.object(backend, 'download', side_effect=AssertionError()):
            self.assertEqual(backend.setup(self.args())['newFiles'], 0)
        self.assertEqual(self.snapshot(), before)
        for name, data in ((self.engine + '/shared/sdk-layout.json', b'changed'), ('console/n2h_rpc.lua', None),
                           (self.engine + '/extra.cjs', b'extra'), (self.engine + '/connection.json', b'live endpoint')):
            with self.subTest(name=name):
                file = self.selected() / name; old = file.read_bytes() if file.exists() else None
                if data is None: file.unlink()
                else: file.write_bytes(data)
                saved = self.snapshot()
                with patch.object(backend, 'download', side_effect=AssertionError()), patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaises(ValueError):
                    backend.setup(self.args())
                self.assertEqual(self.snapshot(), saved)
                if old is None: file.unlink()
                else: file.write_bytes(old)

    def test_descriptor_identity_and_file_map_tampering_fail_before_publication(self):
        for field, value in (('revision', 'bad'), ('sourceDirectory', '../escape'), ('archiveSha256', 'b' * 64),
                             ('sourceTreeAlgorithm', 'other'), ('files', {'../escape': 'a' * 64}),
                             ('files', {'tools/seedsow-bridge.cjs': 'b' * 64})):
            with self.subTest(field=field):
                self.metadata.write_text(json.dumps(self.descriptor | {field: value}))
                with patch.object(backend, 'publish_new', side_effect=AssertionError()), self.assertRaises(ValueError):
                    backend.setup(self.args())
                self.assertEqual(self.snapshot(), {})

    def test_symlink_and_archive_attacks_do_not_publish(self):
        self.write_zip(self.files | {'../escape': b'bad'})
        with self.assertRaisesRegex(ValueError, 'Unsafe archive path'): backend.setup(self.args())
        with zipfile.ZipFile(self.archive, 'w') as archive:
            row = zipfile.ZipInfo(self.prefix + self.engine + '/link'); row.create_system = 3
            row.external_attr = (stat.S_IFLNK | 0o777) << 16; archive.writestr(row, 'outside')
        with self.assertRaisesRegex(ValueError, 'links/special'): backend.setup(self.args())
        self.write_zip()
        with patch.object(backend, 'MAX_ARCHIVE', 1), self.assertRaisesRegex(ValueError, 'Source archive exceeds size bound'): backend.engine_files(self.archive, 'seedsow-malev0')
        with patch.object(backend, 'MAX_ENTRIES', 1), self.assertRaisesRegex(ValueError, 'Too many ZIP entries'): backend.setup(self.args())
        outside = self.base / 'outside'; outside.mkdir(); self.selected().parent.mkdir(parents=True)
        self.selected().symlink_to(outside, target_is_directory=True)
        with patch.object(backend, 'download', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'symlink'):
            backend.setup(self.args())
        self.assertEqual(list(outside.iterdir()), [])

    def test_offline_missing_input_and_source_mode_conflicts_stop_before_work(self):
        args = self.args(); args.engine_archive = None
        with self.assertRaisesRegex(ValueError, 'Offline'): backend.setup(args)
        self.assertEqual(self.snapshot(), {})
        for flags in (('--check',), ('--build',), ('--node', 'node'), ('--hen-binary', str(self.archive))):
            with patch.object(backend, 'setup_sources', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'conflicting options'):
                backend.setup(self.args(*flags))


class SeedsowPinTests(unittest.TestCase):
    def test_real_descriptor_closure_is_independently_pinned(self):
        profile = backend.source_profile('seedsow-malev0')
        self.assertEqual(profile['revision'], '7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17')
        self.assertEqual(profile['archiveSha256'], '616660cb65acc703402a212e8691aff84b7f4c228aeedcef80a7763130291976')
        self.assertEqual(profile['sha256'], '4d4375f2729ca4b95a98348419c1e2b7edffa684bf53316f00a38194c5f4ca7b')
        self.assertEqual(len(profile['files']), 140)
        self.assertEqual(sum(name.startswith(profile['engine'] + '/') for name in profile['files']), 130)
        self.assertEqual(profile['directory'], 'research/sources/seedsow-fs-kaihen')


class WrapperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='Moader spaces '); self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.node = self.root / 'mac2jb Node 24/node'; self.node.parent.mkdir(); self.node.write_bytes(b'fake node'); self.node.chmod(0o700)
        self.run = self.root / 'runs/switch2-AbC123'; self.run.mkdir(parents=True)
        self.rpc = self.root / 'untrusted rpc/rpc.cjs'
        import shlex
        self.status = shlex.join([str(self.node), str(self.rpc), str(self.run), 'status'])

    def test_parse_exact_status_preserves_spaces_and_executable(self):
        self.assertEqual(wrapper.parse_status(self.status), (str(self.node), self.run))

    def test_malformed_or_shell_status_rejected(self):
        for command in (self.status + '; touch /tmp/no', self.status.replace('status', '"{}"'), 'node rpc.cjs run status', self.status + ' extra', self.status.replace('switch2-AbC123', 'wrong')):
            with self.subTest(command=command), self.assertRaises(ValueError):
                wrapper.parse_status(command)

    def test_parse_only_does_not_execute(self):
        with patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), patch('sys.stdout', new=io.StringIO()):
            self.assertIsNone(wrapper.main(['--status-command', self.status]))

    def test_prepare_uses_local_scripts_exact_node_and_unique_manifest(self):
        manifest = self.root / 'build/install/prepared-abc/manifest.json'; manifest.parent.mkdir(parents=True); manifest.write_text('{}')
        calls = []
        def execute(command, **kwargs):
            calls.append(command); self.assertNotIn('shell', kwargs)
            if command[1] == '--version':
                return subprocess.CompletedProcess(command, 0, 'v24.14.0\n', '')
            if command[-1] == '--check':
                return subprocess.CompletedProcess(command, 0, wrapper.PREFIX + str(manifest) + wrapper.SUFFIX + '\n', '')
            return subprocess.CompletedProcess(command, 0)
        with patch.object(wrapper.subprocess, 'run', side_effect=execute):
            command = wrapper.prepare(self.root, str(self.node), self.run)
        self.assertEqual(command, [str(self.node), str(self.root / 'tools/install-moader.cjs'), '--prepared', str(manifest), str(self.run)])
        self.assertEqual(len(calls), 3); self.assertIn(str(self.node), calls[1]); self.assertEqual(calls[2][0], str(self.node))
        self.assertTrue(all(str(self.rpc) not in command for command in calls))
        self.assertTrue(all(str(self.run) not in command for command in calls))

    def test_node26_is_refused_before_setup(self):
        with patch.object(wrapper.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, 'v26.11.1\n', '')) as execute, self.assertRaisesRegex(ValueError, 'Node 24'):
            wrapper.prepare(self.root, str(self.node), self.run)
        self.assertEqual(execute.call_count, 1)

    def test_mutable_pointer_or_ambiguous_output_is_refused(self):
        for path in (self.root / 'build/install/manifest.json', self.root / 'outside/prepared-abc/manifest.json'):
            with self.assertRaisesRegex(ValueError, 'unique'):
                wrapper.prepared_manifest(wrapper.PREFIX + str(path) + wrapper.SUFFIX, self.root)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            wrapper.prepared_manifest('', self.root)

    def test_symlink_manifest_ancestor_is_refused(self):
        outside = self.root / 'outside'; (outside / 'install/prepared-abc').mkdir(parents=True)
        (outside / 'install/prepared-abc/manifest.json').write_text('{}')
        (self.root / 'build').symlink_to(outside, target_is_directory=True)
        manifest = self.root / 'build/install/prepared-abc/manifest.json'
        with self.assertRaisesRegex(ValueError, 'symlink'):
            wrapper.prepared_manifest(wrapper.PREFIX + str(manifest) + wrapper.SUFFIX, self.root)


class DirectRunTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='Moader direct POSIX '); self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.node = self.root / 'Linux Node 24/bin/node'; self.node.parent.mkdir(parents=True)
        self.node.write_bytes(b'fake POSIX node'); self.node.chmod(0o700)
        self.run = self.root / 'mnt/c/Example Projects/ns2jb/runs/switch2-AbC123'; self.run.mkdir(parents=True)
        self.manifest = self.root / 'build/install/prepared-new/manifest.json'; self.manifest.parent.mkdir(parents=True); self.manifest.write_text('{}')
        self.calls = []
        self.output = wrapper.PREFIX + str(self.manifest) + wrapper.SUFFIX + '\n'

    def execute(self, command, **kwargs):
        self.calls.append(command); self.assertNotIn('shell', kwargs)
        if command[1:] == ['--version']:
            return subprocess.CompletedProcess(command, 0, 'v24.14.0\n', '')
        if command[-1] == '--check':
            return subprocess.CompletedProcess(command, 0, self.output, '')
        return subprocess.CompletedProcess(command, 0)

    def invoke(self, *flags):
        return wrapper.main(['--root', str(self.root), '--run-dir', str(self.run), *flags])

    def test_linux_bare_node_default_preserved_and_only_local_checks_execute(self):
        (self.run / 'rpc.cjs').write_text('must never run'); (self.run / 'run.cjs').write_text('must never run')
        stale = self.root / 'build/install/manifest.json'; stale.write_text('stale mutable pointer')
        with patch.object(wrapper.sys, 'platform', 'linux'), patch.object(wrapper.shutil, 'which', return_value=str(self.node)) as which, patch.object(wrapper.subprocess, 'run', side_effect=self.execute), patch('sys.stdout', new=io.StringIO()) as output:
            command = self.invoke('--prepare')
        which.assert_called_once_with('node')
        self.assertEqual(command, [str(self.node), str(self.root / 'tools/install-moader.cjs'), '--prepared', str(self.manifest), str(self.run)])
        import shlex
        self.assertEqual(shlex.split(output.getvalue().splitlines()[-1]), command)
        self.assertEqual(len(self.calls), 3)
        self.assertEqual(self.calls[0], [str(self.node), '--version'])
        self.assertEqual(self.calls[1], [sys.executable, str(self.root / 'tools/setup-backend.py'), '--root', str(self.root), '--node', str(self.node)])
        self.assertEqual(self.calls[2], [str(self.node), str(self.root / 'tools/install-moader.cjs'), '--check'])
        self.assertTrue(all(str(self.run) not in command and '--prepared' not in command for command in self.calls))
        self.assertEqual(stale.read_text(), 'stale mutable pointer')

    def test_explicit_node_path_with_spaces_bypasses_path_search(self):
        with patch.object(wrapper.shutil, 'which', side_effect=AssertionError()), patch.object(wrapper.subprocess, 'run', side_effect=self.execute), patch('sys.stdout', new=io.StringIO()):
            command = self.invoke('--node', str(self.node), '--prepare')
        self.assertEqual(command[0], str(self.node)); self.assertEqual(self.calls[0][0], str(self.node)); self.assertEqual(self.calls[2][0], str(self.node))

    def test_nondefault_profiles_are_passed_to_local_preparation_and_bound_by_manifest(self):
        for profile in ('civ7-s8n', 'seedsow-malev0'):
            with self.subTest(profile=profile):
                self.calls.clear()
                with patch.object(wrapper.shutil, 'which', return_value=str(self.node)), patch.object(wrapper.subprocess, 'run', side_effect=self.execute), patch('sys.stdout', new=io.StringIO()):
                    command = self.invoke('--host-profile', profile, '--prepare')
                self.assertEqual(command, [str(self.node), str(self.root / 'tools/install-moader.cjs'), '--prepared', str(self.manifest), str(self.run)])
                self.assertEqual(len(self.calls), 3)
                for call in self.calls[1:]:
                    index = call.index('--host-profile'); self.assertEqual(call[index + 1], profile)
                self.assertEqual('--offline' in self.calls[1], profile == 'seedsow-malev0')
                self.assertTrue(all(str(self.run) not in call for call in self.calls))

    def test_mac2jb_civswitch_status_is_not_misidentified_as_pinned_s8n(self):
        import shlex
        status = shlex.join([str(self.node), str(self.root / 'rpc.cjs'), str(self.run), 'status'])
        for profile in ('civ7-s8n', 'seedsow-malev0'):
            with self.subTest(profile=profile), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), patch('sys.stderr', new=io.StringIO()) as stderr, self.assertRaises(SystemExit) as error:
                wrapper.main(['--status-command', status, '--host-profile', profile, '--prepare'])
            self.assertEqual(error.exception.code, 2); self.assertIn(profile, stderr.getvalue())

    def test_seedsow_rejects_upstream_attempt_names_without_any_execution(self):
        for name in ('switch-ABCDE', 'switch2-', 'switch2-ABC_def'):
            attempt = self.run.parent / name; attempt.mkdir()
            with self.subTest(name=name), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'run folder name'):
                wrapper.main(['--run-dir', str(attempt), '--host-profile', 'seedsow-malev0', '--node', str(self.node), '--prepare'])

    def test_relative_run_and_node_paths_use_invoking_directory(self):
        relative_run = self.run.relative_to(self.root).as_posix()
        relative_node = './' + self.node.relative_to(self.root).as_posix()
        with patch.object(wrapper.pathlib.Path, 'cwd', return_value=self.root), patch.object(wrapper.subprocess, 'run', side_effect=self.execute), patch('sys.stdout', new=io.StringIO()):
            command = wrapper.main(['--root', str(self.root), '--run-dir', relative_run, '--node', relative_node, '--prepare'])
        self.assertEqual(command[0], str(self.node)); self.assertEqual(command[-1], str(self.run))

    def test_parse_only_resolves_paths_without_executing_any_command(self):
        with patch.object(wrapper.shutil, 'which', return_value=str(self.node)), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), patch('sys.stdout', new=io.StringIO()) as output:
            self.assertIsNone(self.invoke())
        self.assertIn(str(self.node), output.getvalue()); self.assertIn(str(self.run), output.getvalue())

    def test_legitimate_posix_node_symlink_is_preserved(self):
        selected = self.node.parent / 'node24'; selected.symlink_to(self.node)
        with patch.object(wrapper.shutil, 'which', return_value=str(selected)), patch.object(wrapper.subprocess, 'run', side_effect=self.execute), patch('sys.stdout', new=io.StringIO()):
            command = self.invoke('--node', 'node24', '--prepare')
        self.assertEqual(command[0], str(selected)); self.assertEqual(self.calls[0][0], str(selected)); self.assertEqual(self.calls[2][0], str(selected))

    def test_conflicting_routes_or_status_node_override_fail_without_execution(self):
        import shlex
        status = shlex.join([str(self.node), str(self.root / 'rpc.cjs'), str(self.run), 'status'])
        variants = [['--run-dir', str(self.run), '--status-command', status], ['--status-command', status, '--node', str(self.node)], []]
        for flags in variants:
            with self.subTest(flags=flags), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), patch('sys.stderr', new=io.StringIO()), self.assertRaises(SystemExit) as error:
                wrapper.main(flags)
            self.assertEqual(error.exception.code, 2)

    def test_missing_not_executable_and_missing_path_node_are_actionable(self):
        not_executable = self.node.parent / 'not-executable'; not_executable.write_text('not executable')
        for node in (str(self.node.parent / 'missing'), str(not_executable)):
            with self.subTest(node=node), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'executable.*Node.js 24'):
                wrapper.prepare(self.root, node, self.run)
        with patch.object(wrapper.shutil, 'which', return_value=None), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'not found on PATH'):
            wrapper.prepare(self.root, 'node', self.run)

    def test_wrong_node_versions_fail_before_setup_without_retry(self):
        for version in ('v22.0.0', 'v26.11.1', 'not node', ''):
            with self.subTest(version=version), patch.object(wrapper.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, version + '\n', '')) as execute, self.assertRaisesRegex(ValueError, 'Node.js 24'):
                wrapper.prepare(self.root, str(self.node), self.run)
            self.assertEqual(execute.call_count, 1)

    def test_node_launch_failure_is_actionable_and_not_retried(self):
        for error in (OSError('Exec format error'), subprocess.CalledProcessError(1, [str(self.node), '--version'])):
            with self.subTest(error=error), patch.object(wrapper.subprocess, 'run', side_effect=error) as execute, self.assertRaisesRegex(ValueError, 'working POSIX Node.js 24'):
                wrapper.prepare(self.root, str(self.node), self.run)
            self.assertEqual(execute.call_count, 1)

    def test_wsl_windows_node_executables_and_aliases_fail_without_execution(self):
        windows_node = self.root / 'mnt/c/Program Files/nodejs/node.exe'; windows_node.parent.mkdir(parents=True); windows_node.write_bytes(b'MZfake executable'); windows_node.chmod(0o700)
        alias = self.node.parent / 'windows-node'; alias.symlink_to(windows_node)
        renamed = self.node.parent / 'renamed-node'; renamed.write_bytes(b'MZfake executable'); renamed.chmod(0o700)
        for node in (str(windows_node), str(alias), str(renamed), '/mnt/c/Program Files/nodejs/NODE.EXE', 'node.exe'):
            with self.subTest(node=node), patch.object(wrapper.sys, 'platform', 'linux'), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'WSL install Linux Node.js 24'):
                wrapper.prepare(self.root, node, self.run)
        with patch.object(wrapper.shutil, 'which', return_value=str(windows_node)), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'WSL install Linux Node.js 24'):
            wrapper.prepare(self.root, 'node', self.run)

    def test_windows_paths_native_windows_and_controls_are_refused(self):
        for value in ('C:\\Users\\Example\\node.exe', 'C:/Example/Tools/node.exe', '\\\\host\\share\\node'):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'POSIX path'):
                wrapper.resolve_node(value)
            with self.assertRaisesRegex(ValueError, 'POSIX path'):
                wrapper.run_directory(value)
        for character in ('\n', '\r', '\t', '\0', '\x7f'):
            with self.subTest(character=repr(character)), self.assertRaisesRegex(ValueError, 'control characters'):
                wrapper.run_directory(str(self.run) + character)
            with self.assertRaisesRegex(ValueError, 'control characters'):
                wrapper.resolve_node(str(self.node) + character)
        with patch.object(wrapper.sys, 'platform', 'win32'), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'Native Windows is unsupported'):
            self.invoke('--node', str(self.node), '--prepare')

    def test_invalid_missing_and_non_directory_runs_fail_before_node_execution(self):
        file = self.root / 'switch2-test'; file.write_text('not a directory')
        for value in (self.root / 'switch2-Other', file, self.root, ''):
            with self.subTest(value=value), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaises(ValueError):
                wrapper.run_directory(value)

    def test_run_symlinks_and_symlink_parent_traversal_are_refused(self):
        leaf = self.root / 'switch2-safe'; leaf.symlink_to(self.run, target_is_directory=True)
        parent = self.root / 'run-parent'; parent.symlink_to(self.run.parent, target_is_directory=True)
        outside = self.root / 'other'; outside.mkdir()
        lexical_link = self.root / 'link'; lexical_link.symlink_to(outside, target_is_directory=True)
        valid = self.root / 'switch2-normal'; valid.mkdir()
        for path in (leaf, parent / self.run.name, self.root / 'link/../switch2-normal'):
            with self.subTest(path=path), patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'symlink'):
                wrapper.prepare(self.root, str(self.node), path)
        with patch.object(wrapper.pathlib.Path, 'cwd', return_value=self.root), self.assertRaisesRegex(ValueError, 'symlink'):
            wrapper.run_directory('link/../switch2-normal')

    def test_direct_route_rejects_mutable_pointer_output(self):
        self.output = wrapper.PREFIX + str(self.root / 'build/install/manifest.json') + wrapper.SUFFIX
        with patch.object(wrapper.subprocess, 'run', side_effect=self.execute), self.assertRaisesRegex(ValueError, 'unique'):
            wrapper.prepare(self.root, str(self.node), self.run)
        self.assertEqual(len(self.calls), 3)

    def test_setup_failure_stops_without_installer_or_automatic_retry(self):
        def fail_setup(command, **kwargs):
            self.calls.append(command)
            if command[1:] == ['--version']:
                return subprocess.CompletedProcess(command, 0, 'v24.14.0\n', '')
            raise subprocess.CalledProcessError(1, command)
        with patch.object(wrapper.subprocess, 'run', side_effect=fail_setup), self.assertRaises(subprocess.CalledProcessError):
            wrapper.prepare(self.root, str(self.node), self.run)
        self.assertEqual(len(self.calls), 2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
