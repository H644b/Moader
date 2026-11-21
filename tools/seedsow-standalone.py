#!/usr/bin/env python3
"""Prepare the pinned on-console Seedsow save and matched Moader SD files.

Provisioning and checks are host-only. ``upload-save`` writes the Seedsow save
through its existing HTTP launcher; ``stage-sd`` writes only the selected SD
directory. Neither command starts the FS exploit or kaiHEN on the console.
"""

from pathlib import Path, PurePosixPath
import argparse
import hashlib
import importlib.util
import ipaddress
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PROFILE = ROOT / 'config/seedsow-standalone.json'
DEFAULT_SOURCE = ROOT / 'build/seedsow-standalone-upstream'
PAYLOAD_FILES = {
    'context.bin': '44690bcfc77b3550ea87e38d00161cc5311b190285e7759f96278f2b236881a9',
    'fs-abi.bin': '1b7e348fd08186640c2c554e97d0b04141bf2c333dc2d584b17b9ec912f42b4f',
    'gateway.bin': '7680176144606f32f02c3ad60586be64a63538ae773327708699ec32b77576e3',
    'manifest.json': '9ceac7e5070a0fde5911e31e729a3ae8487f9aebbaab3fc579a4f170fe9845ea',
    'profile.lua': '314afb902e48e2827e6a7b3fad331e994eba9baf63226f141b11ced22272b96c',
    'runtime.bin': 'a0b1fbedd1e3c35f17349f05b19b20b16ced737eb26c768aaf0040dd5ad86c8c',
    'runtime.log': 'dd9b476246581edb636a10ad8caaad58e7ceb2024c87d58333c80550aaeed55f',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def profile():
    spec = json.loads(PROFILE.read_text())
    require(spec['schema'] == 1 and spec['repository'] ==
            'https://github.com/Malev0/seedsow-fs-kaihen.git' and
            len(spec['revision']) == 40 and len(spec['files']) == 16,
            'Standalone Seedsow source profile is incomplete')
    return spec


def git(directory, *args):
    return subprocess.check_output(['git', '-C', str(directory), *args], text=True,
                                   stderr=subprocess.PIPE, timeout=30).strip()


def qualify_source(source, spec):
    source = Path(source).resolve(strict=True)
    require(source.is_dir() and not source.is_symlink() and
            Path(git(source, 'rev-parse', '--show-toplevel')).resolve() == source and
            git(source, 'rev-parse', 'HEAD') == spec['revision'],
            'Standalone Seedsow checkout is not the reviewed upstream commit')
    lua_names = {'console-only/lua/' + item.name for item in
                 (source / 'console-only/lua').glob('*.lua')}
    require(lua_names == {name for name in spec['files'] if name.endswith('.lua')},
            'Standalone Seedsow Lua module list changed')
    for name, expected in spec['files'].items():
        path = source / name
        require(path.is_file() and not path.is_symlink() and
                path.resolve().is_relative_to(source) and sha(path.read_bytes()) == expected,
                'Standalone Seedsow source differs: ' + name)
    return source


def provision(destination, spec):
    destination = Path(destination).absolute()
    if destination.exists():
        return qualify_source(destination, spec)
    destination.parent.mkdir(parents=True, exist_ok=True)
    require(destination.parent.resolve() == destination.parent,
            'Standalone source destination parent must be canonical')
    staging = Path(tempfile.mkdtemp(prefix='.seedsow-standalone-',
                                    dir=destination.parent))
    try:
        subprocess.run(['git', 'clone', '--quiet', '--no-checkout',
                        spec['repository'], str(staging)], check=True, timeout=180)
        subprocess.run(['git', '-C', str(staging), 'checkout', '--quiet', '--detach',
                        spec['revision']], check=True, timeout=30)
        qualify_source(staging, spec)
        staging.rename(destination)
        return qualify_source(destination, spec)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def qualify_node(node):
    version = subprocess.check_output([node, '-p', 'process.versions.node'],
                                      text=True, timeout=10).strip()
    require(int(version.split('.')[0]) >= 20, 'Standalone uploader needs Node.js 20+')
    return version


def qualify_uploader(source, node):
    qualify_node(node)
    subprocess.run([node, str(source / 'console-only/upload.cjs'), '--check'],
                   cwd=source, check=True, timeout=30)


def prepare_upload_tree(source, destination, node):
    """Copy the pinned uploader and add isolated A/B save modules."""
    module_spec = importlib.util.spec_from_file_location(
        'moader_standalone_overlay', ROOT / 'tools/seedsow-standalone-overlay.py')
    overlay = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(overlay)
    destination = Path(destination).resolve(strict=True)
    shutil.copytree(source / 'console-only', destination / 'console-only')
    (destination / 'tools').mkdir()
    shutil.copy2(source / 'tools/console-host.cjs',
                 destination / 'tools/console-host.cjs')
    overlay_dir = destination / 'overlay'
    for name, prepared in overlay.build(source, overlay_dir).items():
        shutil.copy2(prepared, destination / 'console-only/lua' / name)
    uploader_path = destination / 'console-only/upload.cjs'
    uploader_text = uploader_path.read_text()
    uploader_text = overlay.replace_once(uploader_text,
        "  'exploita2.lua',\n  'inject2.lua',",
        "  'exploita2.lua',\n  'exploitb2.lua',\n"
        "  's2_b_kh_ro.lua',\n  's2_b_kaihen.lua',\n  's2_b_main.lua',\n"
        "  'inject2.lua',", 'B module upload list')
    uploader_text = overlay.replace_once(uploader_text,
        "    ['exploita2.lua', 'ax_exploita2.lua'],\n",
        "    ['exploita2.lua', 'ax_exploita2.lua'],\n"
        "    ['exploitb2.lua', 'exploitb.lua'],\n"
        "    ['exploitb2.lua', 'exploit2.lua'],\n"
        "    ['exploitb2.lua', 'ax_exploitb2.lua'],\n",
        'B launcher aliases')
    uploader_path.write_text(uploader_text)
    subprocess.run([node, str(destination / 'console-only/upload.cjs'), '--check'],
                   cwd=destination, check=True, timeout=30)
    return destination / 'console-only/upload.cjs'


def qualify_upload_receipt(transcript, result):
    require(result == 0, 'Upstream standalone save upload failed; inspect its log')
    receipt = re.findall(r'commit_rc=0x([0-9a-fA-F]+)', transcript)
    require(receipt and all(int(value, 16) == 0 for value in receipt),
            'Standalone save commit was not verified; do not start FS/HEN')


def moader_sd_files():
    spec = json.loads((ROOT / 'config/release.json').read_text())
    module_spec = importlib.util.spec_from_file_location(
        'moader_standalone_release', ROOT / 'tools/package-release.py')
    release = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(release)
    app = release.binary(ROOT, spec['binaries']['Moader.nro'], b'NRO0', 16)
    service = release.binary(ROOT, spec['binaries']['moader.nso'], b'NSO0')
    hen, bootstrap, proof = release.native_pair(ROOT, spec)
    require(proof.get('schema') == 3 and bootstrap is not None and
            'patched_ro' in spec, 'Standalone Seedsow needs a matched RO/HEN/bootstrap set')
    ro = release.binary(ROOT, spec['patched_ro'], b'NSO0')
    archive, _ = release.full_kaihen_bundle(
        ROOT, app, service, ro, hen, bootstrap, (2026, 11, 20, 0, 0, 0))
    files = {}
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        require(z.testzip() is None, 'Matched kaiHEN archive failed CRC')
        for member in z.namelist():
            if member.endswith('/'):
                continue
            path = PurePosixPath(member)
            require(not path.is_absolute() and '..' not in path.parts and
                    len(path.parts) > 0, 'Invalid kaiHEN archive path')
            files['kaihen/' + member] = z.read(member)
    for name in ('bootstrap.bin', 'ro/main', 'hen/main', 'game/main'):
        files['kaihen/bin/' + name] = files['kaihen/' + name]
    files['hbmenu.nro'] = files['kaihen/hbmenu.nro']
    files['switch/ounceshell.nro'] = files['kaihen/programs/ounceshell.nro']
    files['switch/Moader.nro'] = app
    require(files['kaihen/plugins/moader.nso'] == service and
            files['kaihen/bin/bootstrap.bin'] == bootstrap and
            files['kaihen/bin/ro/main'] == ro and
            files['kaihen/bin/hen/main'] == hen,
            'Standalone SD layout lost a matched Moader component')
    return files, {'moaderVersion': spec['version'],
                   'kaihenBase': proof['release_base'],
                   'protocol': proof['protocol_version'],
                   'fileCount': len(files), 'sha256': sha(archive)}


def standalone_payload_files(directory):
    selected = Path(directory).absolute()
    require(not selected.is_symlink(),
            'Select a regular standalone FS payload directory')
    directory = selected.resolve(strict=True)
    require(directory.is_dir(), 'Select a regular standalone FS payload directory')
    require({p.name for p in directory.iterdir()} == set(PAYLOAD_FILES),
            'Standalone FS payload file list differs')
    files = {}
    for name, expected in PAYLOAD_FILES.items():
        source = directory / name
        require(source.is_file() and not source.is_symlink(),
                'Standalone FS payload is not a regular file: ' + name)
        data = source.read_bytes()
        require(sha(data) == expected,
                'Standalone FS payload differs: ' + name)
        files['kaihen/moader-fs/' + name] = data
    manifest = json.loads(files['kaihen/moader-fs/manifest.json'])
    require(manifest['schema'] == 1 and
            manifest['captureSha256'] ==
            '0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271' and
            manifest['contextBytes'] == 86016 and manifest['runtime']['bytes'] == 106496 and
            manifest['gateway']['bytes'] == 816 and len(manifest['hooks']) == 19 and
            manifest['fsAbi']['bytes'] == 8752 and len(manifest['fsAbi']['ranges']) == 36 and
            manifest['independentRelocationCheck'] and
            manifest['independentContextRelocationCheck'],
            'Standalone FS payload manifest is not qualified')
    for key in ('context', 'runtime', 'gateway', 'fsAbi'):
        row = manifest[key]
        data = files['kaihen/moader-fs/' + row['file']]
        require(len(data) == row['bytes'] and sha(data) == row['sha256'],
                'Standalone FS manifest lost ' + key)
    files['kaihen/moader-fs/s2_moader.lua'] = (ROOT / 'standalone/s2_moader.lua').read_bytes()
    return files, {'captureSha256': manifest['captureSha256'],
                   'fileCount': len(files), 'payloadSha256': sha(
                       files['kaihen/moader-fs/manifest.json']),
                   'installerSha256': sha(files['kaihen/moader-fs/s2_moader.lua'])}


def sd_root(target):
    raw = Path(target).absolute()
    require(raw.exists() and raw.is_dir() and not raw.is_symlink() and
            raw.resolve() == raw and raw != Path(raw.anchor) and
            raw != Path.home() and not ROOT.is_relative_to(raw) and
            not raw.is_relative_to(ROOT),
            'Select an existing canonical SD root, not a system or project directory')
    return raw


def checked_target(root, relative):
    parts = PurePosixPath(relative).parts
    require(parts and not any(part in ('', '.', '..') for part in parts),
            'Invalid SD relative path')
    target = root.joinpath(*parts)
    item = target
    while item != root:
        if item.is_symlink():
            raise ValueError('SD path contains a symlink: ' + str(item))
        item = item.parent
    require(not target.exists() or target.is_file(),
            'SD target is not a regular file: ' + relative)
    return target


def verify_sd(root, files):
    bad = [relative for relative, data in files.items()
           if not (target := checked_target(root, relative)).is_file() or
           target.stat().st_size != len(data) or sha(target.read_bytes()) != sha(data)]
    require(not bad, 'SD readback differs for: ' + ', '.join(bad[:8]))


def stage_sd(root, files):
    targets = {name: checked_target(root, name) for name in files}
    staging = Path(tempfile.mkdtemp(prefix='.moader-seedsow-stage-', dir=root))
    try:
        for name, data in files.items():
            temporary = staging.joinpath(*PurePosixPath(name).parts)
            temporary.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_bytes(data)
            require(sha(temporary.read_bytes()) == sha(data),
                    'SD staging readback differs: ' + name)
        for name, target in targets.items():
            target.parent.mkdir(parents=True, exist_ok=True)
            checked_target(root, name)
            staging.joinpath(*PurePosixPath(name).parts).replace(target)
        verify_sd(root, files)
    finally:
        shutil.rmtree(staging)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('provision', 'check', 'stage-sd',
                                           'verify-sd', 'upload-save'))
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE,
                        help='pinned upstream checkout (default: build/seedsow-standalone-upstream)')
    parser.add_argument('--sd-root', type=Path, help='mounted SD card root')
    parser.add_argument('--host', help='Switch LAN IPv4, for upload-save only')
    parser.add_argument('--node', default='node', help='Node.js 20+ for the upstream uploader')
    parser.add_argument('--payload-dir', type=Path,
                        help='exact source-qualified Moader FS payload directory')
    options = parser.parse_args(argv)
    spec = profile()
    if options.action == 'provision':
        source = provision(options.source, spec)
    else:
        source = qualify_source(options.source, spec)
    if options.action in ('check', 'provision'):
        qualify_uploader(source, options.node)
    if options.action in ('check', 'upload-save'):
        with tempfile.TemporaryDirectory(prefix='moader-standalone-upload-') as temporary:
            prepare_upload_tree(source, Path(temporary), options.node)
    payload = None
    if options.payload_dir is not None:
        payload, payload_info = standalone_payload_files(options.payload_dir)
    else:
        payload_info = None
    if options.action in ('check', 'stage-sd', 'verify-sd'):
        files, bundle = moader_sd_files()
        if options.action != 'check':
            require(payload is not None, '--payload-dir is required for Moader FS installation')
            files.update(payload)
            require(options.sd_root is not None, '--sd-root is required')
            root = sd_root(options.sd_root)
            if options.action == 'stage-sd':
                stage_sd(root, files)
            else:
                verify_sd(root, files)
    else:
        bundle = None
    if options.action == 'upload-save':
        require(payload is not None, '--payload-dir is required before uploading the Moader save')
        require(options.host is not None, '--host is required')
        address = ipaddress.IPv4Address(options.host)
        require(address.is_private and not address.is_loopback and
                not address.is_multicast and not address.is_unspecified,
                'Switch address must be a private LAN IPv4')
        qualify_uploader(source, options.node)
        with tempfile.TemporaryDirectory(prefix='moader-standalone-upload-') as temporary:
            uploader = prepare_upload_tree(source, Path(temporary), options.node)
            with subprocess.Popen([options.node, str(uploader), '--host', str(address)],
                                  cwd=temporary, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, bufsize=1) as process:
                transcript = []
                for line in process.stdout:
                    print(line, end='', flush=True)
                    transcript.append(line)
                result = process.wait()
        qualify_upload_receipt(''.join(transcript), result)
    print(json.dumps({'action': options.action, 'upstreamRevision': spec['revision'],
                      'scope': 'host and SD only' if options.action != 'upload-save'
                      else 'Seedsow save upload; no FS/HEN run',
                      'moaderBackend': 'experimental on-console FS installer; same-boot device result pending',
                      **({'fsPayload': payload_info} if payload_info else {}),
                      **({'matchedBundle': bundle} if bundle else {})}, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print('Seedsow standalone preparation failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
