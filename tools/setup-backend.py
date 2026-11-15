#!/usr/bin/env python3
"""Provision pinned local backend inputs; never acquire or contact a console."""
from __future__ import annotations
import argparse, hashlib, io, json, os, pathlib, platform, shutil, stat, subprocess, tarfile, tempfile, urllib.parse, urllib.request, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
NS2JB_REV = '28f03b5895e5569d8fbc35cbed84906b8f0a65b2'
ENGINE_TREE_SHA = '7003335a2a426a82156c19653be5e61506a05dd02b5d0490c8bcb4516c5c354a'
ENGINE_URL = f'https://gitlab.com/kurumuz/ns2jb/-/archive/{NS2JB_REV}/ns2jb-{NS2JB_REV}.tar.gz'
CIV7_REV = '51a804cbce09216e1a42e65929644fd436b4c8bf'
CIV7_TREE_SHA = 'd83cadc568b65a724f32b848f1681540487a798b354ac3286f71e41e94a9ac75'
CIV7_URL = f'https://github.com/s8n/civ7-fs-kaihen/archive/{CIV7_REV}.tar.gz'
HOST_PROFILES = ('smilebasic-kurumuz', 'civ7-s8n')
KAIHEN_URL = 'https://github.com/ouncebrew/kaihen/releases/download/20261114c/kaiHEN-20261114c.zip'
KAIHEN_SHA = '63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967'
HEN_SHA = 'e6341b1932b450e25f04ccf425c3034bd45dd3cccfadbf5e5fb2778b6a673e5c'
PROGRAM_ENTRIES = {'dist/Moader.nro': 'switch/Moader.nro', 'dist/moader.nso': 'kaihen/plugins/moader.nso'}
KAIHEN_COMPONENTS = ('ro/main', 'hen/main', 'game/main', 'bootstrap.bin', 'hbmenu.nro')
MAX_ARCHIVE, MAX_FILE, MAX_TOTAL, MAX_ENTRIES = 64 << 20, 32 << 20, 96 << 20, 4096


def require(ok, message):
    if not ok:
        raise ValueError(message)


def source_profile(name='smilebasic-kurumuz'):
    require(name in HOST_PROFILES, 'Unknown host profile; choose smilebasic-kurumuz or civ7-s8n')
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
    prefixes = ((f'civ7-fs-kaihen-{CIV7_REV}/', '') if host_profile == 'civ7-s8n' else
                (f'ns2jb-{NS2JB_REV}/engine/', 'engine/', 'research/sources/ns2jb/engine/'))
    if zipfile.is_zipfile(archive):
        entries = zip_files(archive)
    else:
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
            require(relative not in selected, 'Duplicate pinned engine path')
            selected[relative] = data
    require(selected and tree_sha(selected) == profile['sha256'], 'Pinned ' + profile['label'] + ' content differs; no fallback to another revision')
    return selected


def profile_files(root, profile):
    directory = safe_destination(root, profile['directory'])
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
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'Moader-setup/0.1.0-dev'}), timeout=60) as reply:
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


def proof(root):
    p = json.loads(regular(root / 'software-builds/kaihen-mo.json').read_text())
    require(p.get('hen_sha256') == HEN_SHA, 'Public tested-HEN proof/hash differs')
    regular(root / 'software-builds/kaihen-mo.patch')
    return p


def sdk(root):
    require(platform.system() == 'Darwin' and platform.machine().lower() in ('arm64', 'aarch64'), '--build currently requires Apple Silicon macOS; no Linux/Intel SDK is provisioned')
    result = root / 'vendor/devkitpro'
    for name in ('devkitA64/bin/aarch64-none-elf-g++', 'tools/bin/elf2nso', 'libounce/switch2_cpu.mk', 'libounce/lib/libounce_core.a', 'portlibs/switch2/lib/libkaihen_plugin.a'):
        regular(result / name)
    return result.resolve()


def build_inputs(root, work):
    devkit = sdk(root)
    source = work / 'source-build'; source.mkdir()
    # Source allowlist: no research, state, dist, build, .git or device files.
    for name in ('include', 'source', 'native', 'branding', 'web', 'tests', 'runtime/kaihen'):
        shutil.copytree(root / name, source / name, ignore=shutil.ignore_patterns('build', '.git', '__pycache__', 'review', '.DS_Store'))
    for name in ('Makefile', 'tools/embed-web.py'):
        target = source / name; target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(root / name, target)
    subprocess.run(['make', '-C', str(source / 'runtime/kaihen/hen'), '-j4', 'DEVKITPRO=' + str(devkit), 'LIBOUNCE=' + str(devkit / 'libounce')], check=True)
    hen = (source / 'runtime/kaihen/hen/build/main').read_bytes()
    require(digest(hen) == HEN_SHA, 'Rebuilt HEN is not byte-identical to tested HEN; existing binary was not touched')
    subprocess.run(['make', '-C', str(source), '-j4', 'DEVKITPRO=' + str(devkit)], check=True)
    result = {name: regular(source / name).read_bytes() for name in PROGRAM_ENTRIES}
    validate_programs(result); return hen, result


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
    proof(root)
    archive = checked_archive(root / 'research/artifacts/kaiHEN-20261114c.zip', KAIHEN_SHA)
    shipped = zip_files(archive)
    engine = profile_files(root, profile)
    require(tree_sha(engine) == profile['sha256'], 'Local engine is not the pinned source tree')
    for name in KAIHEN_COMPONENTS:
        require(name in shipped, 'Incomplete official kaiHEN archive: ' + name)
        data = shipped[name]
        actual = regular(root / 'vendor/kaihen-mo' / name).read_bytes()
        require(digest(actual) == HEN_SHA if name == 'hen/main' else actual == data, 'Local kaiHEN component differs: ' + name)
    bundled = bundled_programs(root)
    built = {name: regular(root / name).read_bytes() for name in PROGRAM_ENTRIES}; validate_programs(built)
    version = node_check(root, node)
    return {'readyForInstallerCheck': True, 'consoleConnected': False, 'node': version, 'hostProfile': profile['id'], 'engineRevision': profile['revision'],
            'engineTreeSha256': profile['sha256'], 'henSha256': HEN_SHA, 'bundledPrograms': sorted(bundled),
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
            archive = args.engine_archive or download(profile['url'], pathlib.Path(temporary) / 'engine.tar.gz', offline=args.offline)
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
        conflicts = [name.replace('_', '-') for name in ('check', 'build', 'node', 'release_manifest', 'kaihen_archive', 'programs_archive', 'programs_sha256', 'hen_binary') if getattr(args, name) not in (None, False)]
        require(not conflicts, '--sources-only accepts only --root, --host-profile, --offline and --engine-archive; conflicting options: ' + ', '.join('--' + name for name in conflicts))
        return setup_sources(args.root.resolve(), args)
    root = args.root.resolve(); proof(root)
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
        require(all(name in shipped for name in KAIHEN_COMPONENTS), 'Incomplete official kaiHEN archive')
        bundled_programs(root, allow_missing=True)
        if engine_target.exists():
            engine = profile_files(root, profile)
        else:
            engine_archive = args.engine_archive or download(profile['url'], work / 'engine.tar.gz', offline=args.offline)
            engine = engine_files(engine_archive, args.host_profile)
        hen, app = None, None
        if args.build:
            hen, app = build_inputs(root, work)
        if hen is None:
            if args.hen_binary:
                hen = regular(args.hen_binary).read_bytes()
            elif (root / 'vendor/kaihen-mo/hen/main').exists():
                hen = regular(root / 'vendor/kaihen-mo/hen/main').read_bytes()
            elif manifest.get('patched_hen', {}).get('url'):
                hen = download(manifest['patched_hen']['url'], work / 'patched-hen.nso', HEN_SHA, args.offline).read_bytes()
            else:
                raise ValueError('Missing patched HEN: use the pre-populated host kit, --hen-binary, or explicit --build with the installed Apple Silicon SDK')
        require(digest(hen) == HEN_SHA, 'Patched HEN does not match tested bytes; refusing replacement')
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
        # Optional programs such as OunceShell are deliberately not extracted.
        files.update({'vendor/kaihen-mo/' + name: hen if name == 'hen/main' else shipped[name] for name in KAIHEN_COMPONENTS})
        written = publish_new(root, files, directories=('vendor/kaihen-mo/programs',))
    result = check_root(root, args.node or 'node', args.host_profile); result['newFiles'] = written; return result


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=pathlib.Path, default=ROOT, help='isolated public checkout/host-kit root')
    p.add_argument('--host-profile', choices=HOST_PROFILES, default=HOST_PROFILES[0], help='pinned held-session consumer; Civ7 is experimental and hardware untested')
    p.add_argument('--check', action='store_true', help='read-only local validation; no downloads/builds/preparation')
    p.add_argument('--sources-only', action='store_true', help='fetch/validate only selected host sources; no acquisition/HEN/app/SDK/Node work')
    p.add_argument('--offline', action='store_true', help='refuse downloads; use supplied/host-kit inputs')
    p.add_argument('--build', action='store_true', help='explicit isolated HEN/app rebuild with existing Apple Silicon SDK')
    p.add_argument('--node', help='Node executable (default: node); pass mac2jb Node 24 path when appropriate')
    p.add_argument('--release-manifest', type=pathlib.Path)
    p.add_argument('--kaihen-archive', type=pathlib.Path)
    p.add_argument('--engine-archive', type=pathlib.Path, help='offline archive for the selected pinned host profile')
    p.add_argument('--programs-archive', type=pathlib.Path)
    p.add_argument('--programs-sha256')
    p.add_argument('--hen-binary', type=pathlib.Path)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    result = setup(args); print(json.dumps(result, indent=2)); return result


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        raise SystemExit('Backend setup stopped: ' + str(error))
