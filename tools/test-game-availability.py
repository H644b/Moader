#!/usr/bin/env python3
"""Test the production availability provider using real SDK declarations and fake IPC."""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent.parent
binary = root / 'build/host/game-availability-test'
binary.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['clang++', '-std=c++20', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-Wno-invalid-constexpr', '-Wno-missing-field-initializers',
                '-fsanitize=address,undefined', '-I' + str(root),
                '-I' + str(root / 'include'), '-I' + str(root / 'tests/host_sdk'),
                '-I' + str(root / 'vendor/devkitpro/libounce/include'),
                str(root / 'tests/game_availability.cpp'), '-lz', '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
