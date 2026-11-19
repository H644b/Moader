#!/usr/bin/env python3
"""Adversarial publication fixtures; no real credentials or device data."""
from pathlib import Path
import contextlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('public_tree', ROOT/'tools/check-public-tree.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class PublicTreeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='moader-public-check-')
        self.root = Path(self.temp.name).resolve()

    def tearDown(self):
        self.temp.cleanup()

    def put(self, name, text='synthetic fixture'):
        path = self.root/name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text); return path

    def test_private_trees_are_rejected_without_reading_contents(self):
        for name in ['state/devices/x', 'evidence/device/x', 'research/dump',
                     'accounts/x', 'credentials/x', 'native/review/capture',
                     'runtime/modloader/state/capture', 'output/raw', 'vendor/sdk']:
            self.put(name)
        issues = checker.scan(self.root)
        self.assertEqual(len(issues), 9)
        self.assertTrue(all(i['reason'] == 'private or generated tree' for i in issues))

    def test_raw_dumps_and_generated_outputs(self):
        for name in ['dump.dmp', 'game.nsp', 'snapshot.log', 'release.zip', 'build/x', 'dist/x']:
            self.put(name)
        self.assertEqual(len(checker.scan(self.root)), 6)

    def test_credentials_never_appear_in_cli_output(self):
        secret = 'ghp_' + 'a' * 36
        self.put('notes.txt', secret)
        result = subprocess.run([sys.executable, str(ROOT/'tools/check-public-tree.py'),str(self.root),'--json'],capture_output=True,text=True)
        self.assertEqual(result.returncode, 1)
        self.assertNotIn(secret, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout), [{'path':'notes.txt','reason':'credential token'}])

    def test_private_addresses_and_user_paths(self):
        values = ['192.'+'168.4.7', '10.'+'2.3.4', '172.'+'20.1.2', 'fd01'+':2345::1', '/Users/'+'private-person/work', '/home/'+'private-person/work']
        for i, value in enumerate(values): self.put(f'notes/{i}.txt',value)
        self.assertEqual(len(checker.scan(self.root)),len(values))

    def test_generic_macos_temp_prefix_is_public_but_a_concrete_path_is_not(self):
        self.put('prefix.txt', "const alias = '/var/folders/'")
        self.assertEqual(checker.scan(self.root), [])
        self.put('actual.txt', '/private/' + 'var/folders/mz/opaque/T/file')
        self.assertEqual(checker.scan(self.root),
                         [{'path': 'actual.txt', 'reason': 'home or temporary user path'}])

    def test_pairing_and_session_are_not_exempt_in_generic_files(self):
        self.put('notes.md','pairingCode = "'+'654321'+'"\n'+'switch2-'+'Abc123')
        reasons = {i['reason'] for i in checker.scan(self.root)}
        self.assertEqual(reasons, {'session identifier','literal credential or pairing value'})

    def test_known_test_codes_require_fixture_context(self):
        self.put('tests/web_fixture.cpp','token = "012345";\nconst char* s="switch2-Test";')
        self.assertEqual(checker.scan(self.root),[])
        self.put('notes.txt','token = "012345"')
        self.assertEqual(checker.scan(self.root),[{'path':'notes.txt','reason':'literal credential or pairing value'}])

    def test_fixture_location_does_not_allow_real_looking_token(self):
        self.put('tests/fixture.cpp','github_pat_'+'a'*40)
        self.assertEqual(checker.scan(self.root)[0]['reason'],'credential token')

    def test_json_pairing_and_authorization_literals(self):
        value='654'+'321'
        self.put('note.json',json.dumps({'pairingCode':value,'Authorization':'Bearer '+value}))
        self.assertEqual({i['reason'] for i in checker.scan(self.root)}, {'literal credential or pairing value','literal bearer credential'})

    def test_hidden_credentials_and_case_variants(self):
        for name in ['.npmrc','.netrc','.DS_Store','STATE/capture','nested/credentials/x']:
            self.put(name)
        self.assertEqual(len(checker.scan(self.root)),5)

    def test_exact_native_and_wrong_code_fixtures(self):
        code='000'+'042';wrong='999'+'999'
        description=code+' is synthetic fixture data, not a console secret'
        self.put('native/ui_control_test.cpp',f'token="{code}";')
        self.put('native/ui_capture_convert.py',repr({'pairing_code':description}))
        self.put('tools/test-web.py','Authorization: Bearer '+wrong)
        self.assertEqual(checker.scan(self.root),[])
        self.put('tests/unrelated.cpp',f'token="{code}";\nAuthorization: Bearer {wrong}')
        self.assertEqual({i['reason'] for i in checker.scan(self.root)}, {'literal credential or pairing value','literal bearer credential'})
        self.put('native/ui_control_test.cpp','token="'+'000'+'043'+'";')
        self.assertIn({'path':'native/ui_control_test.cpp','reason':'literal credential or pairing value'},checker.scan(self.root))

    def test_exact_qa_sessions_and_markdown_placeholder(self):
        qa='switch2-'+'nativeqa';setup='switch2-'+'AbC123';placeholder='switch2-'+'SESSION'
        self.put('runtime/modloader/native/verify.py',qa)
        self.put('tools/test-setup-backend.py',setup)
        self.put('runtime/modloader/README.md',placeholder)
        self.assertEqual(checker.scan(self.root),[])
        self.put('tests/unrelated.py',qa+' '+setup+' '+placeholder)
        self.assertEqual(checker.scan(self.root),[{'path':'tests/unrelated.py','reason':'session identifier'}])

    def test_capture_description_allowance_is_exact(self):
        description='000'+'042 is synthetic fixture data, not a console secret'
        self.put('native/ui_capture_convert.py',repr({'pairing_code':description+' CHANGED'}))
        self.assertEqual(checker.scan(self.root),[{'path':'native/ui_capture_convert.py','reason':'literal credential or pairing value'}])

    def test_documentation_placeholders_public_urls_and_licenses(self):
        self.put('README.md','pairingCode = "<PAIRING_CODE>"\nhttps://github.com/H644b/Moader\nhttp://192.0.2.1\nhttp://127.0.0.1\nhttps://spdx.org/licenses/GPL-2.0-only.html\nRFC1918: '+ '192.'+'168.0.0/16')
        self.put('native/third_party/LICENSE','Copyright upstream. MIT license.')
        self.put('.env.example','api_key="<YOUR_TOKEN>"')
        self.assertEqual(checker.scan(self.root),[])

    def test_symlinks_and_hardlinks_are_not_followed(self):
        real=self.put('plain.txt'); (self.root/'link').symlink_to(real)
        os.link(real,self.root/'hardlink')
        reasons=[i['reason'] for i in checker.scan(self.root)]
        self.assertEqual(reasons.count('symbolic link'),1)
        self.assertEqual(reasons.count('hard-linked file'),2)

    def test_git_metadata_ignored_and_output_is_relative(self):
        self.put('.git/config','private metadata deliberately not scanned')
        self.put('note.txt','ghp_'+'b'*36)
        self.assertEqual(checker.scan(self.root),[{'path':'note.txt','reason':'credential token'}])

    def test_private_key_and_cookie_values(self):
        self.put('note.txt','-----BEGIN '+'PRIVATE KEY-----\n'+'.ROBLOSECURITY'+'='+('z'*32))
        self.assertEqual({i['reason'] for i in checker.scan(self.root)}, {'private key material','cookie value'})

    def test_checker_and_its_tests_do_not_require_broad_exemptions(self):
        self.put('tools/check-public-tree.py',(ROOT/'tools/check-public-tree.py').read_text())
        self.put('.github/tests/test_public_tree.py',Path(__file__).read_text())
        self.assertEqual(checker.scan(self.root),[])


if __name__ == '__main__':
    unittest.main()
