#!/usr/bin/env python3
"""Package the recorded prerelease from committed source and pinned binaries."""
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
    if proof.get('schema') == 2:
        item = spec.get('patched_bootstrap')
        require(isinstance(item, dict) and item.get('source') == 'vendor/kaihen-mo/bootstrap.bin', 'Paired patched bootstrap descriptor is required')
        bootstrap = binary(root, item, b'')
        backend.qualify_patched(bootstrap, 'bootstrap.bin', proof)
    else:
        require('patched_bootstrap' not in spec, 'Legacy release cannot declare an unqualified patched bootstrap')
    return hen, bootstrap, proof


def qualify_exported_native_source(source, proof):
    if proof.get('schema') != 2:
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
    host[prefix + 'dist/Moader.nro'] = (app, 0o100644)
    host[prefix + 'dist/moader.nso'] = (service, 0o100644)
    host[prefix + 'START_HERE.txt'] = ((
        'Moader backend setup kit ' + tag + '\n\n'
        'Start with README.md and docs/INSTALLATION.md in this folder.\n'
        'Keep this folder on your computer; do not copy the whole kit to the SD.\n'
        'For ordinary SD installation, use the separate Moader app ZIP.\n\n'
        'The kit includes the matched Moader app/service and pinned patched HEN.\n'
        + ('The patched bootstrap is included as the required matched HEN pair.\n' if bootstrap is not None else '') +
        'See docs/VALIDATION.md for host checks and console-test coverage.\n'
        'Preparation fetches pinned official HEN components and upstream host source.\n'
        'It needs Node 24, Python 3.10+ and internet access, not a compiler/SDK.\n'
        'For SmileBASIC, use kurumuz ns2jb on Linux/WSL, or ns2jb/mac2jb on macOS.\n'
        'The experimental CivSwitch profile is documented separately in docs/CIV7.md.\n'
        'It never acquires FS. Follow the guide with a fresh verified held FS session.\n\n'
        'Corresponding source: https://github.com/' + spec['repository'] + '/tree/' + tag + '\n'
    ).encode(), 0o100644)
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
