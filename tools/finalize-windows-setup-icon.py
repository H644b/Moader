#!/usr/bin/env python3
"""Embed the rounded Setup icon in the prebuilt Windows EXE and reseal assets."""
from pathlib import Path, PurePosixPath
import argparse
import copy
import hashlib
import json
import subprocess
import tempfile
import zipfile

RCEDIT_SHA256 = '3e7801db1a5edbec91b49a24a094aad776cb4515488ea5a4ca2289c400eade2a'


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def starts_with(path, prefix):
    with path.open('rb') as stream:
        return stream.read(len(prefix)) == prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-dir', required=True, type=Path)
    parser.add_argument('--rcedit', required=True, type=Path)
    args = parser.parse_args()
    directory = args.release_dir.resolve()
    tool = args.rcedit.resolve()
    require(tool.is_file() and sha(tool) == RCEDIT_SHA256,
            'Pinned rcedit-x64.exe checksum differs')
    manifest_path = directory / 'release-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    require(manifest['tag'] == 'v4.0.0', 'Unexpected release for Windows icon update')
    name = manifest['setupGuiAssets']['windows-x64']
    path = directory / name
    record = manifest['assets'][name]
    require(path.is_file() and path.stat().st_size == record['bytes'] and
            sha(path) == record['sha256'], 'Windows Setup archive differs from the manifest')
    prefix = f'Moader-Setup-{manifest["tag"]}-windows-x64/'
    exe_name = prefix + 'Moader Setup.exe'
    ico_name = prefix + 'resources/kit/branding/setup-icon.ico'
    with tempfile.TemporaryDirectory(prefix='moader-setup-windows-icon-',
                                     dir=directory) as temp_name:
        temp = Path(temp_name)
        exe = temp / 'Moader Setup.exe'
        ico = temp / 'setup-icon.ico'
        revised = temp / name
        with zipfile.ZipFile(path) as original:
            require(original.testzip() is None, 'Windows Setup ZIP has a CRC error')
            names = original.namelist()
            require(len(names) == len(set(names)) and exe_name in names and ico_name in names,
                    'Windows Setup EXE or rounded icon is missing')
            for member in names:
                part = PurePosixPath(member)
                require(member.startswith(prefix) and not part.is_absolute() and
                        '..' not in part.parts, 'Unsafe Windows Setup ZIP member')
            exe.write_bytes(original.read(exe_name))
            ico.write_bytes(original.read(ico_name))
            require(starts_with(exe, b'MZ') and starts_with(ico, b'\x00\x00\x01\x00'),
                    'Invalid Windows executable or icon format')
            previous = sha(exe)
            subprocess.run([str(tool), str(exe), '--set-icon', str(ico)], check=True)
            require(starts_with(exe, b'MZ') and sha(exe) != previous,
                    'Windows executable icon was not updated')
            with zipfile.ZipFile(revised, 'w', compression=zipfile.ZIP_DEFLATED,
                                 compresslevel=9) as updated:
                for info in original.infolist():
                    data = exe.read_bytes() if info.filename == exe_name else original.read(info)
                    updated.writestr(copy.copy(info), data)
        with zipfile.ZipFile(revised) as verified:
            require(verified.testzip() is None and verified.namelist() == names and
                    verified.read(exe_name) == exe.read_bytes(),
                    'Windows Setup ZIP failed post-icon readback')
        revised.replace(path)
    manifest['assets'][name] = {'bytes': path.stat().st_size, 'sha256': sha(path)}
    manifest['windowsIconEmbedded'] = True
    manifest_path.write_bytes((json.dumps(manifest, indent=2) + '\n').encode('utf-8'))
    checksums = directory / 'SHA256SUMS.txt'
    listed = [f'Moader-{manifest["tag"]}.zip', f'Moader-backend-{manifest["tag"]}.zip',
              *manifest['setupGuiAssets'].values(), 'Moader.nro', 'moader.nso',
              'release-manifest.json']
    require(set(listed) == set(manifest['assets']) | {'release-manifest.json'},
            'Final release asset list differs from the manifest')
    checksums.write_bytes(''.join(sha(directory / item) + '  ' + item + '\n'
                                  for item in listed).encode('ascii'))
    for item in listed:
        asset = directory / item
        require(asset.is_file(), 'Final release asset is missing: ' + item)
        if item in manifest['assets']:
            record = manifest['assets'][item]
            require(asset.stat().st_size == record['bytes'] and sha(asset) == record['sha256'],
                    'Final release asset differs from manifest: ' + item)
    print('Embedded the rounded Windows EXE icon and verified final release checksums')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('Windows icon packaging stopped: ' + str(error))
