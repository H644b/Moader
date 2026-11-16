#!/usr/bin/env python3
"""Exercise the production plugin-control extension; no native transport."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
hen = root / 'runtime/kaihen/hen'
with tempfile.TemporaryDirectory(prefix='moader-census-control-') as temporary:
    for name in ['census_control', 'census_relay', 'census_pipeline', 'lifecycle_server']:
        binary = Path(temporary) / name
        subprocess.run([
            'clang++', '-std=c++20', '-pthread', '-Wall', '-Wextra', '-Werror',
            '-Wno-missing-field-initializers', '-Wno-invalid-constexpr',
            '-fsanitize=address,undefined', '-fno-sanitize=alignment',
            '-D__SWITCH__', '-D__SWITCH2__', '-DLIBNX_HOST_TEST_TLS',
            '-I' + str(hen / 'tests/host'),
            '-I' + str(root / 'vendor/devkitpro/libounce/include'),
            '-I' + str(root / 'runtime/kaihen/libkaihen/include'),
            str(hen / ('tests/' + name + '_test.cpp')), '-o', str(binary)
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=20,
                       env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
