#!/usr/bin/env python3
"""Package the recorded release from committed source and pinned binaries."""
from pathlib import Path, PurePosixPath
from contextlib import ExitStack
import argparse
import datetime
import hashlib
import importlib.util
import io
import json
import posixpath
import re
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent
PRIVATE_TOP = {
    'state', 'evidence', 'coordination', 'research', 'vendor', 'build', 'dist',
    'docs-site', 'output', '.impeccable', '.playwright-cli', '.git',
}
HOST_NODE_VERSION = '24.22.3'
HOST_NODE_ARCHIVES = {
    'darwin-arm64': '1d64136c8a43d648d1b3bad10797a4c995b1ca032d2bde8799de43b37f058fe5',
    'darwin-x64': '4daeec962855c1a6e66cda398a25037319dc1bbd4a0939188b3547afbbfb6490',
    'linux-arm64': '6e51a2ada00f0011cc266f65d98922b403d7ad88f809ca2aa615a5fe1ce6e86c',
    'linux-x64': '4f2b175a6d53a5c617a80dba2676238a0688cef28c6da4eba35c7d844e66fcfb',
}
HOST_UV_VERSION = '0.12.16'
HOST_UV_ARCHIVES = {
    'darwin-arm64': ('aarch64-apple-darwin', 'b6e03fae61704b1aa622f12b792a69483e837b83068e44f4fd34f8a07a8f74a3'),
    'darwin-x64': ('x86_64-apple-darwin', 'a42bcc9ce97eb8b364d7f162233a9c6b8c0ee25388e551d362809795127e0c31'),
    'linux-arm64': ('aarch64-unknown-linux-gnu', '36d913ee9c647481d64f1a0a0485f85ff2feaee605c341fc22e73398f9212c26'),
    'linux-x64': ('x86_64-unknown-linux-gnu', '8e5c6e5523dffc2dcf615bd995554c84c9feb4e577808a3fb8698a639d3f8d9c'),
}
HOST_ELECTRON_VERSION = '45.4.1'
HOST_ELECTRON_ARCHIVES = {
    'darwin-arm64': 'd080c3b05a01bd16137f2c12ab9ebf083c577757fe846844ba325bafa8e8bdf4',
    'darwin-x64': '1c80d2ccf71b02c8fd9940fd09cafe88090a48c1bab0193c79b597b08d890e79',
    'linux-arm64': '3da04c450fa682e3b80961e92a918a1c2a1a10796527a7e63dec7b8e466b6334',
    'linux-x64': 'e1d077c7a087d7e344350ea0c5cd2b7dc5af3d9bf7979474f38e0fc1a560b875',
    'win32-x64': '2ba6223afa9121f42831fefe92c434334907540165294fc56697449d12e7cfe9',
}


class ArchiveMember(NamedTuple):
    archive: Path
    name: str


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
                     'runtime/modloader/native/prebuilt/manifest.json',
                     'Moader Setup', 'Moader Setup Windows.cmd',
                     'Moader Setup.app/Contents/MacOS/Moader Setup',
                     'Moader Setup.app/Contents/Info.plist',
                     'setup/launch.sh', 'setup/windows-launch.ps1', 'setup/wsl-entry.sh',
                     'setup/controller.cjs', 'setup/host-policy.cjs',
                     'setup/host-tools.cjs',
                     'setup/desktop-app/package.json', 'setup/desktop-app/main.cjs',
                     'setup/bridge.cjs', 'setup/export-sd.py',
                     'setup/ui/index.html', 'setup/ui/style.css', 'setup/ui/app.js',
                     'setup/licenses/uv-LICENSE-APACHE.txt',
                     'setup/licenses/uv-LICENSE-MIT.txt', 'docs/SETUP_GUI.md']:
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
    with ExitStack() as stack:
        source_archives = {}
        def entry_bytes(data):
            if isinstance(data, ArchiveMember):
                if data.archive not in source_archives:
                    source_archives[data.archive] = stack.enter_context(zipfile.ZipFile(data.archive))
                return source_archives[data.archive].read(data.name)
            return data
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name in sorted(entries):
                data, mode = entries[name]
                info = zipfile.ZipInfo(name, timestamp)
                info.create_system = 3
                info.external_attr = mode << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, entry_bytes(data))
        with zipfile.ZipFile(target) as archive:
            require(archive.testzip() is None, 'Release ZIP failed CRC validation')
            require(set(archive.namelist()) == set(entries), 'Release ZIP file list differs')
            for name, (data, _) in entries.items():
                require(archive.read(name) == entry_bytes(data),
                        'Release ZIP bytes differ: ' + name)


def host_electron_runtimes(root):
    """Keep every official Electron desktop runtime tied to its verified release ZIP."""
    cache = root / 'build/host-electron'
    cache.mkdir(parents=True, exist_ok=True)
    output = {}
    for platform, expected in HOST_ELECTRON_ARCHIVES.items():
        name = f'electron-v{HOST_ELECTRON_VERSION}-{platform}.zip'
        file = cache / name
        if not file.is_file() or sha(file.read_bytes()) != expected:
            require(not file.exists(), 'Cached Electron archive has the wrong checksum: ' + name)
            with urllib.request.urlopen(urllib.request.Request(
                f'https://github.com/electron/electron/releases/download/v{HOST_ELECTRON_VERSION}/{name}',
                headers={'User-Agent': 'Moader release packager'}), timeout=120) as response:
                data = response.read(170 * 1024 * 1024 + 1)
            require(len(data) <= 170 * 1024 * 1024 and sha(data) == expected,
                    'Pinned Electron archive checksum differs: ' + name)
            temporary = file.with_suffix('.zip.tmp')
            temporary.write_bytes(data)
            temporary.replace(file)
        with zipfile.ZipFile(file) as archive:
            require(archive.testzip() is None, 'Pinned Electron archive failed CRC validation: ' + name)
            names = set(archive.namelist())
            require({'LICENSE', 'LICENSES.chromium.html'} <= names,
                    'Pinned Electron notices are missing: ' + name)
            total = 0
            for item in archive.infolist():
                if item.is_dir():
                    continue
                member = PurePosixPath(item.filename)
                require(not member.is_absolute() and '..' not in member.parts and
                        item.file_size <= 400 * 1024 * 1024,
                        'Unexpected Electron archive member: ' + item.filename)
                total += item.file_size
                require(total <= 500 * 1024 * 1024,
                        'Pinned Electron archive has unexpected expanded size: ' + name)
                mode = item.external_attr >> 16
                kind = mode & 0o170000
                require(kind in (0o100000, 0o120000),
                        'Unexpected Electron archive member type: ' + item.filename)
                if kind == 0o120000:
                    target = archive.read(item).decode('utf8')
                    combined = posixpath.normpath(posixpath.join(posixpath.dirname(item.filename), target))
                    require(not target.startswith('/') and not combined.startswith('../') and
                            combined != '..', 'Electron archive symlink leaves its runtime')
                dest = item.filename
                if platform == 'win32-x64':
                    if dest == 'electron.exe': dest = 'Moader Setup.exe'
                    if dest == 'electron_wer.dll': dest = 'Moader Setup_wer.dll'
                    dest = 'Moader Setup Windows/' + dest
                else:
                    dest = f'host-runtimes/electron-v{HOST_ELECTRON_VERSION}/{platform}/' + dest
                require(dest not in output, 'Duplicate Electron runtime member: ' + dest)
                output[dest] = (ArchiveMember(file, item.filename), mode)
            program = 'Moader Setup Windows/Moader Setup.exe' if platform == 'win32-x64' else (
                f'host-runtimes/electron-v{HOST_ELECTRON_VERSION}/{platform}/' +
                ('Electron.app/Contents/MacOS/Electron' if platform.startswith('darwin') else 'electron'))
            require(program in output, 'Pinned Electron runtime executable is missing: ' + name)
    output['Moader Setup Windows/resources/app/package.json'] = (
        (root / 'setup/desktop-app/package.json').read_bytes(), 0o100644)
    output['Moader Setup Windows/resources/app/main.cjs'] = (
        b"// SPDX-License-Identifier: GPL-2.0-only\nrequire('../../../setup/desktop-app/main.cjs');\n", 0o100644)
    return output


def host_node_runtimes(root):
    """Bundle only the Node executable and its upstream license from pinned archives."""
    cache = root / 'build/host-node'
    cache.mkdir(parents=True, exist_ok=True)
    output = {}
    license_bytes = None
    for platform, expected in HOST_NODE_ARCHIVES.items():
        name = f'node-v{HOST_NODE_VERSION}-{platform}.tar.xz'
        file = cache / name
        if not file.is_file() or sha(file.read_bytes()) != expected:
            require(not file.exists(), 'Cached Node archive has the wrong checksum: ' + name)
            with urllib.request.urlopen(
                urllib.request.Request(
                    f'https://nodejs.org/download/release/v{HOST_NODE_VERSION}/{name}',
                    headers={'User-Agent': 'Moader release packager'}), timeout=120
            ) as response:
                data = response.read(80 * 1024 * 1024 + 1)
            require(len(data) <= 80 * 1024 * 1024 and sha(data) == expected,
                    'Pinned Node archive checksum differs: ' + name)
            temporary = file.with_suffix(file.suffix + '.tmp')
            temporary.write_bytes(data)
            temporary.replace(file)
        with tarfile.open(file, 'r:xz') as archive:
            prefix = name[:-7] + '/'
            binary_member = archive.getmember(prefix + 'bin/node')
            license_member = archive.getmember(prefix + 'LICENSE')
            require(binary_member.isfile() and binary_member.size <= 130 * 1024 * 1024 and
                    license_member.isfile() and license_member.size <= 2 * 1024 * 1024,
                    'Pinned Node archive has unexpected executable or license')
            binary = archive.extractfile(binary_member).read()
            notice = archive.extractfile(license_member).read()
            require(binary[:4] in (b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'),
                    'Pinned Node executable format differs')
            if license_bytes is None:
                license_bytes = notice
            else:
                require(notice == license_bytes, 'Node license differs across host archives')
            output[f'host-runtimes/node-v{HOST_NODE_VERSION}/{platform}/node'] = (
                binary, 0o100755)
    output[f'host-runtimes/node-v{HOST_NODE_VERSION}/LICENSE'] = (license_bytes, 0o100644)
    return output


def host_uv_runtimes(root):
    cache = root / 'build/host-uv'
    cache.mkdir(parents=True, exist_ok=True)
    output = {}
    for platform, (triple, expected) in HOST_UV_ARCHIVES.items():
        name = f'uv-{triple}.tar.gz'
        file = cache / name
        if not file.is_file() or sha(file.read_bytes()) != expected:
            require(not file.exists(), 'Cached uv archive has the wrong checksum: ' + name)
            with urllib.request.urlopen(
                urllib.request.Request(
                    f'https://github.com/astral-sh/uv/releases/download/{HOST_UV_VERSION}/{name}',
                    headers={'User-Agent': 'Moader release packager'}), timeout=120
            ) as response:
                data = response.read(40 * 1024 * 1024 + 1)
            require(len(data) <= 40 * 1024 * 1024 and sha(data) == expected,
                    'Pinned uv archive checksum differs: ' + name)
            temporary = file.with_suffix(file.suffix + '.tmp')
            temporary.write_bytes(data)
            temporary.replace(file)
        with tarfile.open(file, 'r:gz') as archive:
            candidates = [member for member in archive.getmembers()
                          if member.isfile() and member.name.split('/')[-1] == 'uv']
            require(len(candidates) == 1 and candidates[0].size <= 70 * 1024 * 1024,
                    'Pinned uv archive has an unexpected executable layout')
            binary = archive.extractfile(candidates[0]).read()
            require(binary[:4] in (b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'),
                    'Pinned uv executable format differs')
            output[f'host-runtimes/uv-v{HOST_UV_VERSION}/{platform}/uv'] = (
                binary, 0o100755)
    return output


def full_kaihen_bundle(root, app, service, ro, hen, bootstrap, timestamp):
    backend_spec = importlib.util.spec_from_file_location('moader_bundle_backend', root / 'tools/setup-backend.py')
    backend = importlib.util.module_from_spec(backend_spec)
    backend_spec.loader.exec_module(backend)
    original = root / 'research/artifacts/kaiHEN-20261114c.zip'
    require(sha(original.read_bytes()) == backend.KAIHEN_SHA, 'Official kaiHEN base checksum differs')
    base = backend.zip_files(original)
    require(all(name in base for name in (*backend.KAIHEN_COMPONENTS, *backend.KAIHEN_PROGRAMS)) and
            any(name.startswith('licenses/') for name in base), 'Official kaiHEN release is incomplete')
    patched = dict(base)
    patched.update({'ro/main': ro if ro is not None else base['ro/main'],
                    'hen/main': hen, 'bootstrap.bin': bootstrap,
                    'programs/Moader.nro': app, 'plugins/moader.nso': service})
    require(all(patched[name] == base[name] for name in base if name not in
                {'ro/main', 'hen/main', 'bootstrap.bin'}),
            'An unpatched kaiHEN release member changed')
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(patched.items()):
            info = zipfile.ZipInfo(name, timestamp)
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    with zipfile.ZipFile(io.BytesIO(output.getvalue())) as archive:
        require(archive.testzip() is None and set(archive.namelist()) == set(patched),
                'Complete patched kaiHEN ZIP failed verification')
        for name, data in patched.items():
            require(archive.read(name) == data, 'Patched kaiHEN ZIP member differs: ' + name)
    return output.getvalue(), len(patched)


def standalone_prebuilt(root, spec, source):
    """Include only the source-built bootstrap and its public build receipt."""
    selected = spec.get('standalone')
    if selected is None:
        return None
    require(selected.get('bootstrap') == {
        'source': 'build/standalone-bootstrap/bootstrap.bin',
        'bytes': 30080,
        'sha256': 'f3f4305d53d5e3da9dac4b054acdcdc3c2addb436d813b2e08c2a9f32f1733ea',
    }, 'Standalone bootstrap descriptor differs from the qualified candidate')
    require(selected.get('adapterSha256') == sha(source['standalone/s2_moader.lua'][0]),
            'Committed standalone adapter differs from the release descriptor')
    image = binary(root, selected['bootstrap'], b'')
    proof_path = root / 'build/standalone-bootstrap/proof.json'
    require(proof_path.is_file() and not proof_path.is_symlink(),
            'Missing regular standalone bootstrap build receipt')
    receipt = proof_path.read_bytes()
    details = json.loads(receipt)
    require(details.get('releaseBootstrapSha256') == spec['patched_bootstrap']['sha256'] and
            details.get('standaloneBootstrapSha256') == sha(image) and
            details.get('adapterSha256') == selected['adapterSha256'] and
            details.get('bytes') == len(image),
            'Standalone bootstrap build receipt differs from the matched release')
    return image, receipt


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
    standalone = standalone_prebuilt(root, spec, source)
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
    for name, value in host_node_runtimes(root).items():
        host[prefix + name] = value
    for name, value in host_uv_runtimes(root).items():
        host[prefix + name] = value
    for name, value in host_electron_runtimes(root).items():
        host[prefix + name] = value
    host[prefix + 'vendor/kaihen-mo/hen/main'] = (hen, 0o100644)
    if bootstrap is not None:
        host[prefix + 'vendor/kaihen-mo/bootstrap.bin'] = (bootstrap, 0o100644)
    if ro is not None:
        host[prefix + 'vendor/kaihen-mo/ro/main'] = (ro, 0o100644)
    host[prefix + 'dist/Moader.nro'] = (app, 0o100644)
    host[prefix + 'dist/moader.nso'] = (service, 0o100644)
    full_kaihen, full_count = full_kaihen_bundle(root, app, service, ro, hen, bootstrap, timestamp)
    host[prefix + 'research/artifacts/kaiHEN-20261114c.zip'] = (
        (root / 'research/artifacts/kaiHEN-20261114c.zip').read_bytes(), 0o100644)
    host[prefix + 'bundles/kaiHEN-Moader-20261114c.zip'] = (full_kaihen, 0o100644)
    if standalone is not None:
        host[prefix + 'prebuilt/standalone-bootstrap/bootstrap.bin'] = (standalone[0], 0o100644)
        host[prefix + 'prebuilt/standalone-bootstrap/proof.json'] = (standalone[1], 0o100644)
    start_here = f'''Moader backend kit {tag}

Open Moader Setup in this folder. Its desktop app checks the computer and guides you through
the current-boot backend, standalone preparation, SD copies and support export.
The setup window is an app; its controller stays private on this computer.
No command-line tools are needed to open it. Use the graphical launcher:

- macOS: Moader Setup.app
- Linux: Moader Setup (choose Run if your file manager asks)
- Windows: Moader Setup Windows/Moader Setup.exe (select a WSL2 distribution)

The Node 24 controller and Electron desktop runtimes are included for supported hosts.
For advanced CLI use, open docs/INSTALLATION.md and choose a method:

- SmileBASIC with mac2jb: macOS and mac2jb's bundled Node 24.
- SmileBASIC with direct ns2jb: macOS, Linux or WSL2 and native Node 24.
- Seedsow Lullaby Demo or full game: macOS, Linux or WSL2. See docs/SEEDSOW.md.
- Seedsow on-console: experimental Moader A route or regular B route after
  one-time SD/save preparation. The kit includes the source-built standalone
  bootstrap and adapter. A requires an exact qualified FS capture to generate
  its local payload; no console capture is bundled. See
  docs/SEEDSOW_STANDALONE.md before using either route.
- CivSwitch: experimental. See docs/CIV7.md for the required profile.

The kit includes the Moader app, resident service and matching backend
binaries. Preparation checks Python 3.10 or newer and other method dependencies.
The first preparation downloads pinned sources, so have internet access.
SmileBASIC setup uses prebuilt
files without an SDK. The full patched kaiHEN release is in
bundles/kaiHEN-Moader-20261114c.zip, including hbmenu, hbloader, OunceShell,
licenses, and the matched RO/HEN/bootstrap and Moader binaries. The Seedsow
guide provisions the pinned patched
GUI from upstream and installs the SDK needed by its live helper.

For CLI fallback, run preparation from this outer folder (the one containing tools/).
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
        'upstreamHostSourceBundled': False, 'officialHenArchiveBundled': True,
        'patchedFullKaihenSha256': sha(full_kaihen),
        'patchedFullKaihenEntries': full_count,
        'standaloneBootstrapBundled': standalone is not None,
        'standaloneFsPayloadBundled': False,
        'setupGuiBundled': True,
        'setupGuiType': 'desktop-app',
        'setupNodeVersion': HOST_NODE_VERSION,
        'setupUvVersion': HOST_UV_VERSION,
        'setupElectronVersion': HOST_ELECTRON_VERSION,
        'setupDesktopPlatforms': sorted(HOST_ELECTRON_ARCHIVES),
        'setupRuntimePlatforms': sorted(HOST_NODE_ARCHIVES),
        **({'standaloneBootstrapSha256': sha(standalone[0]),
            'standaloneAdapterSha256': spec['standalone']['adapterSha256']}
           if standalone is not None else {}),
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
