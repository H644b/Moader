#!/usr/bin/env python3
"""Focused resident entry-point checks; all generated outputs are isolated."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='moader-resident-') as temporary:
    work = pathlib.Path(temporary)
    # Select the real Horizon declarations needed by this lifecycle fixture,
    # preserving host POSIX headers and avoiding the SDK's ARM crypto umbrella.
    headers = work / 'headers'
    headers.mkdir()
    sdk = root / 'vendor/devkitpro/libounce/include'
    (headers / 'switch').symlink_to(sdk / 'switch', target_is_directory=True)
    (headers / 'switch2').symlink_to(sdk / 'switch2', target_is_directory=True)
    binary = work / 'resident-test'
    subprocess.run(['clang++', '-std=c++20', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-Wno-invalid-constexpr', '-Wno-missing-field-initializers',
                    '-fsanitize=address,undefined', '-I' + str(root / 'include'),
                    '-I' + str(root / 'tests/host_sdk'),
                    '-I' + str(root / 'tests/host_sdk/resident'),
                    '-I' + str(headers),
                    '-I' + str(root / 'runtime/kaihen/libkaihen/include'),
                    '-I' + str(root / 'build/generated'),
                    str(root / 'tests/resident_lifecycle.cpp'),
                    *[str(root / ('source/' + name + '.cpp'))
                      for name in ['model', 'library', 'zip', 'web', 'runtime_backend']],
                    '-lz', '-pthread', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(work)], check=True)
