#!/usr/bin/env python3
"""Package the recorded release from committed source and pinned binaries."""
from pathlib import Path, PurePosixPath
import argparse
import datetime
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PRIVATE_TOP = {
    'state', 'evidence', 'coordination', 'research', 'vendor', 'build', 'dist',
    'docs-site', 'output', '.impeccable', '.playwright-cli', '.git',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def binary(root, item, magic, offset=0):
    path = root / item['source']
    require(path.is_file() and not path.is_symlink(), 'Missing regular release binary: ' + item['source'])
    require(path.resolve().is_relative_to(root), 'Release binary leaves the project')
    data = path.read_bytes()
    require(len(data) == item['bytes'] and sha(data) == item['sha256'],
            'Binary differs from config/release.json: ' + item['source'])
    require(data[offset:offset + len(magic)] == magic, 'Invalid release binary format')
    return data


def native_pair(root, spec):
    hen = binary(root, spec['patched_hen'], b'NSO0')
    backend_spec = importlib.util.spec_from_file_location('moader_release_backend', root / 'tools/setup-backend.py')
    backend = importlib.util.module_from_spec(backend_spec)
    backend_spec.loader.exec_module(backend)
    proof = backend.proof(root)
    backend.qualify_patched(hen, 'hen/main', proof)
    require(spec['patched_hen']['source'] == 'vendor/kaihen-mo/hen/main', 'Unexpected patched HEN release source')
    bootstrap = None
    if proof.get('schema') in (2, 3):
        item = spec.get('patched_bootstrap')
        require(isinstance(item, dict) and item.get('source') == 'vendor/kaihen-mo/bootstrap.bin', 'Paired patched bootstrap descriptor is required')
        bootstrap = binary(root, item, b'')
        backend.qualify_patched(bootstrap, 'bootstrap.bin', proof)
        if proof['schema'] == 3:
            item = spec.get('patched_ro')
            require(isinstance(item, dict) and item.get('source') == 'vendor/kaihen-mo/ro/main', 'Paired patched RO descriptor is required')
            backend.qualify_patched(binary(root, item, b'NSO0'), 'ro/main', proof)
        else:
            require('patched_ro' not in spec, 'Schema-2 release cannot declare an unqualified patched RO')
    else:
        require('patched_bootstrap' not in spec and 'patched_ro' not in spec,
                'Legacy release cannot declare unqualified patched components')
    return hen, bootstrap, proof


def qualify_exported_native_source(source, proof):
    if proof.get('schema') not in (2, 3):
        return
    files = {}
    for name, (data, _) in source.items():
        if name.startswith('runtime/kaihen/'):
            relative = name[len('runtime/kaihen/'):]
            if any(part in ('build', '.git', '__pycache__') for part in PurePosixPath(relative).parts) or PurePosixPath(relative).name == '.DS_Store':
                continue
            files[relative] = data
    digest = sha(''.join(name + '\0' + sha(data) + '\n' for name, data in sorted(files.items())).encode())
    require(digest == proof['source_tree_sha256'], 'Committed release omits or changes pinned native source')


def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root)


def public_source(root):
    require(subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--'], cwd=root).returncode == 0,
            'Commit tracked source changes before packaging a release')
    require(subprocess.run(['git', 'diff', '--cached', '--quiet', '--'], cwd=root).returncode == 0,
            'Commit staged changes before packaging a release')
    commit = git(root, 'rev-parse', 'HEAD').decode().strip()
    require(re.fullmatch(r'[a-f0-9]{40}', commit), 'Invalid source commit')
    entries = {}
    for raw in git(root, 'ls-tree', '-rz', '--full-tree', 'HEAD').split(b'\0'):
        if not raw:
            continue
        metadata, name = raw.split(b'\t', 1)
        mode, kind, _ = metadata.decode().split()
        name = name.decode('utf8')
        path = PurePosixPath(name)
        require(kind == 'blob' and mode in {'100644', '100755'}, 'Unsupported source tree entry: ' + name)
        require(not path.is_absolute() and '..' not in path.parts and path.parts[0] not in PRIVATE_TOP,
                'Private/generated path in committed tree: ' + name)
        require(name not in {'GOAL.md', 'PRODUCT.md'} and not name.startswith('native/review/'),
                'Internal development record in committed tree: ' + name)
        if name.startswith('software-builds/'):
            require(name in {'software-builds/kaihen-mo.json', 'software-builds/kaihen-mo.patch'},
                    'Unapproved build record in committed tree: ' + name)
        entries[name] = (git(root, 'show', 'HEAD:' + name), int(mode, 8))
    for required in ['README.md', 'LICENSE', 'docs/INSTALLATION.md', 'tools/setup-backend.py',
                     'tools/install-from-rpc.py', 'tools/install-moader.cjs', 'config/release.json',
                     'runtime/modloader/native/prebuilt/manifest.json']:
        require(required in entries, 'Release source is missing ' + required)
    # Validate only the committed export, never the private development tree.
    import tempfile
    with tempfile.TemporaryDirectory(prefix='moader-public-release-') as temporary:
        exported = Path(temporary)
        for name, (data, _) in entries.items():
            target = exported / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        subprocess.run([sys.executable, str(exported / 'tools/check-public-tree.py'), str(exported)], check=True)
    return commit, entries


def write_zip(target, entries, timestamp):
    require(not target.exists(), 'Release output already exists: ' + target.name)
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(entries):
            data, mode = entries[name]
            info = zipfile.ZipInfo(name, timestamp)
            info.create_system = 3
            info.external_attr = mode << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    with zipfile.ZipFile(target) as archive:
        require(archive.testzip() is None, 'Release ZIP failed CRC validation')
        require(set(archive.namelist()) == set(entries), 'Release ZIP file list differs')
        for name, (data, _) in entries.items():
            require(archive.read(name) == data, 'Release ZIP bytes differ: ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/releases')
    args = parser.parse_args()
    root = ROOT.resolve()
    spec = json.loads((root / 'config/release.json').read_text())
    tag = spec['tag']
    require(re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?', tag), 'Invalid release tag')
    app = binary(root, spec['binaries']['Moader.nro'], b'NRO0', 16)
    service = binary(root, spec['binaries']['moader.nso'], b'NSO0')
    hen, bootstrap, proof = native_pair(root, spec)
    ro = binary(root, spec['patched_ro'], b'NSO0') if proof.get('schema') == 3 else None
    commit, source = public_source(root)
    qualify_exported_native_source(source, proof)
    timestamp = datetime.datetime.fromtimestamp(int(git(root, 'show', '-s', '--format=%ct', 'HEAD')), datetime.timezone.utc).timetuple()[:6]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    names = [f'Moader-{tag}.zip', f'Moader-backend-{tag}.zip', 'Moader.nro', 'moader.nso',
             'release-manifest.json', 'SHA256SUMS.txt']
    require(not any((output / name).exists() for name in names), 'Use an empty release output directory')
    # The existing app packager supplies its user instructions and notices.
    subprocess.run([sys.executable, str(root / 'tools/package-homebrew.py')], cwd=root, check=True)
    with zipfile.ZipFile(root / 'dist/Moader-homebrew.zip') as packaged:
        app_files = {name: (packaged.read(name), 0o100644) for name in packaged.namelist()}
    write_zip(output / names[0], app_files, timestamp)
    prefix = f'Moader-backend-{tag}/'
    host = {prefix + name: value for name, value in source.items()}
    host[prefix + 'vendor/kaihen-mo/hen/main'] = (hen, 0o100644)
    if bootstrap is not None:
        host[prefix + 'vendor/kaihen-mo/bootstrap.bin'] = (bootstrap, 0o100644)
    if ro is not None:
        host[prefix + 'vendor/kaihen-mo/ro/main'] = (ro, 0o100644)
    host[prefix + 'dist/Moader.nro'] = (app, 0o100644)
    host[prefix + 'dist/moader.nso'] = (service, 0o100644)
    start_here = f'''Moader backend kit {tag}

Use this kit to set up the backend for the current Switch boot. Start by
opening docs/INSTALLATION.md and choosing a method:

- SmileBASIC with mac2jb: macOS and mac2jb's bundled Node 24.
- SmileBASIC with direct ns2jb: macOS, Linux or WSL2 and native Node 24.
- Seedsow Lullaby Demo or full game: macOS, Linux or WSL2. See docs/SEEDSOW.md.
- CivSwitch: experimental. See docs/CIV7.md for the required profile.

The kit includes the Moader app, resident service and matching backend
binaries. Install Python 3.10 or newer. The first preparation downloads
pinned dependencies, so have internet access. SmileBASIC setup uses prebuilt
files without an SDK. The Seedsow RO, HEN and bootstrap binaries are included
as one source-qualified set. The Seedsow guide provisions the pinned patched
GUI from upstream and installs the SDK needed by its live helper.

Run preparation from this outer folder (the one containing tools/).
For SmileBASIC and CivSwitch, preparation checks local files and prints
one installation command. For Seedsow, the pinned GUI performs the one
owned installation after its local preparation. Follow the completion
steps for your method in docs/INSTALLATION.md, docs/SEEDSOW.md or
docs/CIV7.md.

Mod layout and controls: docs/USAGE.md
Test results: docs/VALIDATION.md
Corresponding source: https://github.com/{spec['repository']}/tree/{commit}
'''
    host[prefix + 'START_HERE.txt'] = (start_here.encode(), 0o100644)
    write_zip(output / names[1], host, timestamp)
    (output / 'Moader.nro').write_bytes(app)
    (output / 'moader.nso').write_bytes(service)
    records = {name: {'bytes': (output / name).stat().st_size,
                      'sha256': sha((output / name).read_bytes())} for name in names[:4]}
    (output / 'release-manifest.json').write_text(json.dumps({
        'schema': 1, 'version': spec['version'], 'tag': tag, 'prerelease': spec['prerelease'],
        'repository': spec['repository'], 'sourceCommit': commit,
        'appAndResidentMatchRecordedManifest': True,
        'appAndResidentMatchTestedManifest': spec['appAndResidentHardwareTested'],
        'appAndResidentHardwareTested': spec['appAndResidentHardwareTested'],
        'patchedHenSha256': sha(hen),
        **({'patchedBootstrapSha256': sha(bootstrap), 'kaihenProtocolVersion': proof['protocol_version']} if bootstrap is not None else {}),
        **({'patchedRoSha256': sha(ro)} if ro is not None else {}),
        'kaihenBuildProofSchema': proof.get('schema', 1),
        'kaihenConsoleRetestPassed': proof.get('console_retest_passed', False),
        'upstreamHostSourceBundled': False, 'officialHenArchiveBundled': False,
        'assets': records, 'validation': 'docs/VALIDATION.md',
    }, indent=2) + '\n')
    (output / 'SHA256SUMS.txt').write_text(''.join(
        sha((output / name).read_bytes()) + '  ' + name + '\n' for name in names[:5]))
    print('Packaged and verified ' + tag + ' from source commit ' + commit)
    print('Release assets: ' + str(output))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('Release packaging stopped: ' + str(error))
