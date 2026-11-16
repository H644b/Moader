#!/usr/bin/env python3
"""Check the actual Switch server; optional test-library changes use one owned fixture."""
from pathlib import Path
import argparse
import datetime
import hashlib
import io
import ipaddress
import json
import re
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('host', type=lambda value: str(ipaddress.IPv4Address(value)))
parser.add_argument('--port', type=int, default=7712)
parser.add_argument('--manage-fixture', action='store_true', help='Import, toggle and remove a harmless test entry.')
parser.add_argument('--capture', action='store_true', help='Download a previously exported native screenshot.')
parser.add_argument('--settings', action='store_true', help='Verify shared preference changes, then restore their original values.')
parser.add_argument('--metadata-title', help='Read actual installed-game metadata for this Title ID without changing game data.')
parser.add_argument('--pairing-file', type=Path, help='Use this private saved code file when the same console has a new LAN address.')
parser.add_argument('--require-runtime', action='store_true', help='Require current-generation native acknowledgements after each fixture change.')
parser.add_argument('--require-native-log', action='store_true', help='Also require native activation output and a fresh routing log row for every acknowledged fixture change.')
args = parser.parse_args()
if args.require_native_log and not (args.require_runtime and args.manage_fixture):
    parser.error('--require-native-log needs --require-runtime and --manage-fixture')
base = 'http://%s:%d' % (args.host, args.port)
token_path = args.pairing_file or ROOT / 'state/devices' / args.host / 'pairing-code'
token = token_path.read_text().strip()
if len(token) != 6 or any(c not in '0123456789' for c in token):
    raise SystemExit('The saved code is not six digits. Install the new build and retain its current code first.')

stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
folder = ROOT / 'evidence/device'
folder.mkdir(parents=True, exist_ok=True)
destination = folder / ('web-' + stamp + '.json')
report = {'started': stamp, 'base': base, 'complete': False, 'checks': [],
          'runtimeRequired': args.require_runtime,
          'nativeLogRequired': args.require_native_log,
          'scope': 'Actual Switch HTTP server. Native acknowledgement proves routing acceptance, not game reads or visible modification.'}

def save():
    destination.write_text(json.dumps(report, indent=2) + '\n')
    destination.chmod(0o600)

def request(path, method='GET', body=None, auth=True, origin=None, limit=4 * 1024 * 1024):
    headers = {'Authorization': 'Bearer ' + token} if auth else {}
    if origin:
        headers['Origin'] = origin
    req = urllib.request.Request(base + path, data=body, method=method, headers=headers)
    try:
        response = urllib.request.urlopen(req, timeout=10)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        data = response.read(limit + 1)
        if len(data) > limit:
            raise RuntimeError('Device response exceeded its bound: ' + path)
        return response.status if hasattr(response, 'status') else response.code, data

def api(path, method='GET', params=None, body=None, expected=(200,)):
    if params:
        path += '?' + urllib.parse.urlencode(params)
    status, data = request('/api/' + path, method, body, origin=base)
    if status not in expected:
        # JSON error text never contains the supplied bearer code.
        raise RuntimeError('%s returned %d: %s' % (path, status, data.decode('utf8', 'replace')[:300]))
    return json.loads(data)

def checked(name):
    report['checks'].append(name)
    save()
    print('PASS:', name)

def native_log_acknowledged(generation, stage):
    if not args.require_native_log:
        return
    route = re.compile(r'^Runtime: Moader routing published 0{16} ([0-9a-f]{16}) 0{8}$')
    last = {}
    for attempt in range(21):
        events = api('events')
        if not isinstance(events, list) or any(not isinstance(row, str) for row in events):
            raise RuntimeError('Native log endpoint did not return an event list')
        native = [row for row in events if row.startswith('Runtime: ')]
        diagnostics = [row for row in events if row.startswith('Runtime diagnostics: ')]
        if any('native report writer failed' in row for row in diagnostics):
            raise RuntimeError('Native report writer failed at ' + stage)
        matching = [row for row in native if (match := route.fullmatch(row)) and int(match.group(1), 16) == generation]
        header = 'Runtime: Moader filesystem runtime 0.1.1' in native
        activated = 'Runtime: Activation complete: 1' in native
        loader = any(row.startswith('Runtime: Loader active ') for row in native)
        last = {'stage': stage, 'generation': generation, 'nativeRowCount': len(native),
                'matchingRoutingRows': matching, 'headerSeen': header,
                'activationSeen': activated, 'loaderSeen': loader,
                'diagnostics': diagnostics}
        if matching and (stage != 'initial library' or (header and activated and loader)):
            report.setdefault('nativeLogAcknowledgements', []).append(last)
            checked('Native debug log shows ' + stage + ' generation ' + str(generation))
            return
        if attempt < 20:
            time.sleep(0.25)
    report['nativeLogFailure'] = last
    save()
    raise RuntimeError('Native debug log did not show ' + stage + ' within the bounded wait')

def acknowledged(state, stage, check_log=True):
    if not args.require_runtime:
        return state
    generation = state['generation']
    for attempt in range(21):
        runtime = state['runtime']
        if state['generation'] != generation:
            raise RuntimeError('Library changed independently while checking ' + stage)
        if runtime['result']:
            raise RuntimeError('Native runtime rejected routing at ' + stage)
        if runtime['connected'] and runtime['appliedGeneration'] == generation:
            report.setdefault('routingAcknowledgements', []).append({'stage': stage, 'state': state})
            checked('Native runtime acknowledged ' + stage + ' generation ' + str(generation))
            if check_log:
                native_log_acknowledged(generation, stage)
            return state
        if attempt < 20:
            time.sleep(0.25)
            state = api('state')
    raise RuntimeError('Native runtime did not acknowledge ' + stage + ' within the bounded wait')

save()
try:
    for route, local in [('/', 'index.html'), ('/style.css', 'style.css'), ('/app.js', 'app.js'), ('/icon.svg', 'icon.svg')]:
        expected = (ROOT / 'web' / local).read_bytes()
        status, received = request(route, auth=False)
        if status != 200 or received != expected:
            raise RuntimeError('Device serves different bytes for ' + route)
        checked('Current ' + local + ' served by Switch (' + hashlib.sha256(received).hexdigest()[:12] + ')')
    if request('/api/state', auth=False)[0] != 401:
        raise RuntimeError('Unauthenticated library request was not rejected')
    checked('Unauthenticated API rejected')
    if request('/api/state', origin='http://unrelated.invalid')[0] != 403:
        raise RuntimeError('Foreign origin request was not rejected')
    checked('Foreign origin rejected')
    before = acknowledged(api('state'), 'initial library')
    report['stateBefore'] = before
    report['leadingZeroHardwareCase'] = token.startswith('0')
    checked('Six-digit pairing accepted')
    if args.metadata_title:
        metadata = api('game-name', params={'titleId': args.metadata_title})
        report['gameMetadata'] = metadata
        checked('Installed-game metadata queried: ' + ('name available' if metadata['name'] else 'Title ID fallback'))
    if args.settings:
        original = api('settings')
        report['preferencesBefore'] = original
        fields = ['gameNames', 'reducedMotion', 'webOnStart', 'followLog']
        candidate = {field: int(not original[field]) for field in fields}
        candidate['webPort'] = 8772 if original['webPort'] != 8772 else 8774
        try:
            changed = api('settings', 'POST', candidate, b'')
            if any(changed[field] != bool(candidate[field]) for field in fields) or changed['webPort'] != candidate['webPort']:
                raise RuntimeError('Shared settings did not match the requested changes')
            state = api('state')
            if state['settings'] != changed or state['generation'] != before['generation']:
                raise RuntimeError('Preferences did not synchronize independently of mod routing')
            checked('All shared preferences saved; current listener stays available until toggled')
            changed = api('settings', 'POST', {'gameNames': int(original['gameNames'])}, b'')
            if changed['webPort'] != candidate['webPort']:
                raise RuntimeError('A partial update overwrote a different saved setting')
            checked('Partial preference updates preserve other saved fields')
        finally:
            restore = {key: int(value) if isinstance(value, bool) else value for key, value in original.items()}
            restored = api('settings', 'POST', restore, b'')
            if restored != original:
                raise RuntimeError('Original preferences were not restored')
            report['preferencesRestored'] = True
            save()
    if args.manage_fixture:
        title, mod_id = '0400000000010000', 'device-check-' + stamp.lower()
        report['fixture'] = {'titleId': title, 'id': mod_id, 'name': 'Moader device check (test)'}
        save()
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('romfs/moader-device-check.txt', b'Moader interface test. No game content.\n')
        payload = archive.getvalue()
        upload = api('upload/begin', 'POST', {**report['fixture'], 'size': len(payload)}, b'', (201,))
        path = 'upload/' + upload['uploadId']
        offset = 0
        for start in range(0, len(payload), 1 << 20):
            part = payload[start:start + (1 << 20)]
            result = api(path, 'PUT', {'offset': offset}, part)
            offset += len(part)
            if result['received'] != offset:
                raise RuntimeError('Upload receipt differs from the sent offset')
        imported = False
        removed = False
        try:
            # Mark the attempted publication before sending: if its reply is lost,
            # finally reads the library before removing only this unique fixture.
            imported = True
            state = acknowledged(api(path, 'POST', body=b'', expected=(201,)), 'disabled import')
            mod = next(m for m in state['mods'] if m['titleId'] == title and m['id'] == mod_id)
            if mod['requested']:
                raise RuntimeError('New fixture was not imported disabled')
            checked('ZIP uploaded and imported disabled on Switch SD')
            for index, enabled in enumerate([1, 0, 1] if args.require_runtime else [1, 0]):
                stage = ('enable', 'disable', 'enable before removal')[index]
                state = acknowledged(api('mod/enable', 'POST', {'titleId': title, 'id': mod_id,
                            'enabled': enabled, 'generation': state['generation']}, b'', (200, 202)), stage)
                mod = next(m for m in state['mods'] if m['titleId'] == title and m['id'] == mod_id)
                if mod['requested'] != bool(enabled):
                    raise RuntimeError('Requested setting did not persist')
                if not state['runtime']['connected'] and mod['applied'] != 'pending':
                    raise RuntimeError('Unavailable runtime was reported as applied')
                if args.require_runtime and mod['applied'] in ('pending', 'error'):
                    raise RuntimeError('Fixture status failed to reflect accepted native routing')
                checked(stage.capitalize() + ' request saved with independent runtime status')
            report['managedState'] = state
            events = api('events')
            if not all(any(prefix in event and mod_id in event for event in events)
                       for prefix in ['Imported disabled:', 'Enabled request:', 'Disabled request:']):
                raise RuntimeError('Expected library debug events are missing')
            checked('Import and toggle debug events visible')
            after = api('mod/remove', 'POST', {'titleId': title, 'id': mod_id,
                        'generation': state['generation']}, b'')
            removed = True
            after = acknowledged(after, 'removal')
        finally:
            if imported and not removed:
                current = api('state')
                if any(m['titleId'] == title and m['id'] == mod_id for m in current['mods']):
                    cleaned = api('mod/remove', 'POST', {'titleId': title, 'id': mod_id,
                                  'generation': current['generation']}, b'')
                    # A broken debug reader must not prevent independently
                    # verifying removal of this run's harmless fixture.
                    acknowledged(cleaned, 'failure cleanup', check_log=False)
                    report['fixtureRemovedAfterFailure'] = True
                    save()
        def inventory(mods):
            fields = ['titleId', 'id', 'name', 'requested', 'fileCount', 'bytes', 'romfs', 'exefs']
            return sorted(tuple(m[field] for field in fields) for m in mods)
        if inventory(after['mods']) != inventory(before['mods']):
            raise RuntimeError('Library after removal differs from the original library')
        checked('Owned fixture removed; original library preserved')
    if args.capture:
        status, data = request('/api/app-screenshot')
        if status != 200 or len(data) < 54 or data[:2] != b'BM':
            raise RuntimeError('Export a native screenshot before requesting the capture check')
        width, height = struct.unpack_from('<ii', data, 18)
        if width != 1280 or abs(height) != 720:
            raise RuntimeError('Native screenshot has an unexpected size')
        capture = folder / ('native-' + stamp + '.bmp')
        capture.write_bytes(data)
        report['capture'] = str(capture.relative_to(ROOT))
        checked('Native 1280x720 screenshot retrieved from Switch')
    report['stateAfter'] = api('state')
    report['eventsAfter'] = api('events')
    report['complete'] = True
    report['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
    print('Evidence:', destination.relative_to(ROOT))
except Exception as error:
    report['error'] = str(error)
    save()
    raise
