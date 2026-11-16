#!/usr/bin/env python3
"""Exercise the actual native reload controller against deterministic HEN replies."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='moader-reload-client-') as temporary:
    work = Path(temporary)
    binary = work / 'reload-client'
    subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(root / 'native'),
                    '-I' + str(root / 'vendor/devkitpro/libounce/include'),
                    '-I' + str(root / 'runtime/kaihen/libkaihen/include'),
                    str(root / 'tests/reload_client.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
