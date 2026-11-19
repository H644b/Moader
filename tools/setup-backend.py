#!/usr/bin/env python3
"""Download, build or check Moader backend files on this computer. This tool leaves the Switch untouched."""
from __future__ import annotations
import argparse, hashlib, io, json, os, pathlib, platform, re, shutil, stat, subprocess, tarfile, tempfile, urllib.parse, urllib.request, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
NS2JB_REV = '28f03b5895e5569d8fbc35cbed84906b8f0a65b2'
ENGINE_TREE_SHA = '7003335a2a426a82156c19653be5e61506a05dd02b5d0490c8bcb4516c5c354a'
ENGINE_URL = f'https://gitlab.com/kurumuz/ns2jb/-/archive/{NS2JB_REV}/ns2jb-{NS2JB_REV}.tar.gz'
CIV7_REV = '51a804cbce09216e1a42e65929644fd436b4c8bf'
CIV7_TREE_SHA = 'd83cadc568b65a724f32b848f1681540487a798b354ac3286f71e41e94a9ac75'
CIV7_URL = f'https://github.com/s8n/civ7-fs-kaihen/archive/{CIV7_REV}.tar.gz'
SEEDSOW_REV = '7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17'
SEEDSOW_TREE_SHA = '4d4375f2729ca4b95a98348419c1e2b7edffa684bf53316f00a38194c5f4ca7b'
SEEDSOW_ARCHIVE_SHA = '616660cb65acc703402a212e8691aff84b7f4c228aeedcef80a7763130291976'
SEEDSOW_URL = f'https://github.com/Malev0/seedsow-fs-kaihen/archive/{SEEDSOW_REV}.tar.gz'
HOST_PROFILES = ('smilebasic-kurumuz', 'civ7-s8n', 'seedsow-malev0')
KAIHEN_URL = 'https://github.com/ouncebrew/kaihen/releases/download/20261114c/kaiHEN-20261114c.zip'
KAIHEN_SHA = '63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967'
HEN_SHA = 'e6341b1932b450e25f04ccf425c3034bd45dd3cccfadbf5e5fb2778b6a673e5c'
SOURCE_TREE_ALGORITHM = 'sha256-sorted-path-nul-sha256-newline-v1'
PROGRAM_ENTRIES = {'dist/Moader.nro': 'switch/Moader.nro', 'dist/moader.nso': 'kaihen/plugins/moader.nso'}
KAIHEN_COMPONENTS = ('ro/main', 'hen/main', 'game/main', 'bootstrap.bin', 'hbmenu.nro')
KAIHEN_PROGRAMS = ('programs/ounceshell.nro',)
MAX_ARCHIVE, MAX_FILE, MAX_TOTAL, MAX_ENTRIES = 64 << 20, 32 << 20, 96 << 20, 4096


def require(ok, message):
    if not ok:
        raise ValueError(message)


def source_profile(name='smilebasic-kurumuz'):
    require(name in HOST_PROFILES, 'Unknown host profile; choose ' + ', '.join(HOST_PROFILES))
    if name == 'seedsow-malev0':
        descriptor = json.loads(regular(ROOT / 'config/seedsow-provider.json').read_text())
        expected = {'schemaVersion': 1, 'id': name, 'target': 'seedsow', 'repository': 'Malev0/seedsow-fs-kaihen',
                    'revision': SEEDSOW_REV, 'archiveSha256': SEEDSOW_ARCHIVE_SHA,
                    'sourceDirectory': 'research/sources/seedsow-fs-kaihen', 'sourceTreeAlgorithm': SOURCE_TREE_ALGORITHM,
                    'sourceTreeSha256': SEEDSOW_TREE_SHA, 'engineRelativePath': 'ns2jb-main/ns2jb-main/engine'}
        require(isinstance(descriptor, dict) and all(descriptor.get(key) == value for key, value in expected.items()),
                'Seedsow provider descriptor differs from pinned source identity')
        files = descriptor.get('files')
        require(isinstance(files, dict) and files, 'Missing Seedsow source file closure')
        for path, sha in files.items():
            safe_name(path)
            require(isinstance(sha, str) and re.fullmatch(r'[a-f0-9]{64}', sha), 'Invalid Seedsow source file hash')
        require(digest(''.join(path + '\0' + sha + '\n' for path, sha in sorted(files.items())).encode()) == SEEDSOW_TREE_SHA,
                'Seedsow provider file closure differs from pinned source tree')
        return {'id': name, 'revision': SEEDSOW_REV, 'sha256': SEEDSOW_TREE_SHA, 'url': SEEDSOW_URL,
                'archiveSha256': SEEDSOW_ARCHIVE_SHA, 'directory': descriptor['sourceDirectory'],
                'engine': descriptor['engineRelativePath'], 'files': files, 'label': 'Seedsow Malev0 source'}
    if name == 'civ7-s8n':
        return {'id': name, 'revision': CIV7_REV, 'sha256': CIV7_TREE_SHA, 'url': CIV7_URL,
                'directory': 'research/sources/civ7-fs-kaihen', 'label': 'Civ7 s8n source'}
    return {'id': name, 'revision': NS2JB_REV, 'sha256': ENGINE_TREE_SHA, 'url': ENGINE_URL,
            'directory': 'research/sources/ns2jb/engine', 'label': 'ns2jb engine'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    require(path.is_file() and not path.is_symlink(), f'Expected regular local file: {path}')
    return path


def safe_name(name):
    require(isinstance(name, str) and name and '\\' not in name and '\x00' not in name, 'Invalid archive path')
    p = pathlib.PurePosixPath(name)
    require(not p.is_absolute() and all(part not in ('..', '.') for part in name.rstrip('/').split('/')), 'Unsafe archive path: ' + name)
    return p


def zip_files(file):
    result, total, seen = {}, 0, set()
    with zipfile.ZipFile(file) as archive:
        require(len(archive.infolist()) <= MAX_ENTRIES, 'Too many ZIP entries')
        for row in archive.infolist():
            p = safe_name(row.filename)
            require(str(p) not in seen, 'Duplicate ZIP entry: ' + str(p)); seen.add(str(p))
            kind = stat.S_IFMT(row.external_attr >> 16)
            require(kind in (0, stat.S_IFREG, stat.S_IFDIR), 'Archive links/special files are forbidden')
            if row.is_dir():
                continue
            require(0 < row.file_size <= MAX_FILE, 'ZIP member size exceeds bound: ' + str(p))
            total += row.file_size; require(total <= MAX_TOTAL, 'ZIP total exceeds bound')
            data = archive.read(row)
            require(len(data) == row.file_size, 'Short ZIP member')
            result[str(p)] = data
    return result


def tree_sha(files):
    data = ''.join(name + '\0' + digest(value) + '\n' for name, value in sorted(files.items())).encode()
    return digest(data)


def tree_files(root):
    require(root.is_dir() and not root.is_symlink(), f'Expected directory: {root}')
    result = {}
    for file in sorted(root.rglob('*')):
        require(not file.is_symlink(), f'Unexpected symlink: {file}')
        if file.is_file():
            result[file.relative_to(root).as_posix()] = file.read_bytes()
    return result


def engine_files(archive, host_profile='smilebasic-kurumuz'):
    selected, total = {}, 0
    profile = source_profile(host_profile)
    if host_profile == 'seedsow-malev0':
        require(regular(archive).stat().st_size <= MAX_ARCHIVE, 'Source archive exceeds size bound')
    prefixes = ((f'seedsow-fs-kaihen-{SEEDSOW_REV}/',) if host_profile == 'seedsow-malev0' else
                (f'civ7-fs-kaihen-{CIV7_REV}/', '') if host_profile == 'civ7-s8n' else
                (f'ns2jb-{NS2JB_REV}/engine/', 'engine/', 'research/sources/ns2jb/engine/'))
    if zipfile.is_zipfile(archive):
        entries = zip_files(archive)
    else:
        if profile.get('archiveSha256'):
            # The canonical tarball has a byte pin; ZIPs use the same selected
            # source-tree pin with the explicit revision-rooted layout.
            checked_archive(archive, profile['archiveSha256'])
        entries = {}
        with tarfile.open(archive, 'r:*') as source:
            rows = source.getmembers(); require(len(rows) <= MAX_ENTRIES, 'Too many source archive entries')
            for row in rows:
                p = safe_name(row.name)
                if row.isdir():
                    continue
                # Never copy links/devices, even when their names look like source.
                require(row.isfile(), 'Source archive links/special files are forbidden')
                require(0 <= row.size <= MAX_FILE, 'Source member exceeds bound')
                total += row.size; require(total <= MAX_TOTAL, 'Source archive exceeds bound')
                require(str(p) not in entries, 'Duplicate source archive path')
                handle = source.extractfile(row); require(handle is not None, 'Missing archive file')
                data = handle.read(MAX_FILE + 1); require(len(data) == row.size, 'Short source member')
                entries[str(p)] = data
    for name, data in entries.items():
        prefix = next((prefix for prefix in prefixes if name.startswith(prefix)), None)
        if prefix is not None:
            relative = name[len(prefix):]; safe_name(relative)
            if host_profile == 'civ7-s8n' and not (relative.startswith(('engine/', 'targets/civ7/')) or relative == 'bridge/civ_bridge.js'):
                continue
            if host_profile == 'seedsow-malev0' and not (relative.startswith(profile['engine'] + '/') or relative in profile['files']):
                continue
            require(relative not in selected, 'Duplicate pinned engine path')
            selected[relative] = data
    require(selected and tree_sha(selected) == profile['sha256'], 'Pinned ' + profile['label'] + ' content differs; no fallback to another revision')
    return selected


def profile_files(root, profile):
    directory = safe_destination(root, profile['directory'])
    if profile['id'] == 'seedsow-malev0':
        # Keep the original nested engine layout. Extra engine files fail the
        # tree pin; unrelated checkout files are outside this source closure.
        engine = profile['engine']
        target = safe_destination(root, profile['directory'] + '/' + engine)
        result = {engine + '/' + name: data for name, data in tree_files(target).items()}
        for name in profile['files']:
            if not name.startswith(engine + '/'):
                result[name] = regular(safe_destination(root, profile['directory'] + '/' + name)).read_bytes()
        return result
    if profile['id'] != 'civ7-s8n':
        return tree_files(directory)
    # A full upstream checkout can coexist with the selected closure. Inspect
    # every file in executable/profile subtrees, not its .git, runs or README.
    result = {}
    for part in ('engine', 'targets/civ7'):
        target = safe_destination(root, profile['directory'] + '/' + part)
        result.update({part + '/' + name: data for name, data in tree_files(target).items()})
    bridge = safe_destination(root, profile['directory'] + '/bridge/civ_bridge.js')
    result['bridge/civ_bridge.js'] = regular(bridge).read_bytes()
    return result


def download(url, target, expected=None, offline=False):
    parsed = urllib.parse.urlparse(url)
    require(parsed.scheme == 'https' and parsed.hostname in ('github.com', 'gitlab.com'), 'Only explicit HTTPS upstream/release URLs are accepted')
    require(not offline, 'Offline setup needs the missing archive supplied explicitly: ' + url)
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'Moader-setup/0.2.5'}), timeout=60) as reply:
        data = reply.read(MAX_ARCHIVE + 1)
    require(0 < len(data) <= MAX_ARCHIVE, 'Download exceeds archive bound')
    if expected:
        require(digest(data) == expected, 'Downloaded checksum differs: ' + url)
    target.write_bytes(data)
    return target


def checked_archive(path, expected):
    data = regular(path).read_bytes(); require(len(data) <= MAX_ARCHIVE and digest(data) == expected, 'Archive checksum differs: ' + str(path)); return path


def programs(files):
    result = {target: files[entry] for target, entry in PROGRAM_ENTRIES.items() if entry in files}
    require(len(result) == 2, 'Homebrew ZIP must contain switch/Moader.nro and kaihen/plugins/moader.nso')
    validate_programs(result); return result


def validate_programs(files):
    app, service = files['dist/Moader.nro'], files['dist/moader.nso']
    require(20 <= len(app) <= MAX_FILE and app[16:20] == b'NRO0', 'Invalid Moader NRO')
    require(4 <= len(service) <= MAX_FILE and service[:4] == b'NSO0', 'Invalid resident NSO')


def kaihen_source_sha(root):
    base = root / 'runtime/kaihen'
    require(base.is_dir() and not base.is_symlink(), 'Expected native source directory')
    files = {}
    for file in sorted(base.rglob('*')):
        relative = file.relative_to(base)
        if any(part in ('build', '.git', '__pycache__') for part in relative.parts) or file.name == '.DS_Store':
            continue
        require(not file.is_symlink(), 'Native source symlink is forbidden')
        if file.is_dir():
            continue
        data = regular(file).read_bytes()
        require(0 < len(data) <= MAX_FILE, 'Invalid native source file size')
        files[relative.as_posix()] = data
    require(0 < len(files) <= MAX_ENTRIES, 'Invalid native source file count')
    return tree_sha(files)


def proof(root):
    p = json.loads(regular(root / 'software-builds/kaihen-mo.json').read_text())
    require(isinstance(p, dict) and ('schema' not in p or type(p['schema']) is int), 'Invalid kaiHEN build proof schema')
    patch = regular(root / 'software-builds/kaihen-mo.patch').read_bytes()
    if p.get('schema', 1) == 1:
        require(p.get('hen_sha256') == HEN_SHA and not any(key in p for key in
                ('components', 'bootstrap_sha256', 'protocol_version', 'source_tree_sha256')), 'Public legacy HEN proof/hash differs or mixes new descriptors')
        return p
    require(p.get('schema') in (2, 3), 'Unsupported kaiHEN build proof schema')
    require(p.get('base_commit') == 'e897f991524590833b82fbe8d63a3c6775fb8101' and p.get('release_base') == '20261114c' and
            p.get('patch') == 'software-builds/kaihen-mo.patch' and p.get('protocol_version') == 7, 'Unqualified kaiHEN base, patch or protocol')
    require(p.get('native_build_passed') is True and type(p.get('console_retest_passed')) is bool, 'Missing explicit native build/console validation scope')
    require(p.get('source_tree_algorithm') == SOURCE_TREE_ALGORITHM and p.get('source_tree_sha256') == kaihen_source_sha(root), 'kaiHEN source identity differs from build proof')
    require(p.get('patch_sha256') == digest(patch), 'kaiHEN patch identity differs from build proof')
    components = p.get('components')
    expected = {'hen/main', 'bootstrap.bin'} | ({'ro/main'} if p['schema'] == 3 else set())
    require(isinstance(components, dict) and set(components) == expected,
            'Build proof requires the exact RO/HEN/bootstrap set' if p['schema'] == 3 else 'Build proof requires exactly the HEN/bootstrap pair')
    for name, item in components.items():
        require(isinstance(item, dict) and set(item) == {'bytes', 'sha256'} and type(item['bytes']) is int and 0 < item['bytes'] <= MAX_FILE and
                isinstance(item['sha256'], str) and re.fullmatch(r'[a-f0-9]{64}', item['sha256']), 'Invalid patched component descriptor: ' + name)
    require(p.get('hen_sha256') == components['hen/main']['sha256'], 'HEN proof descriptors disagree')
    protocol = regular(root / 'runtime/kaihen/libkaihen/include/kaihen/protocol.h').read_text()
    require(re.search(r'^#define HEN_PROTOCOL_VERSION\s+7\s*$', protocol, re.M), 'Native protocol header differs from build proof')
    return p


def patched_components(p):
    return p['components'] if p.get('schema') in (2, 3) else {'hen/main': {'sha256': HEN_SHA}}


def qualify_patched(data, name, p):
    item = patched_components(p)[name]
    require(0 < len(data) <= MAX_FILE and digest(data) == item['sha256'] and ('bytes' not in item or len(data) == item['bytes']),
            'Patched component does not match pinned build/tested bytes: ' + name)
    if name in ('hen/main', 'ro/main'):
        require(data[:4] == b'NSO0', 'Invalid patched native NSO: ' + name)


def sdk(root):
    system, machine = platform.system(), platform.machine().lower()
    require((system == 'Darwin' and machine in ('arm64', 'aarch64')) or
            (system == 'Linux' and machine in ('x86_64', 'amd64')),
            '--build requires the pinned Apple Silicon macOS or Linux x86_64 SDK')
    result = root / 'vendor/devkitpro'
    for name in ('devkitA64/bin/aarch64-none-elf-g++', 'tools/bin/elf2nso', 'libounce/switch2_cpu.mk', 'libounce/lib/libounce_core.a'):
        regular(result / name)
    return result.resolve()


def build_inputs(root, work):
    p = proof(root)
    devkit = sdk(root)
    source = work / 'source-build'; source.mkdir()
    # Source allowlist: no research, state, dist, build, .git or device files.
    for name in ('include', 'source', 'native', 'branding', 'web', 'tests', 'runtime/kaihen'):
        shutil.copytree(root / name, source / name, ignore=shutil.ignore_patterns('build', '.git', '__pycache__', 'review', '.DS_Store'))
    for name in ('Makefile', 'tools/embed-web.py'):
        target = source / name; target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(root / name, target)
    subprocess.run(['make', '-C', str(source / 'runtime/kaihen/hen'), '-j4', 'DEVKITPRO=' + str(devkit), 'LIBOUNCE=' + str(devkit / 'libounce')], check=True)
    hen = (source / 'runtime/kaihen/hen/build/main').read_bytes()
    qualify_patched(hen, 'hen/main', p)
    pair = {'hen/main': hen}
    if p.get('schema') in (2, 3):
        subprocess.run(['make', '-C', str(source / 'runtime/kaihen/bootstrap'), '-j4', 'DEVKITPRO=' + str(devkit), 'LIBOUNCE=' + str(devkit / 'libounce')], check=True)
        bootstrap = regular(source / 'runtime/kaihen/bootstrap/build/bootstrap.bin').read_bytes()
        qualify_patched(bootstrap, 'bootstrap.bin', p); pair['bootstrap.bin'] = bootstrap
        if p.get('schema') == 3:
            subprocess.run(['make', '-C', str(source / 'runtime/kaihen/ro'), '-j4',
                            'DEVKITPRO=' + str(devkit), 'LIBOUNCE=' + str(devkit / 'libounce')], check=True)
            ro = regular(source / 'runtime/kaihen/ro/build/main').read_bytes()
            qualify_patched(ro, 'ro/main', p); pair['ro/main'] = ro
        require(kaihen_source_sha(source) == p['source_tree_sha256'], 'Copied native build source identity changed')
    subprocess.run(['make', '-C', str(source), '-j4', 'DEVKITPRO=' + str(devkit)], check=True)
    result = {name: regular(source / name).read_bytes() for name in PROGRAM_ENTRIES}
    validate_programs(result); return pair if p.get('schema') in (2, 3) else hen, result


def safe_destination(root, relative):
    target = root / relative
    for part in (target, *target.parents):
        if part == root.parent:
            break
        require(not part.is_symlink(), 'Refusing symlink destination: ' + str(part))
        require(part == target or not part.exists() or part.is_dir(), 'Expected destination parent directory: ' + str(part))
    return target


def publish_new(root, files, directories=()):
    # Qualify every existing destination before the first write. No --force.
    for relative in directories:
        target = safe_destination(root, relative)
        require(not target.exists() or target.is_dir(), 'Expected directory destination: ' + str(target))
    for relative, data in files.items():
        target = safe_destination(root, relative)
        if target.exists():
            require(regular(target).read_bytes() == data, 'Refusing to replace existing input: ' + str(target))
    written = 0
    for relative in directories:
        safe_destination(root, relative).mkdir(parents=True, exist_ok=True)
    for relative, data in files.items():
        target = safe_destination(root, relative)
        if target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation also rejects a concurrent writer after preflight.
        with target.open('xb') as out:
            out.write(data)
        written += 1
    return written


def node_check(root, node):
    version = subprocess.run([node, '--version'], check=True, capture_output=True, text=True).stdout.strip()
    require(version.startswith('v') and int(version[1:].split('.')[0]) >= 22, 'Node.js 22+ is required; preserve mac2jb Node 24 for live installation')
    subprocess.run([node, '-e', 'require("./runtime/modloader/lib/native/artifacts.cjs").artifacts(); console.log("Native source/prebuilt hashes verified (local only)")'], cwd=root, check=True)
    return version


def bundled_programs(root, allow_missing=False):
    directory = root / 'vendor/kaihen-mo/programs'
    if allow_missing and not directory.exists() and not directory.is_symlink():
        return {}
    result = tree_files(directory)
    for name, data in result.items():
        require(0 < len(data) <= MAX_FILE, 'Existing optional bundled program exceeds size bounds: ' + name)
    return result


def check_root(root, node='node', host_profile='smilebasic-kurumuz'):
    profile = source_profile(host_profile)
    p = proof(root)
    archive = checked_archive(root / 'research/artifacts/kaiHEN-20261114c.zip', KAIHEN_SHA)
    shipped = zip_files(archive)
    engine = profile_files(root, profile)
    require(tree_sha(engine) == profile['sha256'], 'Local engine is not the pinned source tree')
    for name in KAIHEN_COMPONENTS:
        require(name in shipped, 'Incomplete official kaiHEN archive: ' + name)
        data = shipped[name]
        actual = regular(root / 'vendor/kaihen-mo' / name).read_bytes()
        if name in patched_components(p):
            qualify_patched(actual, name, p)
        else:
            require(actual == data, 'Local kaiHEN component differs: ' + name)
    bundled = bundled_programs(root)
    require(bundled.get('ounceshell.nro') == shipped[KAIHEN_PROGRAMS[0]],
            'Local kaiHEN programs differ from the complete official release')
    built = {name: regular(root / name).read_bytes() for name in PROGRAM_ENTRIES}; validate_programs(built)
    version = node_check(root, node)
    return {'readyForInstallerCheck': True, 'consoleConnected': False, 'node': version, 'hostProfile': profile['id'], 'engineRevision': profile['revision'],
            'engineTreeSha256': profile['sha256'], 'henSha256': p['hen_sha256'], 'bundledPrograms': sorted(bundled),
            'kaihenProofSchema': p.get('schema', 1), 'kaihenConsoleRetestPassed': p.get('console_retest_passed', False),
            **({'bootstrapSha256': p['components']['bootstrap.bin']['sha256'], 'kaihenProtocolVersion': p['protocol_version']} if p.get('schema') in (2, 3) else {}),
            **({'roSha256': p['components']['ro/main']['sha256']} if p.get('schema') == 3 else {}),
            'programs': {name: {'bytes': len(data), 'sha256': digest(data)} for name, data in built.items()}}


def setup_sources(root, args):
    profile = source_profile(args.host_profile)
    target = safe_destination(root, profile['directory'])
    written = 0
    if target.exists():
        engine = profile_files(root, profile)
        require(tree_sha(engine) == profile['sha256'], 'Existing engine differs; use a separate clean checkout')
    else:
        with tempfile.TemporaryDirectory(prefix='moader-engine-source-') as temporary:
            archive = args.engine_archive or download(profile['url'], pathlib.Path(temporary) / 'engine.tar.gz', expected=profile.get('archiveSha256'), offline=args.offline)
            regular(archive)
            require(archive.stat().st_size <= MAX_ARCHIVE, 'Source archive exceeds size bound')
            engine = engine_files(archive, args.host_profile)
            written = publish_new(root, {profile['directory'] + '/' + name: data for name, data in engine.items()})
        require(tree_sha(profile_files(root, profile)) == profile['sha256'], 'Published engine differs from pinned source tree')
    return {'mode': 'sources-only', 'consoleConnected': False, 'hostProfile': profile['id'], 'engineRevision': profile['revision'],
            'engineTreeSha256': profile['sha256'], 'engineFiles': len(engine),
            'engineBytes': sum(map(len, engine.values())), 'newFiles': written}


def setup(args):
    profile = source_profile(args.host_profile)
    require(not (args.check and args.build), '--check cannot build')
    if args.sources_only:
        conflicts = [name.replace('_', '-') for name in ('check', 'build', 'node', 'release_manifest', 'kaihen_archive', 'programs_archive', 'programs_sha256', 'hen_binary', 'bootstrap_binary', 'ro_binary') if getattr(args, name) not in (None, False)]
        require(not conflicts, '--sources-only accepts only --root, --host-profile, --offline and --engine-archive; conflicting options: ' + ', '.join('--' + name for name in conflicts))
        return setup_sources(args.root.resolve(), args)
    root = args.root.resolve(); p = proof(root)
    if args.check:
        return check_root(root, args.node or 'node', args.host_profile)
    if args.build:
        sdk(root)  # Reject unsupported/missing toolchains before any download or write.
    manifest_file = args.release_manifest or root / 'release/backend-inputs.json'
    manifest = json.loads(manifest_file.read_text()) if manifest_file.exists() else {'schema': 1}
    require(manifest.get('schema') == 1, 'Unsupported public backend-input manifest')
    # Existing source trees are never replaced, even by a correct download.
    engine_target = safe_destination(root, profile['directory'])
    if engine_target.exists():
        require(tree_sha(profile_files(root, profile)) == profile['sha256'], 'Existing engine differs; use a separate clean checkout')
    existing_archive = root / 'research/artifacts/kaiHEN-20261114c.zip'
    if existing_archive.exists():
        checked_archive(existing_archive, KAIHEN_SHA)
    with tempfile.TemporaryDirectory(prefix='moader-backend-setup-') as temporary:
        work = pathlib.Path(temporary)
        archive = args.kaihen_archive or (existing_archive if existing_archive.exists() else download(KAIHEN_URL, work / 'kaihen.zip', KAIHEN_SHA, args.offline))
        checked_archive(archive, KAIHEN_SHA); shipped = zip_files(archive)
        require(all(name in shipped for name in (*KAIHEN_COMPONENTS, *KAIHEN_PROGRAMS)),
                'Incomplete official kaiHEN archive')
        bundled_programs(root, allow_missing=True)
        if engine_target.exists():
            engine = profile_files(root, profile)
        else:
            engine_archive = args.engine_archive or download(profile['url'], work / 'engine.tar.gz', expected=profile.get('archiveSha256'), offline=args.offline)
            engine = engine_files(engine_archive, args.host_profile)
        built, app = None, None
        if args.build:
            built, app = build_inputs(root, work)
        pair = built if isinstance(built, dict) else ({'hen/main': built} if built is not None else {})
        require(p.get('schema') in (2, 3) or not args.bootstrap_binary, '--bootstrap-binary requires a paired build proof')
        require(p.get('schema') == 3 or not args.ro_binary, '--ro-binary requires the schema-3 RO build proof')
        for name, pin in patched_components(p).items():
            if name not in pair:
                explicit = {'hen/main': args.hen_binary, 'bootstrap.bin': args.bootstrap_binary,
                            'ro/main': args.ro_binary}.get(name)
                local = root / 'vendor/kaihen-mo' / name
                release = manifest.get({'hen/main': 'patched_hen', 'bootstrap.bin': 'patched_bootstrap',
                                        'ro/main': 'patched_ro'}[name], {})
                if explicit:
                    pair[name] = regular(explicit).read_bytes()
                elif local.exists():
                    pair[name] = regular(local).read_bytes()
                elif release.get('url'):
                    pair[name] = download(release['url'], work / {'hen/main': 'patched-hen.nso',
                        'bootstrap.bin': 'bootstrap.bin', 'ro/main': 'patched-ro.nso'}[name], pin['sha256'], args.offline).read_bytes()
                else:
                    raise ValueError('Missing pinned patched component '+name+': use the paired host kit, explicit binary, or --build')
            qualify_patched(pair[name], name, p)
        if app is None:
            if all((root / name).exists() for name in PROGRAM_ENTRIES):
                app = {name: regular(root / name).read_bytes() for name in PROGRAM_ENTRIES}; validate_programs(app)
            else:
                release = manifest.get('programs', {})
                expected = args.programs_sha256 or release.get('sha256')
                require(isinstance(expected, str) and len(expected) == 64 and all(c in '0123456789abcdef' for c in expected), 'Missing pinned homebrew ZIP checksum; use host kit or public release manifest')
                program_archive = args.programs_archive or download(release.get('url', ''), work / 'programs.zip', expected, args.offline)
                checked_archive(program_archive, expected); app = programs(zip_files(program_archive))
        files = {'research/artifacts/kaiHEN-20261114c.zip': archive.read_bytes(), **app}
        files.update({profile['directory'] + '/' + name: data for name, data in engine.items()})
        files.update({'vendor/kaihen-mo/' + name: pair.get(name, shipped[name]) for name in KAIHEN_COMPONENTS})
        files.update({'vendor/kaihen-mo/' + name: shipped[name] for name in KAIHEN_PROGRAMS})
        written = publish_new(root, files, directories=('vendor/kaihen-mo/programs',))
    result = check_root(root, args.node or 'node', args.host_profile); result['newFiles'] = written; return result


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=pathlib.Path, default=ROOT, help='path to the extracted backend kit or source checkout')
    p.add_argument('--host-profile', choices=HOST_PROFILES, default=HOST_PROFILES[0], help='setup method. CivSwitch is experimental. Seedsow installation uses its own GUI')
    p.add_argument('--check', action='store_true', help='check files on this computer without downloading, building or preparing them')
    p.add_argument('--sources-only', action='store_true', help='download and verify only the selected upstream source files')
    p.add_argument('--offline', action='store_true', help='use local files only')
    p.add_argument('--build', action='store_true', help='build the backend, app and resident in a separate staging directory using the matching SDK')
    p.add_argument('--node', help='Node.js executable (default: node). Local checks accept 22+. Use Node 24 for installation')
    p.add_argument('--release-manifest', type=pathlib.Path)
    p.add_argument('--kaihen-archive', type=pathlib.Path)
    p.add_argument('--engine-archive', type=pathlib.Path, help='offline archive for the selected pinned host profile')
    p.add_argument('--programs-archive', type=pathlib.Path)
    p.add_argument('--programs-sha256')
    p.add_argument('--hen-binary', type=pathlib.Path)
    p.add_argument('--bootstrap-binary', type=pathlib.Path, help='pinned bootstrap paired with patched HEN')
    p.add_argument('--ro-binary', type=pathlib.Path, help='schema-3 pinned RO with FS proof and HEN frontend')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    result = setup(args); print(json.dumps(result, indent=2)); return result


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        raise SystemExit('Backend setup stopped: ' + str(error))
