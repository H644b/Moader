#!/usr/bin/env python3
"""Test pinned-input and extraction boundaries using tiny synthetic archives."""
from pathlib import Path
import hashlib
import importlib.util
import io
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('host_deps', ROOT/'.github/scripts/prepare-host-deps.py')
deps = importlib.util.module_from_spec(spec); spec.loader.exec_module(deps)


class DependencyTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='moader-deps-test-')
        self.root=Path(self.temp.name).resolve();self.out=self.root/'checkout';self.out.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def tar(self, members):
        file=self.root/'sdk.tar.xz'
        with tarfile.open(file,'w:xz') as tar:
            for name, data, link in members:
                info=tarfile.TarInfo(name)
                if link:info.type=tarfile.SYMTYPE;info.linkname=link;tar.addfile(info)
                else:info.size=len(data);tar.addfile(info,io.BytesIO(data))
        return file

    def package(self, extra=None, missing=False):
        file=self.root/'release.zip'
        with zipfile.ZipFile(file,'w') as package:
            for name in ['ro/main','bootstrap.bin','game/main','hbmenu.nro'][:3 if missing else 4]:
                package.writestr(name,b'synthetic public input; never executed')
            package.writestr('licenses/LICENSE.txt','Fixture license notice')
            if extra:package.writestr(*extra)
        return file

    def test_checksum_cache_verified_without_network(self):
        cache=self.root/'cache';cache.mkdir();work=self.root/'work';work.mkdir();(cache/'fixture.zip').write_bytes(b'fixture')
        item=('fixture.zip','https://example.invalid/unused',hashlib.sha256(b'fixture').hexdigest())
        with patch.object(deps.urllib.request,'urlopen',side_effect=AssertionError('No network')):
            self.assertEqual(deps.verified(item,work,cache).read_bytes(),b'fixture')
            with self.assertRaisesRegex(ValueError,'checksum mismatch'):
                deps.verified((item[0],item[1],'0'*64),work,cache)

    def test_only_sdk_headers_extracted(self):
        file=self.tar([('libounce/include/switch/nacp.h',b'fixture declaration',None),('libounce/lib/library.a',b'not needed',None)])
        deps.sdk_headers(file,self.out)
        self.assertEqual((self.out/'vendor/devkitpro/libounce/include/switch/nacp.h').read_bytes(),b'fixture declaration')
        self.assertFalse((self.out/'vendor/devkitpro/libounce/lib').exists())

    def test_tar_links_and_traversal_rejected(self):
        for name, link in [('libounce/include/link','outside'),('libounce/include/../../outside',None)]:
            file=self.tar([(name,b'bad',link)])
            with self.assertRaises(ValueError):deps.sdk_headers(file,self.out)
        self.assertFalse((self.root/'outside').exists())

    def test_missing_sdk_declarations_rejected(self):
        with self.assertRaisesRegex(ValueError,'required declarations'):
            deps.sdk_headers(self.tar([('unrelated.txt',b'ignored',None)]),self.out)

    def test_installer_inputs_and_licenses_only(self):
        file=self.package(('programs/unused.nro',b'not extracted'))
        deps.installer_inputs(file,self.out)
        self.assertTrue((self.out/'vendor/kaihen-mo/ro/main').is_file())
        self.assertTrue((self.out/'vendor/kaihen-mo/licenses/LICENSE.txt').is_file())
        self.assertFalse((self.out/'vendor/kaihen-mo/programs').exists())
        self.assertEqual((self.out/'research/artifacts'/deps.KAIHEN[0]).read_bytes(),file.read_bytes())

    def test_zip_traversal_and_missing_required_input_rejected(self):
        with self.assertRaises(ValueError):deps.installer_inputs(self.package(('licenses/../../outside',b'bad')),self.out)
        # A separate destination avoids masking the missing-member result with
        # the intended immutable-existing-file guard.
        another=self.root/'another';another.mkdir()
        with self.assertRaisesRegex(ValueError,'missing qualification'):
            deps.installer_inputs(self.package(missing=True),another)
        self.assertFalse((self.out/'outside').exists())

    def test_existing_different_file_and_parent_link_preserved(self):
        (self.out/'entry').write_bytes(b'keep')
        with self.assertRaises(ValueError):deps.write_checked(self.out,'entry',b'new')
        self.assertEqual((self.out/'entry').read_bytes(),b'keep')
        (self.out/'linked').symlink_to(self.root,target_is_directory=True)
        with self.assertRaises(ValueError):deps.write_checked(self.out,'linked/outside',b'bad')
        self.assertFalse((self.root/'outside').exists())


if __name__ == '__main__':
    unittest.main()
