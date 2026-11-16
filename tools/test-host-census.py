#!/usr/bin/env python3
"""Local-only receiver, HTTP and capture checks. No console endpoint is used."""
import contextlib
import http.server
import importlib.util
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/host'
BUILD.mkdir(parents=True, exist_ok=True)
FLAGS = ['clang++', '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-pthread', '-I' + str(ROOT / 'include'),
         '-I' + str(ROOT / 'runtime/kaihen/libkaihen/include')]
receiver = BUILD / 'host-census-test'
subprocess.run([*FLAGS, str(ROOT / 'tests/host_census.cpp'), '-o', str(receiver)], check=True)
subprocess.run([str(receiver)], check=True)
web = BUILD / 'host-census-web'
subprocess.run([*FLAGS, str(ROOT / 'tests/host_census_web.cpp'),
                *[str(ROOT / 'source' / f) for f in ['model.cpp', 'library.cpp', 'zip.cpp', 'web.cpp']],
                '-lz', '-o', str(web)], check=True)
TOKEN = '012345'
ROUTE = '/api/diagnostics/host-census'
CLI = ROOT / 'tools/capture-host-census.py'


@contextlib.contextmanager
def listener(root, mode):
    child = subprocess.Popen([str(web), str(root), TOKEN, mode], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        port = int(child.stdout.readline())
        yield 'http://127.0.0.1:' + str(port) + ROUTE
        child.stdin.write('stop\n'); child.stdin.flush()
        assert child.wait(timeout=10) == 0
    finally:
        if child.poll() is None:
            child.terminate(); child.wait(timeout=10)


def request(url, auth=True, method='GET', extra=None):
    headers = {'Authorization': 'Bearer ' + TOKEN} if auth else {}
    headers.update(extra or {})
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers, method=method), timeout=3) as response:
            return response.status, json.loads(response.read()), response.headers
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read()), error.headers


with tempfile.TemporaryDirectory(prefix='moader-census-') as temp:
    work = pathlib.Path(temp).resolve()
    token_file = work / 'token'; token_file.write_text(TOKEN); token_file.chmod(0o600)
    def cli(url, output, *args):
        return subprocess.run([sys.executable, str(CLI), '--endpoint', url, '--output', str(output),
                               '--token-file', str(token_file), '--interval', '.05', '--timeout', '.1',
                               '--count', '2', *args], capture_output=True, text=True, timeout=10)
    with listener(work / 'library', 'cache') as url:
        assert request(url, auth=False)[0] == 401
        assert request(url, extra={'Authorization': 'Bearer 123456'})[0] == 401
        status, data, headers = request(url)
        assert status == 200 and headers['Cache-Control'] == 'no-store'
        assert data['available'] and data['sessionGeneration'] == '1'
        assert data['header']['bootId'] == str(2**64-1)
        assert data['records'][0]['tick'] == str(2**64-2) and data['records'][0]['ownerPid'] == '42'
        assert data['lastRecordFrame'] == data['records'][0]
        assert data['lastRecordBootId'] == data['header']['bootId'] and data['lastRecordGeneration'] == '1'
        assert TOKEN not in json.dumps(data)
        assert request(url, method='POST')[0] == 404
        assert request(url + '?path=anything')[0] == 400
        assert request(url, extra={'Origin': 'http://unrelated.invalid'})[0] == 403
        output = work / 'capture.jsonl'
        result = cli(url, output)
        assert result.returncode == 0 and TOKEN not in result.stdout + result.stderr
        entries = [json.loads(line) for line in output.read_text().splitlines()]
        assert len(entries) == 2 and all(e['listenerState'] == 'responded' and e['census'] == data for e in entries)
        assert stat.S_IMODE(output.stat().st_mode) == 0o600
        original = output.read_bytes(); assert cli(url, output).returncode == 2 and output.read_bytes() == original
        linked = work / 'linked'; linked.symlink_to(output)
        assert cli(url, linked).returncode == 2 and output.read_bytes() == original
        token_file.chmod(0o644)
        assert cli(url, work / 'public-token.jsonl').returncode == 2
        assert not (work / 'public-token.jsonl').exists()
        token_file.chmod(0o600)
        for endpoint in [url + '?code=' + TOKEN, url.replace('http://', 'http://user@'), url.replace(ROUTE, '/api/state')]:
            result = cli(endpoint, work / 'invalid.jsonl')
            assert result.returncode != 0 and TOKEN not in result.stdout + result.stderr
    with listener(work / 'unsupported', 'null') as url:
        assert request(url)[1] == {'schema': 1, 'available': False, 'reason': 'unsupported'}

    # An explicit loopback fixture verifies no redirect following and no replay
    # after failed authentication. It never contacts or discovers a device.
    class Fake(http.server.BaseHTTPRequestHandler):
        paths = []
        mode = 302
        def do_GET(self):
            self.paths.append(self.path)
            if self.mode == 'timeout':
                time.sleep(.3)
                return
            if self.mode == 200:
                body = b'{"schema":1,"available":false,"reason":"no-header"}'
                self.send_response(200); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
            else:
                self.send_response(self.mode); self.send_header('Location', '/unexpected'); self.end_headers()
        def log_message(self, *args): pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fake)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        url = 'http://127.0.0.1:' + str(server.server_port) + ROUTE
        for code in (302, 401, 403, 429, 200):
            Fake.mode = code; Fake.paths = []; target = work / ('http-' + str(code) + '.jsonl')
            result = cli(url, target)
            assert result.returncode == (0 if code == 200 else 2)
            entries = [json.loads(line) for line in target.read_text().splitlines()]
            assert len(entries) == (2 if code == 200 else 1)
            assert Fake.paths == [ROUTE] * len(entries)
            assert TOKEN not in result.stdout + result.stderr
        Fake.mode = 'timeout'; Fake.paths = []; target = work / 'timeout.jsonl'
        result = cli(url, target)
        entries = [json.loads(line) for line in target.read_text().splitlines()]
        assert result.returncode == 0 and len(entries) == 2 and Fake.paths == [ROUTE, ROUTE]
        assert all(e['listenerState'] == 'unknown' and e['error'] == 'transport' for e in entries)
    finally:
        server.shutdown(); server.server_close(); thread.join()
    # A closed, explicitly selected local port is unknown listener state, never
    # evidence that any console process is terminal. Repeated GETs only.
    target = work / 'offline.jsonl'; result = cli(url, target)
    assert result.returncode == 0
    entries = [json.loads(line) for line in target.read_text().splitlines()]
    assert len(entries) == 2 and all(e['listenerState'] == 'unknown' and e['error'] == 'transport' for e in entries)
print('PASS: authenticated cached HTTP export, exact 64-bit JSON, null support, private capture, GET-only/no redirects, and unknown transport state')
