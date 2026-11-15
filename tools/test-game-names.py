#!/usr/bin/env python3
"""Exercise the production resolver with SDK declarations and fake service calls."""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent.parent
binary = root / 'build/host/game-names-test'
binary.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['clang++', '-std=c++20', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-Wno-invalid-constexpr', '-Wno-missing-field-initializers',
                '-fsanitize=address,undefined', '-I' + str(root),
                '-I' + str(root / 'vendor/devkitpro/libounce/include'),
                str(root / 'tests/game_names.cpp'), '-lz', '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
