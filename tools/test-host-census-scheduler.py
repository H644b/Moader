#!/usr/bin/env python3
"""Run the actual HenServer scheduler fixture; no native transport or SDK build."""
from pathlib import Path
import argparse
import os
import platform
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--x86-host-fallback', action='store_true',
                    help='Explicit macOS arm64 allocator workaround; run this fixture as x86-64, not a native/device test')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
hen = root / 'runtime/kaihen/hen'
compiler = shlex.split(os.environ.get('CXX', 'clang++'))
if args.x86_host_fallback:
    if platform.system() != 'Darwin':
        parser.error('--x86-host-fallback is only supported on macOS')
    compiler += ['-target', 'x86_64-apple-macos13']
print('Scheduler fixture architecture: ' + ('explicit x86-64 host fallback' if args.x86_host_fallback else 'native host'), flush=True)
with tempfile.TemporaryDirectory(prefix='moader-census-scheduler-') as temporary:
    binary = Path(temporary) / 'server_census'
    link_gc = ['-Wl,-dead_strip'] if platform.system() == 'Darwin' else ['-Wl,--gc-sections']
    subprocess.run(compiler + [
        '-std=c++20', '-pthread', '-Wall', '-Wextra', '-Werror',
        '-Wno-missing-field-initializers', '-Wno-invalid-constexpr',
        '-ffunction-sections', '-fdata-sections', *link_gc,
        '-fsanitize=address,undefined', '-fno-sanitize=alignment',
        '-D__SWITCH__', '-D__SWITCH2__', '-DLIBNX_HOST_TEST_TLS',
        '-I' + str(hen / 'tests/host'),
        '-I' + str(root / 'vendor/devkitpro/libounce/include'),
        '-I' + str(root / 'runtime/kaihen/libkaihen/include'),
        str(hen / 'tests/server_census_test.cpp'), '-o', str(binary)
    ], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=20,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
