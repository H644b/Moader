#!/usr/bin/env python3
"""Run the actual inventory/ZIP code with deterministic injected IO and timing."""
import pathlib
import subprocess
import tempfile
import zipfile

root = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='moader-library-refresh-') as temporary:
    work = pathlib.Path(temporary).resolve()
    archive = work / 'synthetic.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        output.writestr('romfs/payload.bin', b'Synthetic payload\n' * (1 << 16))
    for flags, name in [([], 'posix'), (['-DMOADER_TEST_NONREPLACING_RENAME'], 'native-publication')]:
        binary = work / name
        subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I' + str(root / 'include'),
                        *flags, str(root / 'tests/library_refresh.cpp'), str(root / 'source/model.cpp'),
                        '-pthread', '-lz', '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(work / (name + '-library')), str(archive)], check=True, timeout=30)
