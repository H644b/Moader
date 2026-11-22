#!/usr/bin/env python3
"""Export the existing checked SD file map for a browser directory picker."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import importlib.util
import json
import os

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('moader_standalone', ROOT / 'tools/seedsow-standalone.py')
standalone = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(standalone)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('kind', choices=('app', 'standalone'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--payload-dir', type=Path)
    args = parser.parse_args()
    target = args.output.absolute()
    if target.exists() or target.is_symlink():
        raise ValueError('Choose a new SD export directory')
    target.mkdir(parents=True, mode=0o700)
    if args.kind == 'app':
        spec = json.loads((ROOT / 'config/release.json').read_text())
        files = {}
        for source, dest in (('Moader.nro', 'switch/Moader.nro'),
                             ('moader.nso', 'kaihen/plugins/moader.nso')):
            item = spec['binaries'][source]
            data = (ROOT / item['source']).read_bytes()
            if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
                raise ValueError('Release binary differs: ' + source)
            files[dest] = data
    else:
        if args.payload_dir is None:
            raise ValueError('Qualified standalone payload is required')
        files, _ = standalone.moader_sd_files()
        payload, _ = standalone.standalone_payload_files(args.payload_dir)
        files.update(payload)
        standalone.add_standalone_bootstrap(
            files, ROOT / 'prebuilt/standalone-bootstrap')
    entries = []
    for relative, data in sorted(files.items()):
        parts = PurePosixPath(relative).parts
        if not parts or any(part in ('', '.', '..') for part in parts):
            raise ValueError('Invalid SD member')
        output = target.joinpath(*parts)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
        entries.append({'path': relative, 'bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest()})
    manifest = {'schema': 1, 'kind': args.kind, 'files': entries,
                'totalBytes': sum(row['bytes'] for row in entries)}
    (target / 'setup-export.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'manifest': str(target / 'setup-export.json'),
                      'fileCount': len(entries)}))

if __name__ == '__main__':
    main()
