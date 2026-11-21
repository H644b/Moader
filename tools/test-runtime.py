#!/usr/bin/env python3
"""Execute the actual native FS reader with owned SDK mocks under sanitizers."""
from pathlib import Path
import subprocess, tempfile, zipfile
ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / 'runtime/modloader/native'
BUILD = ROOT / 'build/host'
BUILD.mkdir(parents=True, exist_ok=True)
sources = ['verify-lazy', 'romfs', 'namespace', 'diagnostics', 'trace', 'lazy',
           'lazy-romfs', 'lazy-codefs', 'lazy-nso', 'moader']
binary = BUILD / 'runtime-test'
# glibc exposes pthread barriers in strict C11 only with POSIX enabled.
subprocess.run(['clang','-std=c11','-D_POSIX_C_SOURCE=200809L','-g','-O1','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-DMOADER_HOST_TEST','-pthread','-I'+str(ROOT/'include'),
                '-I'+str(NATIVE), *[str(NATIVE/(name+'.c')) for name in sources],
                '-o', str(binary)], check=True)
with tempfile.TemporaryDirectory(prefix='moader-runtime-') as temporary:
    work = Path(temporary).resolve()
    (work / 'state/qa').mkdir(parents=True)
    fixture = work / 'mod.zip'
    with zipfile.ZipFile(fixture,'w',zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('romfs/Model/sample.bin',b'example texture')
        archive.writestr('romfs/config.txt',b'configuration')
    replacement = work / 'replacement.zip'
    with zipfile.ZipFile(replacement, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('romfs/config.txt', b'replacement')
    backend = BUILD / 'backend-test'
    subprocess.run(['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I'+str(ROOT/'include'),
                    str(ROOT/'tests/backend_test.cpp'),
                    *[str(ROOT/'source'/name) for name in ['model.cpp','library.cpp','zip.cpp','runtime_backend.cpp']],
                    '-lz','-o',str(backend)],check=True)
    subprocess.run([str(backend),str(fixture),str(work/'library'),str(replacement)],check=True)
    subprocess.run([str(binary),str(work/'library/native-fixture.bin')],check=True,cwd=work)
