#!/usr/bin/env python3
"""Offline C++ service-core restart -> staged catalogues -> one actual live C FS reader."""
from pathlib import Path
import subprocess
import tempfile
import zipfile
ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / 'runtime/modloader/native'
with tempfile.TemporaryDirectory(prefix='moader-backend-restart-') as temporary:
    work = Path(temporary).resolve()
    fixture = work / 'sample.zip'
    with zipfile.ZipFile(fixture, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('exefs/main', bytes([9, 8, 7, 6]))
    state = work / 'moader'
    common = ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-pthread', '-I' + str(ROOT / 'include')]
    producer = work / 'producer'
    subprocess.run(['clang++', '-std=c++17', *common, str(ROOT / 'tests/backend_restart.cpp'),
                    *[str(ROOT / 'source' / (name + '.cpp')) for name in ['model', 'library', 'zip', 'runtime_backend']],
                    '-lz', '-o', str(producer)], check=True)
    subprocess.run([str(producer), 'emit', str(state), str(fixture)], check=True, timeout=20)
    objects = []
    for source in [ROOT / 'tests/backend_restart_native.c', *[NATIVE / (name + '.c') for name in
                    ['romfs', 'namespace', 'diagnostics', 'trace', 'lazy', 'lazy-romfs', 'lazy-codefs', 'lazy-nso', 'moader']]]:
        obj = work / (source.stem + '.o')
        subprocess.run(['clang', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-DMOADER_HOST_TEST',
                        *common, '-I' + str(NATIVE), '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    native = work / 'native-reader'
    subprocess.run(['clang', *common, *objects, '-o', str(native)], check=True)
    subprocess.run([str(native), str(state)], check=True, timeout=20)
    subprocess.run([str(producer), 'verify', str(state)], check=True, timeout=20)
print('Backend restart regression passed. Host-only staged publication/retained-read seam; no device reload or process-launch claim.')
