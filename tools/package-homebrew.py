#!/usr/bin/env python3
"""Package the normal app and staged plugin without device state or secrets."""
from pathlib import Path
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parent.parent
release = json.loads((root / 'config/release.json').read_text())
files = {
    'switch/Moader.nro': root / 'dist/Moader.nro',
    'kaihen/plugins/moader.nso': root / 'dist/moader.nso',
    'Moader-docs/LICENSE': root / 'LICENSE',
    'Moader-docs/THIRD_PARTY.md': root / 'THIRD_PARTY.md',
    'Moader-docs/NotoSans-OFL.txt': root / 'native/assets/NotoSans-OFL.txt',
    'Moader-docs/stb_truetype.h': root / 'native/third_party/stb_truetype.h',
}
for notice in sorted((root / 'native/third_party/licenses').glob('*.txt')):
    files['Moader-docs/licenses/' + notice.name] = notice
app, service = files['switch/Moader.nro'].read_bytes(), files['kaihen/plugins/moader.nso'].read_bytes()
assert app[16:20] == b'NRO0' and service[:4] == b'NSO0'
instructions = '''Moader RELEASE_TAG (app display version 0.1.0-dev)

Extract the switch/ and kaihen/ directories to the SD root. Open Moader
normally from hbmenu. The M mark is shared by the app, web GUI and favicon.

Y: web on/off. ZR: address and six-digit pairing code. Left stick: Settings.
A: enable/disable request. X then X: recoverable removal. B: back/cancel.
L/R: game. D-pad: navigation. Right stick: full mod details. ZL: debug.
Minus: save a screenshot. Plus: hbmenu. Settings includes Advanced and Credits.

Settings persist on the Switch: game names/Title IDs, reduced motion,
debug-log following, automatic web startup and the web port. Unknown game
names fall back to their Title IDs. A changed port applies at the next
listener start. The web server defaults off at port 7712.

Select each game, then open Settings / Game copy. Cartridge and game-key
copies require their matching inserted card. Choose Digital only for a copy
you can play without that card. Unspecified checks the inserted card; it does
not infer digital ownership from downloaded files. Missing/uncertain games
keep requested settings but are omitted from new routing. Read apply details
for acknowledgement or withdrawal errors. Already loaded changes may remain.

State polling keeps the last complete library available during imports.
Refresh in the browser explicitly rescans SD; failed scans retain that list
with a separate warning and pause changes until a complete scan succeeds.

The standalone foreground app hosts the web GUI while Moader stays open.
The moader.nso file is staged for a later kaiHEN bootstrap; copying it does
not start it in an already-running HEN. Resident browser access during games
also requires the separate privileged filesystem-runtime installation.
This archive does not perform that installation. Beginner setup and usage:
https://github.com/H644b/Moader/blob/RELEASE_TAG/docs/INSTALLATION.md
https://github.com/H644b/Moader/blob/RELEASE_TAG/docs/USAGE.md
https://github.com/H644b/Moader/wiki

Native debug output in this build requires the matching updated filesystem
runtime, which publishes closed log snapshots. The app and resident include
the compressed ZIP boundary fix, verified by host tests. These rebuilt binaries
have not been console-tested. Resident browser management,
native debug output and live enable/disable/removal of an MK8 course-preview
texture passed device checks with the original binaries. That preview refreshed after a race, quitting
to the main menu and reopening course selection, without closing the game.
Copying the new app/plugin does not update FS hooks.

Resident browser uploads use the running service's ZIP reader. Copying only
the new NRO does not fix uploads handled by an older resident. Use the matched
updated backend kit for the next supported per-boot setup.

Requested settings and runtime acknowledgement are separate. Same-sized
loose-file replacements support live reads; the MK8 texture test confirmed
this after a scene reload. Game caches,
changed file geometry and executable changes can require content to reopen
or the game to relaunch. Do not infer game success from a library write.

No device pairing code, saves, firmware, game files or private state are
included in this package. Corresponding source, credits and build instructions:
https://github.com/H644b/Moader/tree/RELEASE_TAG
This is a development prerelease.
'''.replace('RELEASE_TAG', release['tag'])
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
