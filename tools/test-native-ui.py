#!/usr/bin/env python3
"""Run actual native controls and renderer with synthetic, host-only fixtures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='moader-native-ui-') as temporary:
    work = Path(temporary).resolve()
    binary = work / 'control-test'
    subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-missing-field-initializers', '-fsanitize=address,undefined',
                    '-I' + str(root / 'native/ui_tests'), '-I' + str(root / 'native'),
                    '-I' + str(root / 'include'), str(root / 'native/ui_control_test.cpp'),
                    *[str(root / ('source/' + name + '.cpp')) for name in ['model', 'library', 'zip', 'web']],
                    '-pthread', '-lz', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
    subprocess.run(['python3', str(root / 'native/embed-font.py')], check=True)
    subprocess.run(['python3', str(root / 'native/embed-store.py')], check=True)
    renderer = work / 'renderer-test'
    subprocess.run(['clang++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-I' + str(root / 'include'), '-I' + str(root / 'build/generated'),
                    *[str(root / ('native/' + name + '.cpp')) for name in ['ui_capture','ui','canvas','navigation','artwork','settings_screen','store_screen','store']], '-o', str(renderer)], check=True)
    subprocess.run([str(renderer), str(work / 'captures')], check=True, timeout=60)
