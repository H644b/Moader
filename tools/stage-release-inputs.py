#!/usr/bin/env python3
"""Recover only checksum-pinned v4 build inputs from a published backend ZIP."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend-zip', type=Path, required=True)
    args = parser.parse_args()
    release = json.loads((ROOT / 'config/release.json').read_text())
    backend = module('moader_stage_backend', ROOT / 'tools/setup-backend.py')
    prefix = f"Moader-backend-{release['tag']}/"
    expected = {
        'dist/Moader.nro': ('dist/Moader.nro', release['binaries']['Moader.nro']),
        'dist/moader.nso': ('dist/moader.nso', release['binaries']['moader.nso']),
        'vendor/kaihen-mo/hen/main': ('vendor/kaihen-mo/hen/main', release['patched_hen']),
        'vendor/kaihen-mo/ro/main': ('vendor/kaihen-mo/ro/main', release['patched_ro']),
        'vendor/kaihen-mo/bootstrap.bin': ('vendor/kaihen-mo/bootstrap.bin', release['patched_bootstrap']),
        'research/artifacts/kaiHEN-20261114c.zip': (
            'research/artifacts/kaiHEN-20261114c.zip', {'sha256': backend.KAIHEN_SHA}),
        'prebuilt/standalone-bootstrap/bootstrap.bin': (
            'build/standalone-bootstrap/bootstrap.bin', release['standalone']['bootstrap']),
        'prebuilt/standalone-bootstrap/proof.json': (
            'build/standalone-bootstrap/proof.json', {'maxBytes': 1024 * 1024}),
    }
    with zipfile.ZipFile(args.backend_zip) as archive:
        for member, (target, record) in expected.items():
            info = archive.getinfo(prefix + member)
            if info.is_dir() or info.file_size > record.get('maxBytes', 200 * 1024 * 1024):
                raise ValueError('Unexpected release input size: ' + member)
            content = archive.read(info)
            if 'bytes' in record and len(content) != record['bytes']:
                raise ValueError('Release input length differs: ' + member)
            if 'sha256' in record and hashlib.sha256(content).hexdigest() != record['sha256']:
                raise ValueError('Release input digest differs: ' + member)
            path = ROOT / target
            if path.is_symlink() or path.exists() and (not path.is_file() or path.read_bytes() != content):
                raise ValueError('Existing release input differs: ' + target)
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists():
                path.write_bytes(content)
    print(f'Staged {len(expected)} checksum-qualified release inputs from {args.backend_zip.name}')


if __name__ == '__main__':
    main()
