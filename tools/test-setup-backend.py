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


def zip_bytes(files):
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return result.getvalue()


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
        self.assertEqual(backend.tree_files(self.root / 'vendor/kaihen-mo/programs'), {})
        self.assertEqual(result['bundledPrograms'], [])
        self.assertFalse((self.root / 'vendor/kaihen-mo/licenses').exists())
        self.assertFalse((self.root / 'research/sources/ns2jb/runs').exists())
        before = self.snapshot(); self.assertEqual(backend.setup(self.args())['newFiles'], 0); self.assertEqual(self.snapshot(), before)

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
        self.assertEqual(result['bundledPrograms'], [])
        self.assertTrue(all(self.snapshot()[name] == value for name, value in tested.items()))
        self.assertEqual(backend.tree_files(self.root / 'vendor/kaihen-mo/programs'), {})

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
        with patch.object(backend.platform, 'system', return_value='Linux'), patch.object(backend, 'download', side_effect=AssertionError()), self.assertRaisesRegex(ValueError, 'Apple Silicon'):
            backend.setup(self.args('--build'))
        self.assertEqual(self.snapshot(), before)

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

    def test_civ7_profile_is_passed_to_local_preparation_and_bound_by_manifest(self):
        with patch.object(wrapper.shutil, 'which', return_value=str(self.node)), patch.object(wrapper.subprocess, 'run', side_effect=self.execute), patch('sys.stdout', new=io.StringIO()):
            command = self.invoke('--host-profile', 'civ7-s8n', '--prepare')
        self.assertEqual(command, [str(self.node), str(self.root / 'tools/install-moader.cjs'), '--prepared', str(self.manifest), str(self.run)])
        for call in self.calls[1:]:
            index = call.index('--host-profile'); self.assertEqual(call[index + 1], 'civ7-s8n')
        self.assertTrue(all(str(self.run) not in call for call in self.calls))

    def test_mac2jb_civswitch_status_is_not_misidentified_as_pinned_s8n(self):
        import shlex
        status = shlex.join([str(self.node), str(self.root / 'rpc.cjs'), str(self.run), 'status'])
        with patch.object(wrapper.subprocess, 'run', side_effect=AssertionError()), patch('sys.stderr', new=io.StringIO()), self.assertRaises(SystemExit) as error:
            wrapper.main(['--status-command', status, '--host-profile', 'civ7-s8n', '--prepare'])
        self.assertEqual(error.exception.code, 2)

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
