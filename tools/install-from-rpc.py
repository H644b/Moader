#!/usr/bin/env python3
"""Prepare local Moader inputs from a mac2jb status command; print installation command."""
from __future__ import annotations
import argparse, os, pathlib, re, shlex, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PREFIX = 'Prepared read-only local Moader snapshot: '
SUFFIX = '. No console connection opened.'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def parse_status(command):
    tokens = shlex.split(command, posix=True)
    require(len(tokens) == 4 and tokens[3] == 'status', 'Paste the complete four-token mac2jb status command, ending in status')
    require(all(not any(ord(c) < 32 for c in token) for token in tokens), 'Control characters are not accepted')
    node, rpc, run = map(pathlib.Path, tokens[:3])
    require(all(p.is_absolute() for p in (node, rpc, run)), 'Node, rpc.cjs and run-folder paths must be absolute')
    require(rpc.name == 'rpc.cjs', 'Expected the mac2jb rpc.cjs status command')
    require(re.fullmatch(r'switch2-[A-Za-z0-9]+', run.name), 'Expected a mac2jb run folder')
    return str(node), run


def prepared_manifest(output, root):
    matches = [line[len(PREFIX):-len(SUFFIX)] for line in output.splitlines() if line.startswith(PREFIX) and line.endswith(SUFFIX)]
    require(len(matches) == 1, 'Installer did not report exactly one unique prepared snapshot')
    path = pathlib.Path(matches[0])
    require(path.is_absolute() and path.name == 'manifest.json', 'Invalid prepared manifest path')
    require(path.parent.parent == root / 'build/install' and path.parent.name.startswith('prepared-') and len(path.parent.name) > len('prepared-'), 'Installer must report a unique build/install/prepared-*/manifest.json')
    require(path.is_file(), 'Prepared snapshot is missing')
    require(all(not part.is_symlink() for part in (path, *path.parents) if part == root or root in part.parents), 'Prepared snapshot path contains a symlink')
    return path


def prepare(root, node, run):
    require(pathlib.Path(node).is_file() and os.access(node, os.X_OK), 'The pasted Node executable is missing or not executable')
    require(run.is_dir() and not run.is_symlink(), 'The pasted mac2jb run folder is missing or is a symlink')
    version = subprocess.run([node, '--version'], check=True, capture_output=True, text=True).stdout.strip()
    require(re.fullmatch(r'v24\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?', version), 'Use the exact Node 24 executable supplied by mac2jb')
    # No pasted script is executed. Both tools below are the local Moader copies.
    subprocess.run([sys.executable, str(root / 'tools/setup-backend.py'), '--root', str(root), '--node', node], cwd=root, check=True)
    try:
        output = subprocess.run([node, str(root / 'tools/install-moader.cjs'), '--check'], cwd=root, check=True, capture_output=True, text=True).stdout
    except subprocess.CalledProcessError as error:
        if error.stderr:
            sys.stderr.write(error.stderr)
        raise
    manifest = prepared_manifest(output, root)
    return [node, str(root / 'tools/install-moader.cjs'), '--prepared', str(manifest), str(run)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--status-command', required=True, help='the full quoted mac2jb status command (not just its run path)')
    parser.add_argument('--prepare', action='store_true', help='fetch pinned missing source, validate local kit, create a local snapshot and print installation command')
    parser.add_argument('--root', type=pathlib.Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    node, run = parse_status(args.status_command)
    if not args.prepare:
        print('Parsed Node executable: ' + node)
        print('Parsed run folder: ' + str(run))
        print('Add --prepare to prepare local inputs. No status command was executed.')
        return None
    command = prepare(args.root.resolve(), node, run)
    print('Local snapshot prepared. No console connection opened. Review and run this command once:')
    print(shlex.join(command))
    return command


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('Moader preparation stopped: ' + str(error))
