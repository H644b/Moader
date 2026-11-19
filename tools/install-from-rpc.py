#!/usr/bin/env python3
"""Prepare local Moader inputs for a pinned held-FS host; print installation command."""
from __future__ import annotations
import argparse, os, pathlib, re, shlex, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PREFIX = 'Prepared read-only local Moader snapshot: '
SUFFIX = '. No console connection opened.'
HOST_PROFILES = ('smilebasic-kurumuz', 'civ7-s8n', 'seedsow-malev0')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def clean_text(value, label):
    require(isinstance(value, str) and value.strip(), label + ' must not be empty')
    require(not any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in value), label + ' must not contain control characters')
    return value


def posix_path(value, label):
    clean_text(value, label)
    require(not re.match(r'^[A-Za-z]:', value) and not value.startswith('\\\\'), label + ' must use a POSIX path; in WSL use /mnt/c/... rather than a Windows path')
    return pathlib.Path(value)


def run_directory(value):
    path = posix_path(str(value), 'Run folder')
    absolute = path if path.is_absolute() else pathlib.Path.cwd() / path
    # Check lexical ancestors before resolve: link/../run must not conceal a
    # symlink. Only then canonicalize paths relative to the invoking terminal.
    for part in (absolute, *absolute.parents):
        require(not part.is_symlink(), 'Run folder path contains a symlink: ' + str(part) + '; use the real, nonsymlink ns2jb/mac2jb run path')
    try:
        resolved = absolute.resolve(strict=True)
    except OSError as error:
        raise ValueError('Run folder is missing or inaccessible; use the existing runs folder printed by the held ns2jb/mac2jb runner: ' + str(absolute)) from error
    require(resolved.is_dir(), 'Run folder must be an existing directory: ' + str(resolved))
    require(re.fullmatch(r'switch2-[A-Za-z0-9]+', resolved.name), 'Invalid ns2jb/mac2jb run folder name; use the folder printed by the held runner')
    return resolved


def resolve_node(value):
    require(sys.platform != 'win32', 'Native Windows is unsupported; run this helper in Linux/WSL or macOS with Node.js 24')
    path = posix_path(value, 'Node executable')
    windows_message = 'Windows Node executables are unsupported; in WSL install Linux Node.js 24 inside the distribution and select its node executable with --node'
    require(path.suffix.lower() != '.exe', windows_message)
    if '/' not in value:
        selected = shutil.which(value)
        require(selected is not None, 'Node executable was not found on PATH; install Node.js 24 and use --node node or --node /absolute/path/to/node')
        path = posix_path(selected, 'Node executable')
    require(path.suffix.lower() != '.exe', windows_message)
    if not path.is_absolute():
        path = pathlib.Path.cwd() / path
    require(path.is_file() and os.access(path, os.X_OK), 'Node executable is missing or not executable; select an executable Node.js 24 binary: ' + str(path))
    require(path.resolve().suffix.lower() != '.exe', windows_message)
    with path.open('rb') as binary:
        require(binary.read(2) != b'MZ', windows_message)
    # Keep the selected executable, including a legitimate POSIX Node symlink.
    # Resolving it for the Windows check must not silently select another Node.
    return str(path)


def parse_status(command):
    clean_text(command, 'Status command')
    tokens = shlex.split(command, posix=True)
    require(len(tokens) == 4 and tokens[3] == 'status', 'Paste the complete four-token mac2jb status command, ending in status')
    for token in tokens:
        clean_text(token, 'Status command token')
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


def prepare(root, node, run, host_profile='smilebasic-kurumuz'):
    require(host_profile in HOST_PROFILES, 'Unsupported host profile')
    run = run_directory(run)
    node = resolve_node(node)
    try:
        version = subprocess.run([node, '--version'], check=True, capture_output=True, text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError('Could not run Node --version; select a working POSIX Node.js 24 executable (Linux Node inside WSL)') from error
    require(re.fullmatch(r'v24\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?', version), 'Node.js 24 is required; select it with --node for ns2jb, or use the exact Node 24 path from the mac2jb status command')
    # No pasted script is executed. Both tools below are the local Moader copies.
    profile_args = ['--host-profile', host_profile] if host_profile != HOST_PROFILES[0] else []
    offline_args = ['--offline'] if host_profile == 'seedsow-malev0' else []
    subprocess.run([sys.executable, str(root / 'tools/setup-backend.py'), '--root', str(root), '--node', node, *profile_args, *offline_args], cwd=root, check=True)
    try:
        output = subprocess.run([node, str(root / 'tools/install-moader.cjs'), *profile_args, '--check'], cwd=root, check=True, capture_output=True, text=True).stdout
    except subprocess.CalledProcessError as error:
        if error.stderr:
            sys.stderr.write(error.stderr)
        raise
    manifest = prepared_manifest(output, root)
    return [node, str(root / 'tools/install-moader.cjs'), '--prepared', str(manifest), str(run)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    route = parser.add_mutually_exclusive_group(required=True)
    route.add_argument('--status-command', help='the full quoted mac2jb status command (not just its run path)')
    route.add_argument('--run-dir', help='an existing switch2-* held-run folder; absolute or relative POSIX path; Seedsow requires a new Moader-owned run')
    parser.add_argument('--host-profile', choices=HOST_PROFILES, default=HOST_PROFILES[0], help='pinned host family; nondefault profiles require --run-dir; Seedsow uses its dedicated GUI')
    parser.add_argument('--node', help='Node.js 24 command or executable path (default: node); --run-dir route only')
    parser.add_argument('--prepare', action='store_true', help='prepare pinned local inputs and print installation command; use the dedicated GUI for Seedsow')
    parser.add_argument('--root', type=pathlib.Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    require(sys.platform != 'win32', 'Native Windows is unsupported; run this helper in Linux/WSL or macOS with Node.js 24')
    if args.status_command is not None:
        if args.host_profile != HOST_PROFILES[0]:
            parser.error(args.host_profile + ' requires an explicit --run-dir for that profile; mac2jb status commands identify only the default profile and do not qualify other held sessions')
        if args.node is not None:
            parser.error('--node may only be used with --run-dir; the mac2jb status command supplies its exact Node executable')
        node, run = parse_status(args.status_command)
    else:
        run = run_directory(args.run_dir)
        node = resolve_node('node' if args.node is None else args.node)
    if not args.prepare:
        print('Parsed Node executable: ' + node)
        print('Parsed run folder: ' + str(run))
        print('Add --prepare to prepare local inputs. No status command was executed.' if args.status_command is not None else 'Add --prepare to prepare local inputs. No commands were executed.')
        return None
    command = prepare(args.root.resolve(), node, run, args.host_profile)
    print('Local snapshot prepared. No console connection opened. Review and run this command once:')
    print(shlex.join(command))
    return command


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('Moader preparation stopped: ' + str(error))
