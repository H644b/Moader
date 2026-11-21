#!/usr/bin/env python3
"""Package the normal app and staged plugin without device state or secrets."""
from pathlib import Path
from runpy import run_path
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
    'Moader-docs/store-catalog.json': root / 'native/store/catalog.json',
    'Moader-docs/stb_truetype.h': root / 'native/third_party/stb_truetype.h',
}
for notice in sorted((root / 'native/third_party/licenses').glob('*.txt')):
    files['Moader-docs/licenses/' + notice.name] = notice
app, service = files['switch/Moader.nro'].read_bytes(), files['kaihen/plugins/moader.nso'].read_bytes()
assert app[16:20] == b'NRO0' and service[:4] == b'NSO0'
tag = release['tag']
repository = 'https://github.com/' + release['repository']
source_commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
guide_paths = (
    'docs/INSTALLATION.md', 'docs/SMILEBASIC.md', 'docs/SEEDSOW_STANDALONE.md',
    'docs/SEEDSOW.md', 'docs/CIV7.md',
)
convert_links = run_path(str(root / 'tools/build-wiki.py'))['convert']
guides = {}
for name in guide_paths:
    source = root / name
    linked = convert_links(source.read_text(), source, release['repository'], bundled_guides=guide_paths)
    guides[name] = linked.encode('utf-8')

instructions = f'''Moader {tag}

Open docs/INSTALLATION.md in this ZIP for installation instructions.
All four method guides are in the same docs folder.

Online installation guide
{repository}/wiki/Installation

Release notes and checksums
{repository}/releases/tag/{tag}

Corresponding source
{repository}/tree/{source_commit}
'''
manifest = {name: {'bytes': path.stat().st_size,
                  'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            for name, path in files.items()}
for name, content in guides.items():
    manifest[name] = {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
target = root / 'dist/Moader-homebrew.zip'
with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name, path in files.items():
        archive.write(path, name)
    for name, content in guides.items():
        archive.writestr(name, content)
    archive.writestr('Moader-docs/INSTALL.txt', instructions)
    archive.writestr('Moader-docs/manifest.json', json.dumps(manifest, indent=2) + '\n')
with zipfile.ZipFile(target) as archive:
    assert archive.testzip() is None
    assert archive.read('switch/Moader.nro') == app
    assert archive.read('kaihen/plugins/moader.nso') == service
print('Packaged and verified:', target)
