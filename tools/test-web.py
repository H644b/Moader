#!/usr/bin/env python3
import io, json, os, pathlib, socket, subprocess, sys, tempfile, time, urllib.error, urllib.parse, urllib.request, zipfile
ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/host'
BUILD.mkdir(parents=True, exist_ok=True)
TOKEN = '012345'
binary = BUILD / 'web-host'
subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I' + str(ROOT / 'include'),
                str(ROOT / 'tests/web_host.cpp'), *[str(ROOT / 'source' / f) for f in ['model.cpp', 'library.cpp', 'zip.cpp', 'web.cpp']],
                '-lz', '-o', str(binary)], check=True)
with tempfile.TemporaryDirectory(prefix='moader-web-') as temp:
    work = pathlib.Path(temp).resolve()
    icon = work / 'fixture-icon'
    child = subprocess.Popen([str(binary), str(work / 'library'), TOKEN, str(icon)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        port = int(child.stdout.readline())
        base = 'http://127.0.0.1:' + str(port)
        def request(path, method='GET', body=None, auth=True, headers=None):
            h = {'Authorization': 'Bearer ' + TOKEN} if auth else {}
            h.update(headers or {})
            q = urllib.request.Request(base + path, data=body, method=method, headers=h)
            try:
                with urllib.request.urlopen(q, timeout=5) as r: return r.status, r.read()
            except urllib.error.HTTPError as e: return e.code, e.read()
        assert request('/', auth=False)[0] == 200
        assert request('/api/state', auth=False)[0] == 401
        for wrong in ['12345', '0123450', '01234a', 'Bearer 012345']:
            assert request('/api/state', headers={'Authorization': 'Bearer ' + wrong})[0] == 401
            # Successful pairing clears partial failures without losing zero.
            assert request('/api/state')[0] == 200
        assert request('/api/state', headers={'Origin': 'http://unrelated.invalid'})[0] == 403
        status, body = request('/api/state')
        assert status == 200 and not json.loads(body)['runtime']['connected']
        assert request('/api/settings', auth=False)[0] == 401
        art = '/api/game-art?titleId=0400F43008C44001'
        assert request(art, auth=False)[0] == 401
        assert request('/api/game-art?titleId=bad')[0] == 400
        assert request(art)[0] == 404
        assert request('/api/game-name?titleId=0400f43008c44001', auth=False)[0] == 401
        assert request('/api/game-name?titleId=not-an-id')[0] == 400
        assert json.loads(request('/api/game-name?titleId=0400F43008C44001')[1]) == {'titleId': '0400f43008c44001', 'name': None}
        assert request('/api/settings', headers={'Origin': 'http://unrelated.invalid'})[0] == 403
        defaults = json.loads(request('/api/settings')[1])
        assert defaults == {'gameNames': True, 'reducedMotion': False, 'webOnStart': False, 'webPort': 7712, 'followLog': True}
        settings_query = urllib.parse.urlencode({'gameNames': '0', 'reducedMotion': '1', 'webOnStart': '1', 'webPort': '8721', 'followLog': '0'})
        status, body = request('/api/settings?' + settings_query, 'POST', b'')
        saved = json.loads(body)
        assert status == 200 and saved == {'gameNames': False, 'reducedMotion': True, 'webOnStart': True, 'webPort': 8721, 'followLog': False}
        assert json.loads(request('/api/state')[1])['settings'] == saved
        assert json.loads(request('/api/state')[1])['generation'] == 0
        for bad in ['webPort=7713', 'webPort=0', 'webPort=65536', 'gameNames=true', 'unknown=1', 'gameNames=1&webPort=1']:
            assert request('/api/settings?' + bad, 'POST', b'')[0] == 400
            assert json.loads(request('/api/settings')[1]) == saved
        assert request('/api/settings?gameNames=1', 'POST', b'')[0] == 200
        assert json.loads(request('/api/settings')[1])['webPort'] == 8721
        # Read-only screenshot contract: fixed file, authenticated, same origin,
        # regular and without links, at most 4 MiB. No caller-selected paths.
        screenshot = work / 'library/app-screenshot.bmp'
        assert request('/api/app-screenshot', auth=False)[0] == 401
        status, body = request('/api/app-screenshot')
        assert status == 404 and 'No app screenshot' in json.loads(body)['error']
        fixture = b'BM' + bytes(range(256))
        screenshot.write_bytes(fixture)
        assert request('/api/app-screenshot', headers={'Origin': 'http://unrelated.invalid'})[0] == 403
        with urllib.request.urlopen(urllib.request.Request(base + '/api/app-screenshot', headers={'Authorization': 'Bearer ' + TOKEN}), timeout=5) as response:
            assert response.status == 200 and response.headers['Content-Type'] == 'image/bmp'
            assert response.read() == fixture
        for path in ['/api/app-screenshot?path=../pairing-code', '/api/app-screenshot?file=other.bmp']:
            assert request(path)[0] == 400
        for path in ['/api/app-screenshot/../pairing-code', '/api/app-screenshot%2f..%2fpairing-code']:
            assert request(path)[0] == 404
        assert request('/api/app-screenshot', 'POST', b'')[0] == 404
        screenshot.unlink()
        screenshot.mkdir()
        assert request('/api/app-screenshot')[0] == 400
        screenshot.rmdir()
        os.mkfifo(screenshot)
        assert request('/api/app-screenshot')[0] == 400
        screenshot.unlink()
        outside = work / 'other.bmp'
        outside.write_bytes(fixture)
        screenshot.symlink_to(outside)
        assert request('/api/app-screenshot')[0] == 400
        screenshot.unlink()
        os.link(outside, screenshot)
        assert request('/api/app-screenshot')[0] == 400
        screenshot.unlink()
        with screenshot.open('wb') as stream:
            stream.truncate((4 << 20) + 1)
        assert request('/api/app-screenshot')[0] == 400
        with screenshot.open('wb') as stream:
            stream.truncate(4 << 20)
        status, body = request('/api/app-screenshot')
        assert status == 200 and len(body) == 4 << 20
        screenshot.unlink()
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w', zipfile.ZIP_DEFLATED) as z: z.writestr('romfs/config.txt', 'integration fixture')
        payload = data.getvalue()
        params = urllib.parse.urlencode({'titleId': '0400f43008c44001', 'id': 'web-upload', 'name': 'Web upload', 'size': len(payload)})
        status, body = request('/api/upload/begin?' + params, 'POST', b'')
        assert status == 201, body
        key = json.loads(body)['uploadId']
        route = '/api/upload/' + key
        assert request(route + '?offset=1', 'PUT', payload)[0] == 400
        status, body = request(route + '?offset=0', 'PUT', payload)
        assert status == 200 and json.loads(body)['received'] == len(payload)
        assert request(route, 'POST', b'')[0] == 201
        for name, mime in [('hbmenu.jpg', 'image/jpeg'), ('mark-alpha.png', 'image/png')]:
            expected = (ROOT / 'branding' / name).read_bytes()
            icon.write_bytes(expected)
            with urllib.request.urlopen(urllib.request.Request(base + art, headers={'Authorization': 'Bearer ' + TOKEN}), timeout=5) as response:
                assert response.status == 200 and response.headers['Content-Type'] == mime
                assert response.read() == expected
        icon.write_bytes(b'unknown format')
        assert request(art)[0] == 404
        icon.write_bytes(b'\xff\xd8\xff' + bytes(0x20000))
        assert request(art)[0] == 404
        icon.unlink()
        status, body = request(art)
        assert status == 404 and json.loads(body)['error'] == 'Fixture icon unavailable'
        toggle = urllib.parse.urlencode({'titleId': '0400f43008c44001', 'id': 'web-upload', 'enabled': '1'})
        status, body = request('/api/mod/enable?' + toggle, 'POST', b'')
        assert status == 202 and json.loads(body)['mods'][0]['applied'] == 'pending'
        remove = urllib.parse.urlencode({'titleId': '0400f43008c44001', 'id': 'web-upload'})
        assert request('/api/mod/remove?' + remove, 'POST', b'')[0] == 200
        assert request(art)[0] == 404
        assert json.loads(request('/api/state')[1])['mods'] == []
        # State polling is cached. Only an authenticated rescan inspects an
        # external SD copy, and a failed scan retains that complete inventory.
        assert request('/api/library/rescan', 'POST', b'', auth=False)[0] == 401
        assert request('/api/library/rescan', 'POST', b'', headers={'Origin': 'http://unrelated.invalid'})[0] == 403
        external = work / 'library/library/0400f43008c44001/external/romfs'
        external.mkdir(parents=True)
        (external / 'fixture').write_bytes(b'external fixture')
        assert json.loads(request('/api/state')[1])['mods'] == []
        status, body = request('/api/library/rescan', 'POST', b'')
        complete = json.loads(body)
        assert status == 200 and len(complete['mods']) == 1 and complete['inventory'] == {'ready': True, 'error': ''}
        assert request('/api/game-source?titleId=0400f43008c44001&source=cartridge', 'POST', b'', auth=False)[0] == 401
        assert request('/api/game-source?titleId=0400f43008c44001&source=cartridge', 'POST', b'', headers={'Origin': 'http://unrelated.invalid'})[0] == 403
        for bad_source in ['physical', '', '3']:
            assert request('/api/game-source?' + urllib.parse.urlencode({'titleId': '0400f43008c44001', 'source': bad_source}), 'POST', b'')[0] == 400
        assert request('/api/game-source?titleId=0400f43008c44001&source=digital&unexpected=1', 'POST', b'')[0] == 400
        generation = complete['generation']
        status, body = request('/api/game-source?titleId=0400f43008c44001&source=cartridge&generation=' + str(generation), 'POST', b'')
        complete = json.loads(body)
        assert status == 200 and complete['mods'][0]['gameSource'] == 'cartridge' and complete['generation'] > generation
        assert request('/api/game-source?titleId=0400f43008c44001&source=digital&generation=' + str(generation), 'POST', b'')[0] == 400
        contents = work / 'library/library'
        offline = work / 'unavailable-library'
        contents.rename(offline)
        assert request('/api/library/rescan', 'POST', b'')[0] == 400
        retained = json.loads(request('/api/state')[1])
        assert retained['mods'] == complete['mods'] and retained['generation'] == complete['generation']
        assert retained['inventory']['ready'] and retained['inventory']['error']
        offline.rename(contents)
        status, body = request('/api/library/rescan', 'POST', b'')
        assert status == 200 and json.loads(body)['inventory'] == {'ready': True, 'error': ''}
        assert request('/api/unknown')[0] == 404
        assert request('/api/mod/enable?enabled=2', 'POST', b'')[0] == 400
        for raw in [
            b'POST /api/state HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nContent-Length: 1\r\n\r\n',
            b'POST /api/state HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n',
            b'POST /api/state HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1048577\r\n\r\n',
        ]:
            with socket.create_connection(('127.0.0.1', port), timeout=5) as s:
                s.sendall(raw)
                assert s.recv(4096).startswith(b'HTTP/1.1 400')
        for _ in range(5):
            assert request('/api/state', headers={'Authorization': 'Bearer 999999'})[0] == 401
        assert request('/api/state')[0] == 429
        assert request('/', auth=False)[0] == 200
        child.stdin.write('restart\n'); child.stdin.flush()
        assert int(child.stdout.readline()) == port
        assert request('/api/state')[0] == 200
        # A partial request must not hold stop/restart until the five-second
        # receive timeout. Restart also verifies port reuse and error recovery.
        for _ in range(10):
            with socket.create_connection(('127.0.0.1', port), timeout=2) as idle:
                idle.sendall(b'GET /api/state HTTP/1.1\r\n')
                started = time.monotonic()
                child.stdin.write('restart\n'); child.stdin.flush()
                assert int(child.stdout.readline()) == port
                assert time.monotonic() - started < 2
            assert request('/api/state')[0] == 200
        child.stdin.write('stop\n'); child.stdin.flush()
        assert child.wait(timeout=10) == 0
        print('PASS: six-digit pairing, origin checks, bounded read-only screenshot, chunk upload, management API, malformed requests, lockout, idle-client shutdown and restart', flush=True)
    finally:
        if child.poll() is None: child.terminate(); child.wait(timeout=10)
subprocess.run([sys.executable, str(ROOT / 'tools/test-pairing.py')], check=True)
