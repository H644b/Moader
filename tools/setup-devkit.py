#!/usr/bin/env python3
"""Install pinned build packages into this project, without system changes."""
import concurrent.futures, hashlib, json, pathlib, subprocess, urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDOR = ROOT / 'vendor'
SDK = VENDOR / 'devkitpro'
REGISTER = json.loads((ROOT / 'tools/toolchain-packages.json').read_text())
NAMES = ['devkitA64', 'devkita64-binutils', 'devkita64-gcc', 'devkita64-newlib',
         'devkita64-rules', 'switch-tools', 'general-tools', 'dkp-toolchain-vars', 'switch-zlib']

def download(name):
    item = REGISTER[name]
    file = VENDOR / item['FILENAME'][0]
    if not file.exists():
        req = urllib.request.Request(item['url'], headers={'User-Agent': 'pacman/6.0.2 (Darwin arm64) libalpm/13.0.2'})
        with urllib.request.urlopen(req, timeout=90) as response:
            file.write_bytes(response.read())
    assert hashlib.sha256(file.read_bytes()).hexdigest() == item['SHA256SUM'][0], name
    print('Verified', name, flush=True)
    return file

SDK.mkdir(parents=True, exist_ok=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
    files = list(executor.map(download, NAMES))
for file in files:
    names = subprocess.check_output(['tar', '-tf', str(file)], text=True).splitlines()
    members = [n for n in names if n.startswith('opt/devkitpro/') and '..' not in n.split('/')]
    if members:
        subprocess.run(['tar', '-xf', str(file), '-C', str(SDK), '--strip-components=2', *members], check=True)
archive = ROOT / 'research/artifacts/ounce-devkit-20261114.tar.xz'
if not archive.exists():
    archive.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen('https://github.com/ouncebrew/ounce-devkit/releases/download/devkit-20261114/ounce-devkit-20261114.tar.xz', timeout=90) as response:
        archive.write_bytes(response.read())
assert hashlib.sha256(archive.read_bytes()).hexdigest() == 'f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59'
subprocess.run(['tar', '-xJf', str(archive), '-C', str(SDK)], check=True)
print('Native build SDK:', SDK)
