#!/usr/bin/env python3
"""Deterministic host-only pairing and FTP publication regressions; no network."""
import contextlib
import ftplib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('moader_install_ftp', ROOT / 'tools/install-ftp.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

# Include the actual implementation so boundary times and 64 distinct peers can
# be exercised without production clock overrides, sleeps or 64 real hosts.
LIMITER_TEST = r'''
#include "source/web.cpp"
#include <cassert>
int main() {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;
    const auto now = Clock::time_point{};
    moader::PairingLimiter limiter;
    for (unsigned i = 0; i < 5; ++i) assert(limiter.authenticate(1, false, now) == 401);
    assert(limiter.authenticate(1, false, now + seconds(29)) == 429);
    assert(limiter.authenticate(1, true, now + seconds(30) - nanoseconds(1)) == 429);
    assert(limiter.authenticate(2, true, now) == 200); // peer isolation
    assert(limiter.authenticate(1, true, now + seconds(30)) == 200);
    for (unsigned i = 0; i < 4; ++i) assert(limiter.authenticate(1, false, now) == 401);
    assert(limiter.authenticate(1, true, now) == 200);
    for (unsigned i = 0; i < 5; ++i) assert(limiter.authenticate(1, false, now) == 401);
    assert(limiter.authenticate(1, true, now) == 429);
    limiter.clear();
    for (unsigned peer = 0; peer < 64; ++peer)
        for (unsigned i = 0; i < 5; ++i) assert(limiter.authenticate(peer, false, now) == 401);
    for (unsigned peer = 64; peer < 1024; ++peer) assert(limiter.authenticate(peer, false, now) == 429);
    assert(limiter.authenticate(0, true, now) == 429); // no eviction
    assert(limiter.authenticate(1024, true, now) == 200); // correct code needs no entry
    assert(limiter.authenticate(1024, false, now + seconds(30)) == 401); // expired capacity reusable
    limiter.clear();
    assert(limiter.authenticate(0, true, now) == 200);
    const auto root = std::string(TEST_ROOT) + "/library";
    moader::UnavailableBackend backend;
    moader::Library library(root, backend);
    for (const auto* code : {"", "12345", "1234567", "abcdef", "01234 ", "01234\n", "old-long-token-value"}) {
        bool rejected = false;
        try { moader::WebServer server(library, root, code, {}); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    }
    for (const auto* code : {"000000", "012345", "999999"}) {
        moader::WebServer server(library, root, code, {});
        assert(!server.running() && server.port() == 0);
    }
}
'''


class MemoryFTP:
    """Strict fake FTP server, deliberately unable to open a network socket."""
    def __init__(self):
        self.dirs = {'/sdmc:', '/sdmc:/switch', '/sdmc:/kaihen'}
        self.files = {}
        self.sessions = []
        self.operations = []
        self.fault = None

    def session(self):
        server = self

        class Session:
            closed = False
            directory = None

            def connect(self, host, port, timeout):
                server.operations.append(('connect', host, port))

            def login(self, user, password):
                assert user == 'anonymous' and password == 'moader'

            def cwd(self, directory):
                if directory not in server.dirs:
                    raise ftplib.error_perm('550 No directory')
                self.directory = directory

            def pwd(self):
                return '/wrong-device' if server.fault == 'wrong-directory' else self.directory

            def retrlines(self, command, callback):
                assert command == 'LIST'
                prefix = self.directory + '/'
                names = {p[len(prefix):] for p in server.dirs | set(server.files)
                         if p.startswith(prefix) and '/' not in p[len(prefix):]}
                for name in sorted(names):
                    callback('-rw-r--r-- 1 owner group 1 Jan 01 00:00 ' + name)

            def mkd(self, name):
                server.operations.append(('mkdir', self.directory + '/' + name))
                server.dirs.add(self.directory + '/' + name)

            def retrbinary(self, command, callback):
                assert command.startswith('RETR ')
                name = command[5:]
                path = self.directory + '/' + name
                data = server.files[path]
                server.operations.append(('read', path))
                if server.fault in ('stage-corrupt', 'stage-oversize') and '.pending-' in name:
                    data = data + b'!' if server.fault == 'stage-oversize' else bytes([data[0] ^ 1]) + data[1:]
                if server.fault == 'final-corrupt' and name == 'Moader.nro':
                    if data == b'new-app-data':
                        data = b'X' + data[1:]
                for at in range(0, len(data), 3):
                    callback(data[at:at + 3])

            def storbinary(self, command, stream):
                assert command.startswith('STOR ')
                name = command[5:]
                path = self.directory + '/' + name
                data = stream.read()
                server.operations.append(('write', path))
                if server.fault == 'partial-final' and name == 'Moader.nro':
                    server.files[path] = data[:2]
                    raise OSError('simulated transfer failure')
                server.files[path] = data

            def rename(self, old, new):
                if server.fault in ('rename-unsupported', 'partial-final'):
                    raise ftplib.error_perm('502 Rename unsupported')
                source, target = self.directory + '/' + old, self.directory + '/' + new
                server.operations.append(('rename', source, target))
                if target in server.files:
                    raise ftplib.error_perm('550 Destination exists')
                server.files[target] = server.files.pop(source)

            def voidcmd(self, command):
                assert command.startswith('DELE ')
                path = self.directory + '/' + command[5:]
                server.operations.append(('delete', path))
                del server.files[path]

            def close(self):
                self.closed = True

        connection = Session()
        self.sessions.append(connection)
        return connection


class FTPTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='moader-pairing-ftp-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'dist').mkdir()
        (self.root / 'dist/Moader.nro').write_bytes(b'new-app-data')
        (self.root / 'dist/moader.nso').write_bytes(b'new-plugin-data')
        self.server = MemoryFTP()
        self.server.files['/sdmc:/switch/Moader.nro'] = b'previous-app-data'
        self.tool = installer.Installer('192.0.2.10', root=self.root, ftp_factory=self.server.session)
        self.output = io.StringIO()
        self.addCleanup(self.assert_connections_closed)

    def assert_connections_closed(self):
        self.assertTrue(all(session.closed for session in self.server.sessions))

    def run_install(self, random=42):
        with patch.object(installer.secrets, 'randbelow', return_value=random), contextlib.redirect_stdout(self.output):
            self.tool.run()

    def add_code(self, code):
        self.server.dirs.add('/sdmc:/moader')
        self.server.files['/sdmc:/moader/pairing-code'] = code

    def assert_local_code(self, code):
        path = self.root / 'state/devices/192.0.2.10/pairing-code'
        self.assertEqual(path.read_bytes(), code)
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertNotIn(code.decode(), self.output.getvalue())

    def test_fresh_install_seeds_zero_and_verifies_both_artifacts(self):
        self.run_install(random=0)
        self.assert_local_code(b'000000')
        self.assertEqual(self.server.files['/sdmc:/moader/pairing-code'], b'000000')
        self.assertTrue(self.tool.record['complete'])
        self.assertFalse(self.tool.record['pluginStarted'])
        self.assertFalse(self.tool.record['runningProcessesPatched'])
        self.assertEqual(len(self.tool.record['files']), 3)
        for item in self.tool.record['files']:
            self.assertTrue(item['stagedVerified'] and item['verified'])
        app = self.tool.record['files'][0]
        self.assertEqual(Path(app['previousBackup']).read_bytes(), b'previous-app-data')
        self.assertEqual(self.server.files[app['remoteBackup']], b'previous-app-data')
        self.assertEqual(self.server.files['/sdmc:/switch/Moader.nro'], b'new-app-data')
        self.assertEqual(self.server.files['/sdmc:/kaihen/plugins/moader.nso'], b'new-plugin-data')

    def test_existing_numeric_code_is_retained_with_leading_zeros(self):
        self.add_code(b'000042\n')
        self.run_install(random=999999)
        self.assert_local_code(b'000042')
        self.assertEqual(self.server.files['/sdmc:/moader/pairing-code'], b'000042\n')
        self.assertNotIn('pairingCodeSeeded', self.tool.record)
        self.assertFalse(any(op[0] == 'write' and 'pairing-code' in op[1] for op in self.server.operations))

    def test_legacy_code_migrates_with_private_backup_and_no_journal_digest(self):
        self.add_code(b'old-long-token-' * 3)
        self.run_install()
        self.assert_local_code(b'000042')
        item = self.tool.record['files'][-1]
        self.assertNotIn('sha256', item)
        backup = Path(item['previousBackup'])
        self.assertEqual(backup.read_bytes(), b'old-long-token-' * 3)
        self.assertEqual(backup.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.server.files[item['remoteBackup']], backup.read_bytes())
        journal = self.tool.journal.read_text()
        self.assertNotIn('000042', journal)
        self.assertNotIn(installer.hashlib.sha256(b'000042').hexdigest(), journal)

    def test_whitespace_is_migrated_instead_of_stripped_to_a_different_native_code(self):
        self.add_code(b' 012345 \n')
        self.run_install()
        self.assert_local_code(b'000042')
        self.assertEqual(self.server.files['/sdmc:/moader/pairing-code'], b'000042')
        self.assertTrue(self.tool.record['pairingCodeSeeded'])

    def test_pairing_stage_failure_preserves_old_remote_and_local_material(self):
        previous = b'old-long-token-value'
        self.add_code(previous)
        local = self.root / 'state/devices/192.0.2.10/pairing-code'
        installer.private_write(local, b'111111')
        self.server.fault = 'stage-corrupt'
        with patch.object(installer.secrets, 'randbelow', return_value=42):
            with self.assertRaisesRegex(RuntimeError, 'Staged FTP bytes differ'):
                self.tool.pairing_code()
        self.assertEqual(self.server.files['/sdmc:/moader/pairing-code'], previous)
        self.assertEqual(local.read_bytes(), b'111111')
        item = self.tool.record['files'][0]
        self.assertEqual(Path(item['previousBackup']).read_bytes(), previous)
        self.assertFalse(item['verified'] or item['stagedVerified'])

    def test_corrupt_staging_never_publishes(self):
        self.server.fault = 'stage-corrupt'
        with self.assertRaisesRegex(RuntimeError, 'Staged FTP bytes differ'):
            self.run_install()
        self.assertEqual(self.server.files['/sdmc:/switch/Moader.nro'], b'previous-app-data')
        self.assertFalse(self.tool.record['files'][0]['stagedVerified'])
        self.assertFalse(self.tool.record['complete'])
        self.assertIn('error', json.loads(self.tool.journal.read_text()))

    def test_oversized_readback_is_bounded(self):
        self.server.fault = 'stage-oversize'
        with self.assertRaisesRegex(RuntimeError, 'exceeded its expected bound'):
            self.run_install()
        self.assertFalse(self.tool.record['complete'])
        self.assertEqual(self.server.files['/sdmc:/switch/Moader.nro'], b'previous-app-data')

    def test_final_readback_failure_retains_original_backup(self):
        self.server.fault = 'final-corrupt'
        with self.assertRaisesRegex(RuntimeError, 'Final FTP bytes differ'):
            self.run_install()
        item = self.tool.record['files'][0]
        self.assertTrue(item['stagedVerified'])
        self.assertFalse(item['verified'])
        self.assertEqual(Path(item['previousBackup']).read_bytes(), b'previous-app-data')
        self.assertFalse(self.tool.record['complete'])

    def test_rename_fallback_is_journaled_and_read_back(self):
        self.server.fault = 'rename-unsupported'
        self.run_install()
        for item in self.tool.record['files']:
            self.assertIn('renameFallback', item)
            self.assertTrue(item['stagedVerified'] and item['verified'])
        self.assertEqual(Path(self.tool.record['files'][0]['previousBackup']).read_bytes(), b'previous-app-data')
        self.assertFalse(any('.pending-' in path for path in self.server.files))

    def test_failed_fallback_is_never_complete_and_keeps_backup(self):
        self.server.fault = 'partial-final'
        with self.assertRaisesRegex(OSError, 'simulated transfer failure'):
            self.run_install()
        item = self.tool.record['files'][0]
        self.assertIn('renameFallback', item)
        self.assertEqual(Path(item['previousBackup']).read_bytes(), b'previous-app-data')
        self.assertFalse(item['verified'] or self.tool.record['complete'])

    def test_wrong_sd_directory_is_rejected_before_writes(self):
        self.server.fault = 'wrong-directory'
        with self.assertRaisesRegex(RuntimeError, 'requested SD directory'):
            self.run_install()
        self.assertFalse(any(op[0] in ('write', 'rename', 'mkdir') for op in self.server.operations))

    def test_missing_artifact_fails_before_any_ftp_connection(self):
        (self.root / 'dist/moader.nso').unlink()
        with self.assertRaises(FileNotFoundError):
            self.run_install()
        self.assertEqual(self.server.operations, [])
        self.assertFalse(self.tool.record['complete'])

    def test_empty_artifact_fails_before_any_ftp_connection(self):
        (self.root / 'dist/moader.nso').write_bytes(b'')
        with self.assertRaisesRegex(RuntimeError, 'must be nonempty'):
            self.run_install()
        self.assertEqual(self.server.operations, [])

    def test_numeric_validation_requires_six_ascii_digits(self):
        for code in [b'000000', b'012345', b'999999']:
            self.assertTrue(installer.valid_code(code))
        for code in [b'', b'12345', b'0123456', b'01234x', b'01234 ', b'01234\n', '１２３４５６'.encode()]:
            self.assertFalse(installer.valid_code(code))
        for port in (0, 65536):
            with self.assertRaises(ValueError):
                installer.Installer('192.0.2.10', port)
        with self.assertRaises(ValueError):
            installer.Installer('untrusted-hostname')


def main():
    with tempfile.TemporaryDirectory(prefix='moader-pairing-unit-') as temp:
        source = Path(temp) / 'pairing.cpp'
        source.write_text(LIMITER_TEST)
        binary = Path(temp) / 'pairing-test'
        subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I' + str(ROOT), '-I' + str(ROOT / 'include'),
                        '-DTEST_ROOT=' + json.dumps(str(Path(temp).resolve())), str(source),
                        *[str(ROOT / 'source' / name) for name in ('model.cpp', 'library.cpp', 'zip.cpp')],
                        '-lz', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: deterministic pairing windows, peer isolation, 64-entry saturation, expiry and strict six-digit codes', flush=True)
    unittest.main(argv=['test-pairing.py'], verbosity=2)


if __name__ == '__main__':
    main()
