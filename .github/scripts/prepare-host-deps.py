#!/usr/bin/env python3
"""Fetch verified public inputs needed by host tests; never contact a console."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import shutil
import stat
import tarfile
import tempfile
import urllib.request
import zipfile

SDK = ('ounce-devkit-20261114.tar.xz',
       'https://github.com/ouncebrew/ounce-devkit/releases/download/devkit-20261114/ounce-devkit-20261114.tar.xz',
       'f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59')
KAIHEN = ('kaiHEN-20261114c.zip',
          'https://github.com/ouncebrew/kaihen/releases/download/20261114c/kaiHEN-20261114c.zip',
          '63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967')
MAX_DOWNLOAD = 64 * 1024 * 1024


def verified(item, work, archives):
    name, url, expected = item
    source = archives/name if archives else None
    target = work/name
    if source and source.is_file():
        if source.stat().st_size > MAX_DOWNLOAD:
            raise ValueError('Cached public dependency exceeds its size budget')
        shutil.copyfile(source, target)
    else:
        request = urllib.request.Request(url, headers={'User-Agent':'Moader-host-CI'})
        with urllib.request.urlopen(request, timeout=90) as response, target.open('wb') as out:
            total = 0
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > MAX_DOWNLOAD:
                    raise ValueError('Public dependency exceeds its download budget')
                out.write(chunk)
    if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
        raise ValueError('Public dependency checksum mismatch: ' + name)
    print('Verified public dependency:', name)
    return target


def safe_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name:
        raise ValueError('Unsafe dependency member name')
    return path


def write_checked(root, name, data):
    """Populate only regular files; preserve different existing local inputs."""
    root = root.resolve()
    name = safe_name(str(name))
    parent = root
    for part in name.parts[:-1]:
        parent = parent/part
        if parent.is_symlink() or parent.exists() and not parent.is_dir():
            raise ValueError('Dependency parent is not a normal directory')
        parent.mkdir(exist_ok=True)
    target = root.joinpath(*name.parts)
    if target.is_symlink() or target.exists() and not target.is_file():
        raise ValueError('Dependency destination is not a regular file')
    if target.exists():
        if target.read_bytes() != data:
            raise ValueError('Existing local dependency differs; use a clean checkout')
        return
    with target.open('xb') as out:
        out.write(data)


def sdk_headers(archive, root):
    target = root/'vendor/devkitpro'
    total = 0
    with tarfile.open(archive, 'r:xz') as tar:
        for member in tar:
            if not member.name.startswith('libounce/include/'):
                continue
            name = safe_name(member.name)
            if member.isdir():
                continue
            if not member.isfile() or member.size > 4 * 1024 * 1024:
                raise ValueError('Unsupported SDK header member')
            total += member.size
            if total > 32 * 1024 * 1024:
                raise ValueError('SDK header budget exceeded')
            with tar.extractfile(member) as source:
                data = source.read(member.size + 1)
            if len(data) != member.size:
                raise ValueError('Truncated SDK header')
            write_checked(root, PurePosixPath('vendor/devkitpro')/name, data)
    if not (target/'libounce/include/switch/nacp.h').is_file():
        raise ValueError('Pinned SDK archive did not supply required declarations')
    print('Prepared host SDK declarations; no target compiler installed.')


def installer_inputs(archive, root):
    # The existing qualification test validates the release ZIP itself and the
    # four exact upstream inputs. No downloaded executable is launched here.
    write_checked(root, PurePosixPath('research/artifacts')/KAIHEN[0], archive.read_bytes())
    required = {'ro/main', 'bootstrap.bin', 'game/main', 'hbmenu.nro'}
    with zipfile.ZipFile(archive) as package:
        for info in package.infolist():
            if info.filename not in required and not info.filename.startswith('licenses/'):
                continue
            name = safe_name(info.filename)
            if info.is_dir():
                continue
            if stat.S_ISLNK(info.external_attr >> 16) or info.file_size > 16 * 1024 * 1024:
                raise ValueError('Unsupported release member')
            write_checked(root, PurePosixPath('vendor/kaihen-mo')/name, package.read(info))
            required.discard(info.filename)
    if required:
        raise ValueError('Pinned release is missing qualification inputs')
    print('Prepared pinned installer fixtures; no device transport opened.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--archives-dir', type=Path, help='Optional local cache of the same checksum-pinned public archives.')
    parser.add_argument('--sdk', action='store_true')
    parser.add_argument('--installer', action='store_true')
    args = parser.parse_args()
    if not (args.sdk or args.installer):
        parser.error('Select --sdk and/or --installer')
    with tempfile.TemporaryDirectory(prefix='moader-ci-deps-') as temporary:
        work = Path(temporary)
        if args.sdk:
            sdk_headers(verified(SDK,work,args.archives_dir), args.root)
        if args.installer:
            installer_inputs(verified(KAIHEN,work,args.archives_dir), args.root)


if __name__ == '__main__':
    main()
