#!/usr/bin/env python3
"""Install pinned macOS arm64 or Linux x86_64 SDK packages without system changes."""
import concurrent.futures
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDOR = ROOT / 'vendor'
SDK = VENDOR / 'devkitpro'
NAMES = ['devkitA64', 'devkita64-binutils', 'devkita64-gcc', 'devkita64-newlib',
         'devkita64-rules', 'switch-tools', 'general-tools', 'dkp-toolchain-vars', 'switch-zlib']
OUNCE_URL = 'https://github.com/ouncebrew/ounce-devkit/releases/download/devkit-20261114/ounce-devkit-20261114.tar.xz'
OUNCE_SHA256 = 'f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59'

def host():
    system, machine = platform.system(), platform.machine().lower()
    if system == 'Darwin' and machine in ('arm64', 'aarch64'):
        return 'macos-arm64', 'pacman/6.0.2 (Darwin arm64) libalpm/13.0.2'
    if system == 'Linux' and machine in ('x86_64', 'amd64'):
        return 'linux-x86_64', 'pacman/6.0.2 (Linux x86_64) libalpm/13.0.2'
    raise ValueError('Pinned SDK packages support Apple Silicon macOS and Linux x86_64 (including WSL2) only')

def register(host_name):
    base = json.loads((ROOT / 'tools/toolchain-packages.json').read_text())
    if host_name == 'linux-x86_64':
        base.update(json.loads((ROOT / 'tools/toolchain-packages-linux-x86_64.json').read_text()))
    return base

def verified_download(file, url, expected, user_agent):
    if file.is_symlink():
        raise ValueError('Refusing symlinked SDK package: ' + file.name)
    if not file.exists():
        file.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            req = urllib.request.Request(url, headers={'User-Agent': user_agent})
            with tempfile.NamedTemporaryFile(dir=file.parent, prefix=file.name + '.', delete=False) as out:
                temporary = pathlib.Path(out.name)
                with urllib.request.urlopen(req, timeout=90) as response:
                    while chunk := response.read(1024 * 1024):
                        out.write(chunk)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != expected:
                raise ValueError('Pinned SDK download checksum differs: ' + file.name)
            try:
                os.link(temporary, file)
            except FileExistsError:
                pass
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    if hashlib.sha256(file.read_bytes()).hexdigest() != expected:
        raise ValueError('Pinned SDK package checksum differs: ' + file.name)
    return file

def main():
    host_name, user_agent = host()
    packages = register(host_name)
    VENDOR.mkdir(parents=True, exist_ok=True)
    def download(name):
        item = packages[name]
        file = verified_download(VENDOR / item['FILENAME'][0], item['url'], item['SHA256SUM'][0], user_agent)
        print('Verified', name, flush=True)
        return file

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        files = list(executor.map(download, NAMES))
    SDK.mkdir(parents=True, exist_ok=True)
    for file in files:
        names = subprocess.check_output(['tar', '-tf', str(file)], text=True).splitlines()
        members = [n for n in names if n.startswith('opt/devkitpro/') and '..' not in n.split('/')]
        if members:
            subprocess.run(['tar', '-xf', str(file), '-C', str(SDK), '--strip-components=2', *members], check=True)
    archive = ROOT / 'research/artifacts/ounce-devkit-20261114.tar.xz'
    verified_download(archive, OUNCE_URL, OUNCE_SHA256, user_agent)
    subprocess.run(['tar', '-xJf', str(archive), '-C', str(SDK)], check=True)
    print('Native build SDK:', SDK)

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('SDK setup stopped: ' + str(error))
