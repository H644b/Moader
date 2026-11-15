#!/usr/bin/env python3
"""Run the Switch-selected worker with fake SDK calls and real core/HTTP code."""
import pathlib
import subprocess
import tempfile
import zipfile

root = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='moader-web-worker-') as temporary:
    work = pathlib.Path(temporary).resolve()
    headers = work / 'headers'
    headers.mkdir()
    sdk = root / 'vendor/devkitpro/libounce/include'
    (headers / 'switch').symlink_to(sdk / 'switch', target_is_directory=True)
    fixture = work / 'compressed.zip'
    with zipfile.ZipFile(fixture, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('romfs/config.bin', b'worker fixture\n' * 20000)
    binary = work / 'web-worker-test'
    subprocess.run(['clang++', '-std=c++20', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-Wno-invalid-constexpr', '-Wno-missing-field-initializers',
                    '-fsanitize=address,undefined', '-D__SWITCH__',
                    '-I' + str(root / 'tests/web_worker_sdk'), '-I' + str(headers),
                    '-I' + str(root / 'include'), str(root / 'tests/web_worker.cpp'),
                    *[str(root / ('source/' + name + '.cpp'))
                      for name in ['model', 'library', 'zip', 'web']],
                    '-lz', '-pthread', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(work / 'library'), str(fixture)], check=True)
