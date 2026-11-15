#!/usr/bin/env python3
"""Offline actual native producer/C++ reader regressions; all outputs temporary."""
from pathlib import Path
import subprocess, tempfile, zipfile
ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / 'runtime/modloader/native'
native = ['romfs','namespace','diagnostics','trace','lazy','lazy-romfs','lazy-codefs','lazy-nso','moader']
cpp = [str(ROOT/'source'/name) for name in ['model.cpp','library.cpp','zip.cpp','runtime_backend.cpp']]
subprocess.run(['node',str(ROOT/'runtime/modloader/tests/diagnostics/moader-log-context.cjs')],check=True)
with tempfile.TemporaryDirectory(prefix='moader-log-snapshots-') as temporary:
    work = Path(temporary).resolve()
    objects = []
    for source in [ROOT/'tests/runtime_log_native.c',*[NATIVE/(name+'.c') for name in native]]:
        obj = work/(source.stem+'.o')
        # glibc exposes pthread barriers in strict C11 only with POSIX enabled.
        subprocess.run(['clang','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-DMOADER_HOST_TEST','-pthread',
                        '-I'+str(ROOT/'include'),'-I'+str(NATIVE),'-c',str(source),'-o',str(obj)],check=True)
        objects.append(str(obj))
    binary = work/'snapshot-test'
    common = ['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
              '-fsanitize=address,undefined','-pthread','-I'+str(ROOT/'include')]
    subprocess.run([*common,str(ROOT/'tests/runtime_log_test.cpp'),*cpp,*objects,'-lz','-o',str(binary)],check=True)
    subprocess.run([str(binary),str(work/'snapshots')],check=True)
    # Preserve catalogue/receipt and existing actual-C regression coverage.
    fixture = work/'mod.zip'
    with zipfile.ZipFile(fixture,'w',zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('romfs/Model/sample.bin',b'example texture')
        archive.writestr('romfs/config.txt',b'configuration')
    backend = work/'backend-test'
    subprocess.run([*common,str(ROOT/'tests/backend_test.cpp'),*cpp,'-lz','-o',str(backend)],check=True)
    subprocess.run([str(backend),str(fixture),str(work/'library')],check=True)
    native_test = work/'native-test'
    subprocess.run(['clang','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-DMOADER_HOST_TEST','-pthread',
                    '-I'+str(ROOT/'include'),'-I'+str(NATIVE),str(NATIVE/'verify-lazy.c'),
                    *objects[1:],'-o',str(native_test)],check=True)
    (work/'state/qa').mkdir(parents=True)
    subprocess.run([str(native_test),str(work/'library/native-fixture.bin')],cwd=work,check=True)
print('All runtime-log and routing regressions passed; no device connections or shared build outputs.')
