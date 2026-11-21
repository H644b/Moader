#!/usr/bin/env python3
"""Package the normal app and staged plugin without device state or secrets."""
from pathlib import Path
import hashlib
import json
import subprocess
import zipfile

root = Path(__file__).resolve().parent.parent
release = json.loads((root / 'config/release.json').read_text())
files = {
    'switch/Moader.nro': root / 'dist/Moader.nro',
    'kaihen/plugins/moader.nso': root / 'dist/moader.nso',
    'Moader-docs/LICENSE': root / 'LICENSE',
    'Moader-docs/THIRD_PARTY.md': root / 'THIRD_PARTY.md',
    'Moader-docs/NotoSans-OFL.txt': root / 'native/assets/NotoSans-OFL.txt',
    'Moader-docs/DMSans-OFL.txt': root / 'native/assets/DMSans-OFL.txt',
    'Moader-docs/Bricolage-OFL.txt': root / 'branding/Bricolage-OFL.txt',
    'Moader-docs/stb_truetype.h': root / 'native/third_party/stb_truetype.h',
}
for notice in sorted((root / 'native/third_party/licenses').glob('*.txt')):
    files['Moader-docs/licenses/' + notice.name] = notice
app, service = files['switch/Moader.nro'].read_bytes(), files['kaihen/plugins/moader.nso'].read_bytes()
assert app[16:20] == b'NRO0' and service[:4] == b'NSO0'
tag = release['tag']
repository = 'https://github.com/' + release['repository']
source_commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
instructions = f'''Moader {tag}

1. Close Moader on the Switch if it is open.
2. Copy switch/Moader.nro to the SD card's switch folder.
3. Open Moader from hbmenu.

If the matching backend is running, the app connects to it. Otherwise the
app can manage the library and serve the browser while it stays open.
Press Y if the web listener is off. Press ZR for the address and six-digit
pairing code. Open the displayed address on the same local network.

This ZIP also contains kaihen/plugins/moader.nso. Copy it to the matching
folder on SD to stage a replacement. Open Settings > Advanced >
Reload Moader service to load it if your running backend supports reload.
For first-time backend setup, a reboot or a HEN update, use the matching
backend kit and the installation guide.

Install and update: {repository}/blob/{source_commit}/docs/INSTALLATION.md
Manage mods: {repository}/blob/{source_commit}/docs/USAGE.md
Downloads and checksums: {repository}/releases/tag/{tag}
Corresponding source: {repository}/tree/{source_commit}
'''
manifest = {name: {'bytes': path.stat().st_size,
                  'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            for name, path in files.items()}
target = root / 'dist/Moader-homebrew.zip'
with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name, path in files.items():
        archive.write(path, name)
    archive.writestr('Moader-docs/INSTALL.txt', instructions)
    archive.writestr('Moader-docs/manifest.json', json.dumps(manifest, indent=2) + '\n')
with zipfile.ZipFile(target) as archive:
    assert archive.testzip() is None
    assert archive.read('switch/Moader.nro') == app
    assert archive.read('kaihen/plugins/moader.nso') == service
print('Packaged and verified:', target)
