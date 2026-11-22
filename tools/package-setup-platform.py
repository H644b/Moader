#!/usr/bin/env python3
"""Turn the checked Setup kit into five self-contained prebuilt desktop apps."""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
TARGETS = {
    'macos-arm64': ('darwin-arm64', 'darwin-arm64'),
    'macos-x64': ('darwin-x64', 'darwin-x64'),
    'linux-arm64': ('linux-arm64', 'linux-arm64'),
    'linux-x64': ('linux-x64', 'linux-x64'),
    'windows-x64': ('win32-x64', 'linux-x64'),
}


def release_package():
    spec = importlib.util.spec_from_file_location('moader_platform_release', ROOT / 'tools/package-release.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inventory(archive, prefix, package):
    files = {}
    for info in archive.infolist():
        package.require(info.filename.startswith(prefix), 'Unexpected Setup source member')
        name = info.filename[len(prefix):]
        package.require(name and name not in files and not info.is_dir(), 'Duplicate or empty Setup source member')
        path = Path(name)
        package.require(not path.is_absolute() and '..' not in path.parts, 'Unsafe Setup source member')
        files[name] = (package.ArchiveMember(Path(archive.filename), info.filename), info.external_attr >> 16)
    return files


def files_for_target(source, target, tag, package, archive):
    electron, controller = TARGETS[target]
    package.require('branding/setup-icon.png' in source and 'branding/setup-icon.ico' in source,
                    'Rounded Setup icon exports are missing')
    folder = f'Moader-Setup-{tag}-{target}/'
    if target.startswith('macos-'):
        app = 'Moader Setup.app/'
        resources = app + 'Contents/Resources/'
        runtime_prefix = f'host-runtimes/electron-v{package.HOST_ELECTRON_VERSION}/{electron}/Electron.app/'
    elif target.startswith('linux-'):
        app = ''
        resources = 'resources/'
        runtime_prefix = f'host-runtimes/electron-v{package.HOST_ELECTRON_VERSION}/{electron}/'
    else:
        app = ''
        resources = 'resources/'
        runtime_prefix = 'Moader Setup Windows/'
    output = {}

    def add(destination, value):
        package.require(destination not in output, 'Duplicate platform member: ' + destination)
        output[destination] = value

    for name, value in source.items():
        if not name.startswith(runtime_prefix):
            continue
        relative = name[len(runtime_prefix):]
        if relative in ('resources/app/main.cjs', 'resources/app/package.json'):
            continue
        if target.startswith('linux-') and relative == 'electron':
            relative = 'Moader Setup'
        destination = app + relative
        if target.startswith('macos-') and relative == 'Contents/Info.plist':
            info = plistlib.loads(archive.read(value[0].name))
            info['CFBundleDisplayName'] = 'Moader Setup'
            info['CFBundleName'] = 'Moader Setup'
            info['CFBundleIdentifier'] = 'com.moader.setup'
            info['CFBundleShortVersionString'] = tag.removeprefix('v')
            info['CFBundleIconFile'] = 'MoaderSetup.icns'
            add(folder + destination, (plistlib.dumps(info), value[1]))
        else:
            add(folder + destination, value)
    package.require(any(name.endswith('Moader Setup.exe') for name in output) if target == 'windows-x64' else
                    any(name.endswith('/MacOS/Electron') for name in output) if target.startswith('macos-') else
                    folder + 'Moader Setup' in output, 'Prebuilt Electron program is missing')

    excluded = ('host-runtimes/', 'Moader Setup.app/', 'Moader Setup Windows/')
    for name, value in source.items():
        if name.startswith(excluded) or name in ('Moader Setup', 'Moader Setup Windows.cmd', 'START_HERE.txt'):
            continue
        add(folder + resources + 'kit/' + name, value)
    for runtime_name in (
        f'host-runtimes/node-v{package.HOST_NODE_VERSION}/{controller}/node',
        f'host-runtimes/node-v{package.HOST_NODE_VERSION}/LICENSE',
        f'host-runtimes/uv-v{package.HOST_UV_VERSION}/{controller}/uv',
    ):
        package.require(runtime_name in source, 'Setup controller runtime is missing: ' + runtime_name)
        add(folder + resources + 'kit/' + runtime_name, source[runtime_name])
    for name in ('main.cjs', 'package.json'):
        add(folder + resources + 'app/' + name, source['setup/desktop-app/' + name])
    intro = f'''Moader Setup {tag} for {target}

Open Moader Setup.app on macOS, Moader Setup on Linux, or Moader Setup.exe on
Windows. Keep the extracted app folder together. The app contains its matching
backend files and a prebuilt runtime for this computer; the command-line
backend ZIP is a separate, optional download.

The on-console Seedsow A route still requires a qualified local FS capture.
No private capture, derived payload, save, or pairing code is included.
'''
    add(folder + 'README.txt', (intro.encode(), 0o100644))
    return output


def sign_macos(path, tag):
    with tempfile.TemporaryDirectory(prefix='moader-macos-sign-') as temporary:
        temporary = Path(temporary)
        unsigned = temporary / 'unsigned.zip'
        path.replace(unsigned)
        subprocess.run(['ditto', '-x', '-k', str(unsigned), str(temporary / 'extracted')], check=True)
        folder = temporary / 'extracted' / path.stem
        app = folder / 'Moader Setup.app'
        icon = app / 'Contents/Resources/kit/branding/setup-icon.png'
        iconset = temporary / 'MoaderSetup.iconset'
        iconset.mkdir()
        for name, size in (
            ('16x16', 16), ('16x16@2x', 32), ('32x32', 32), ('32x32@2x', 64),
            ('128x128', 128), ('128x128@2x', 256), ('256x256', 256),
            ('256x256@2x', 512), ('512x512', 512), ('512x512@2x', 1024),
        ):
            subprocess.run(['sips', '-z', str(size), str(size), str(icon), '--out',
                            str(iconset / f'icon_{name}.png')], check=True,
                           stdout=subprocess.DEVNULL)
        subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o',
                        str(app / 'Contents/Resources/MoaderSetup.icns')], check=True)
        subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
        subprocess.run(['ditto', '-c', '-k', '--keepParent', str(folder), str(path)], check=True)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise ValueError('Signed macOS Setup ZIP failed CRC validation: ' + tag)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-dir', required=True, type=Path)
    parser.add_argument('--keep-universal', action='store_true')
    args = parser.parse_args()
    package = release_package()
    directory = args.release_dir.resolve()
    manifest_path = directory / 'release-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    tag = manifest['tag']
    universal_name = f'Moader-Setup-{tag}.zip'
    universal = directory / universal_name
    record = manifest['assets'][universal_name]
    package.require(universal.is_file() and universal.stat().st_size == record['bytes'] and
                    package.sha(universal.read_bytes()) == record['sha256'],
                    'Universal Setup input differs from the release manifest')
    assets = {}
    with zipfile.ZipFile(universal) as archive:
        source = inventory(archive, f'Moader-Setup-{tag}/', package)
        timestamp = archive.infolist()[0].date_time
        for target in TARGETS:
            name = f'Moader-Setup-{tag}-{target}.zip'
            path = directory / name
            package.write_zip(path, files_for_target(source, target, tag, package, archive), timestamp)
            if target.startswith('macos-'):
                package.require(sys.platform == 'darwin', 'macOS Setup apps must be packaged and signed on macOS')
                sign_macos(path, tag)
            assets[target] = name
            print('Packaged prebuilt Setup app: ' + name, flush=True)
    manifest['assets'].pop(universal_name)
    for name in assets.values():
        path = directory / name
        manifest['assets'][name] = {'bytes': path.stat().st_size, 'sha256': package.sha(path.read_bytes())}
    manifest.pop('setupGuiAsset', None)
    manifest['setupGuiAssets'] = assets
    manifest['setupPackageWorkflow'] = '.github/workflows/package-setup.yml'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    checksums = directory / 'SHA256SUMS.txt'
    checksums.write_text(''.join(
        package.sha((directory / name).read_bytes()) + '  ' + name + '\n'
        for name in [f'Moader-{tag}.zip', f'Moader-backend-{tag}.zip',
                     *assets.values(), 'Moader.nro', 'moader.nso', 'release-manifest.json']))
    if not args.keep_universal:
        universal.unlink()
    print('Verified five platform apps, manifest, and release checksums')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('Platform packaging stopped: ' + str(error))
