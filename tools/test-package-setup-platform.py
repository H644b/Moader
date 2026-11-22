#!/usr/bin/env python3
"""Exercise the OS app layouts with synthetic runtimes; no downloads or signing."""
from pathlib import Path
import importlib.util
import plistlib
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('moader_setup_layout', ROOT / 'tools/package-setup-platform.py')
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)
package = layout.release_package()


def main():
    tag = 'v4.0.0'
    prefix = f'Moader-Setup-{tag}/'
    with tempfile.TemporaryDirectory(prefix='moader-platform-fixture-') as folder:
        folder = Path(folder)
        original = folder / 'universal.zip'
        files = {
            'config/release.json': b'{"version":"4.0.0"}',
            'setup/desktop-app/main.cjs': b'// desktop entry',
            'setup/desktop-app/package.json': b'{"main":"main.cjs"}',
            'setup/launch.sh': b'#!/bin/sh\n',
            'dist/Moader.nro': b'NRO0fixture',
            'vendor/kaihen-mo/hen/main': b'NSO0fixture',
            'host-runtimes/node-v24.22.3/LICENSE': b'Node notice',
        }
        for host in ('darwin-arm64', 'darwin-x64', 'linux-arm64', 'linux-x64'):
            files[f'host-runtimes/node-v24.22.3/{host}/node'] = b'node ' + host.encode()
            files[f'host-runtimes/uv-v0.12.16/{host}/uv'] = b'uv ' + host.encode()
        for target, (electron, _) in layout.TARGETS.items():
            if target.startswith('macos-'):
                base = f'host-runtimes/electron-v45.4.1/{electron}/Electron.app/'
                files[base + 'Contents/MacOS/Electron'] = b'mac binary'
                files[base + 'Contents/Info.plist'] = plistlib.dumps({'CFBundleName': 'Electron'})
            elif target.startswith('linux-'):
                files[f'host-runtimes/electron-v45.4.1/{electron}/electron'] = b'linux binary'
            else:
                files['Moader Setup Windows/Moader Setup.exe'] = b'windows binary'
        with zipfile.ZipFile(original, 'w') as archive:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(prefix + name)
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                archive.writestr(info, data)
        with zipfile.ZipFile(original) as archive:
            source = layout.inventory(archive, prefix, package)
            for target in layout.TARGETS:
                path = folder / (target + '.zip')
                package.write_zip(path, layout.files_for_target(source, target, tag, package, archive),
                                  archive.infolist()[0].date_time)
                root = f'Moader-Setup-{tag}-{target}/'
                with zipfile.ZipFile(path) as built:
                    names = set(built.namelist())
                    if target.startswith('macos-'):
                        resources = root + 'Moader Setup.app/Contents/Resources/'
                        assert root + 'Moader Setup.app/Contents/MacOS/Electron' in names
                        plist = plistlib.loads(built.read(root + 'Moader Setup.app/Contents/Info.plist'))
                        assert plist['CFBundleName'] == 'Moader Setup'
                    else:
                        resources = root + 'resources/'
                        assert root + ('Moader Setup.exe' if target == 'windows-x64' else 'Moader Setup') in names
                    assert built.read(resources + 'app/main.cjs') == b'// desktop entry'
                    assert built.read(resources + 'kit/dist/Moader.nro') == b'NRO0fixture'
                    assert len([n for n in names if n.endswith('/node')]) == 1
                    assert len([n for n in names if n.endswith('/uv')]) == 1
    print('Five self-contained prebuilt app layouts passed synthetic archive checks')


if __name__ == '__main__':
    main()
