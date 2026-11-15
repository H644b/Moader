#!/usr/bin/env python3
"""Test the real bounded diagnostic name parser against installed SDK layouts."""
import pathlib
import subprocess
ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/host'
BUILD.mkdir(parents=True, exist_ok=True)
binary = BUILD / 'metadata-names-test'
subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I' + str(ROOT),
                '-I' + str(ROOT / 'vendor/devkitpro/libounce/include'),
                str(ROOT / 'tests/metadata_names.cpp'), '-lz', '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
