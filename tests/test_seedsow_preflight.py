import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('seedsow_preflight', ROOT/'tools/seedsow-preflight.py')
preflight = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preflight)


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.profile = json.loads((ROOT/'config/seedsow-provider.json').read_text())
        self.rows = preflight.plan(self.profile)
        self.nonce = 'a'*32

    def body(self, rows=None, nonce=None, mismatch=False):
        rows, nonce = rows or self.rows, nonce or self.nonce
        lines = ['status: ok', 'output:', 'MOADER_PREFLIGHT '+nonce,
                 'BASE sdk '+str(2**32), 'BASE main '+str(2**33)]
        for row in rows:
            value = row.get('bytes', '00'*row['length'])
            if mismatch and row is rows[0]:
                value = ('00' if value[:2] != '00' else 'ff')+value[2:]
            lines.append('CODE '+row['name']+' '+value)
        lines.append('MOADER_PREFLIGHT_END '+nonce)
        return ('\n'.join(lines)+'\n').encode()

    def test_real_plan_and_safe_read_emitter(self):
        self.assertEqual(len(self.rows), 13)
        source = preflight.lua_source(self.rows, self.nonce)
        self.assertIn('E:peek(address+at,dst+at)', source)
        for forbidden in ['N.peek(', 'N.poke(', 'N.call_deep(', 'N.alloc(', 'write_bytes', 'dofile(', 'io.open(']:
            self.assertNotIn(forbidden, source)
        self.assertEqual(next(r for r in self.rows if r['name']=='worker-vcall-restore')['mapping'], 'sdk')

    def test_unknown_samples_never_become_qualification(self):
        result = preflight.parse_response(self.body(), self.rows, self.nonce)
        self.assertTrue(result['allExistingSignaturesMatch'])
        self.assertTrue(result['allRequiredPinnedSignaturesMatch'])
        self.assertFalse(result['requiredPrimitiveBaselinesComplete'])
        self.assertFalse(result['qualificationComplete'])
        self.assertEqual(sum(s['matchesPinnedBytes'] is None for s in result['samples']), 7)

    def test_actual_signature_mismatch_is_retained(self):
        result = preflight.parse_response(self.body(mismatch=True), self.rows, self.nonce)
        self.assertFalse(result['allExistingSignaturesMatch'])
        self.assertFalse(result['qualificationComplete'])

    def test_nonrequired_try_wait_drift_does_not_hide_required_signature_state(self):
        rows = list(self.rows)
        target = next(row for row in rows if row['name'] == 'pthread-try-wait')
        lines = self.body(rows=rows).decode().splitlines()
        prefix = 'CODE ' + target['name'] + ' '
        for index, line in enumerate(lines):
            if line.startswith(prefix):
                value = line[len(prefix):]
                lines[index] = prefix + ('00' if value[:2] != '00' else 'ff') + value[2:]
                break
        result = preflight.parse_response(('\n'.join(lines) + '\n').encode(), rows, self.nonce)
        self.assertFalse(result['allExistingSignaturesMatch'])
        self.assertTrue(result['allRequiredPinnedSignaturesMatch'])
        self.assertFalse(result['requiredPrimitiveBaselinesComplete'])
        sample = next(item for item in result['samples'] if item['name'] == target['name'])
        self.assertFalse(sample['requiredForNativeHandoff'])
        self.assertFalse(sample['matchesPinnedBytes'])
        self.assertFalse(result['qualificationComplete'])

    def test_stale_repeated_partial_and_extra_data_reject(self):
        body = self.body()
        cases = [body.replace(self.nonce.encode(), b'b'*32), body+('MOADER_PREFLIGHT '+self.nonce+'\n').encode(),
                 body.replace(b'BASE main '+str(2**33).encode()+b'\n', b''),
                 body.replace(b'MOADER_PREFLIGHT_END', b'CODE unexpected aa\nMOADER_PREFLIGHT_END'),
                 body.replace(b'status: ok', b'status: error'), b'x'*(preflight.LIMIT+1)]
        for value in cases:
            with self.subTest(size=len(value)), self.assertRaises(ValueError):
                preflight.parse_response(value, self.rows, self.nonce)

    def test_capture_is_one_request_and_report_stays_unqualified(self):
        calls = []
        owner = self
        class Reply:
            status = 200
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, limit):
                nonce = re.search(r'print\("MOADER_PREFLIGHT ([a-f0-9]+)"\)', calls[0].data.decode())[1]
                return owner.body(nonce=nonce)
        def request(value, timeout):
            calls.append(value)
            self.assertEqual(timeout, 30)
            return Reply()
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)/'new'
            result = preflight.capture('198.51.100.5', 8080, output, self.profile, request)
            self.assertEqual(len(calls), 1)
            self.assertFalse(result['fsAcquisitionAttempted'])
            self.assertFalse(result['codeOrKernelPatchAttempted'])
            self.assertFalse(result['nativeHandoffReady'])
            self.assertEqual((output/'preflight.json').stat().st_mode & 0o777, 0o600)
            with self.assertRaises(ValueError):
                preflight.capture('198.51.100.5', 8080, output, self.profile, request)
            self.assertEqual(len(calls), 1)

    def test_transport_failure_records_without_retry(self):
        calls = []
        def fail(*args, **kwargs):
            calls.append(args)
            raise TimeoutError('synthetic preflight timeout')
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)/'new'
            with self.assertRaises(TimeoutError):
                preflight.capture('198.51.100.5', 8080, output, self.profile, fail)
            result = json.loads((output/'preflight.json').read_text())
            self.assertEqual(len(calls), 1)
            self.assertIn('error', result)
            self.assertFalse(result['complete'])

    def test_redirect_is_not_followed(self):
        with self.assertRaisesRegex(ValueError, 'not replayed'):
            preflight.NoRedirect().redirect_request(None, None, 302, 'redirect', {}, 'https://example.com')

    def test_existing_capture_reclassification_is_read_only_and_stays_unqualified(self):
        parsed = preflight.parse_response(self.body(), self.rows, self.nonce)
        report = {'schema': 1, 'provider': self.profile['id'], 'revision': self.profile['revision'],
                  'complete': True, 'fsAcquisitionAttempted': False,
                  'codeOrKernelPatchAttempted': False, 'nativeHandoffReady': False,
                  **parsed}
        target = next(sample for sample in report['samples'] if sample['name'] == 'pthread-try-wait')
        target['bytes'] = ('00' if target['bytes'][:2] != '00' else 'ff') + target['bytes'][2:]
        target['sha256'] = preflight.hashlib.sha256(bytes.fromhex(target['bytes'])).hexdigest()
        result = preflight.review_report(report, self.profile)
        self.assertFalse(result['consoleConnected'])
        self.assertFalse(result['allExistingSignaturesMatch'])
        self.assertTrue(result['allRequiredPinnedSignaturesMatch'])
        self.assertFalse(result['requiredPrimitiveBaselinesComplete'])
        self.assertFalse(result['qualificationComplete'])
        report['samples'][0]['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'digest differs'):
            preflight.review_report(report, self.profile)


if __name__ == '__main__':
    unittest.main()
