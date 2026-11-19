#!/usr/bin/env python3
"""Check a clean public export. Print relative filenames/reasons, never matches.

Run before generating local SDK/build inputs. This is a publication guard, not
an export allowlist or a guarantee that manual privacy review can be skipped.
"""
from pathlib import Path
import argparse
import ipaddress
import json
import os
import re
import stat

PRIVATE_TOP = {
    'state', 'evidence', 'research', 'coordination', 'output', 'build', 'dist',
    'vendor', 'accounts', 'credentials', 'secrets', '.codex', '.impeccable',
    '.idea', '.vscode', '.ssh', '.aws', 'node_modules', '__pycache__', '.pytest_cache',
}
PRIVATE_PREFIXES = ('native/review', 'runtime/modloader/state', 'runtime/modloader/logs')
PRIVATE_NAMES = {'.ds_store', '.env', 'pairing-code', 'pairing-code.txt',
                 'cookies.txt', 'credentials.json', 'accounts.json', 'keychain.json',
                 '.envrc', '.npmrc', '.pypirc', '.netrc', 'app-screenshot.bmp',
                 'pairing.json', 'connection.json', 'session.json', 'resident-install.json'}
ARTIFACT_SUFFIXES = {'.dmp', '.core', '.hop', '.idb', '.i64', '.nsp', '.xci',
                     '.nca', '.kip', '.npdm', '.nso', '.nro', '.elf', '.o', '.a',
                     '.zip', '.tar', '.xz', '.zst', '.7z', '.pcap', '.pcapng',
                     '.log', '.pem', '.key', '.p12', '.pfx', '.bin', '.bntx',
                     '.bfres', '.szs', '.romfs', '.nacp', '.sqlite', '.db', '.bmp'}
PRIVATE_NETWORKS = tuple(ipaddress.ip_network(n) for n in (
    '10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '169.254.0.0/16',
    '100.64.0.0/10', 'fc00::/7', 'fe80::/10'))
IPV4 = re.compile(r'(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])')
IPV6 = re.compile(r'(?i)(?<![\w:])(?:f[cd][0-9a-f]{2}|fe80):[0-9a-f:]+')
PATTERNS = (
    ('private key material', re.compile(r'-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY-----')),
    ('credential token', re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|AKIA[A-Z0-9]{16}|xox[baprs]-[A-Za-z0-9-]{16,})\b')),
    ('home or temporary user path', re.compile(r'/(?:Users|home)/[^/\s<>"\']+/|/(?:private/)?var/folders/[^/\s<>"\']+/')),
    ('session identifier', re.compile(r'\bswitch2-[A-Za-z0-9]{4,}\b')),
    ('cookie value', re.compile(r'(?i)(?:\.ROBLOSECURITY|session_cookie)\s*[=:]\s*["\']?[^\s"\';,<>]{12,}')),
    ('credential in URL', re.compile(r'(?i)https?://[^/\s:@]+:[^/\s@]+@')),
    ('device serial', re.compile(r'\b(?:XAW|XAJ|XKW|XKJ|XJW|XTW|XKX)[0-9]{8,}\b')),
)
ASSIGNMENT = re.compile(r'''(?i)\b(pairing[_-]?code|api[_-]?key|access[_-]?token|refresh[_-]?token|password|secret|token)\b["']?\s*[:=]\s*["']([^"'\r\n]{1,512})["']''')
BEARER = re.compile(r'(?i)\bBearer\s+([A-Za-z0-9._~-]{20,}|[0-9]{6})(?![A-Za-z0-9._~-])')
FIXTURE_SESSIONS = {
    'Test', 'Other', 'test', 'host', 'layout', 'safe', 'normal', 'trace',
    'timeouttest', 'controlfailure', 'controlok', 'loggervariant',
    'old1', 'old2', 'old3', 'old4', 'ControlOffline', 'LogOffline',
    'Mk8Offline', 'PolicyOffline', 'Recovery', 'RecoveryOffline',
}
EXACT_FIXTURE_SESSIONS = {
    'runtime/modloader/native/verify.py': {'nativeqa'},
    'tools/test-setup-backend.py': {'AbC123'},
}
EXACT_FIXTURE_CODES = {
    'native/ui_control_test.cpp': {'000042'},
    'tools/test-web.py': {'999999'},
}
SYNTHETIC_CAPTURE_PAIRING = '000042 is synthetic fixture data, not a console secret'
MAX_BYTES = 32 * 1024 * 1024


def fixture_path(relative):
    return relative in {'native/ui_control_test.cpp','runtime/modloader/native/verify.py'} or relative.startswith(('tests/', '.github/tests/', 'runtime/modloader/tests/')) or (
        relative.startswith('tools/test-') and relative.endswith(('.py', '.cjs')))


def content_reasons(relative, text):
    reasons = set()
    fixture = fixture_path(relative)
    for reason, pattern in PATTERNS:
        for match in pattern.finditer(text):
            if reason == 'session identifier':
                suffix = match.group()[8:]
                if relative.endswith('.md') and suffix == 'SESSION':
                    continue
                if (fixture and suffix in FIXTURE_SESSIONS) or suffix in EXACT_FIXTURE_SESSIONS.get(relative,set()):
                    continue
            reasons.add(reason)
    for pattern in (IPV4, IPV6):
        for match in pattern.finditer(text):
            try:
                address = ipaddress.ip_address(match.group())
            except ValueError:
                continue
            # Public protocol documentation and this policy can name canonical
            # reserved blocks, without permitting a real host within a block.
            prefix = re.match(r'^/(\d{1,3})(?!\d)', text[match.end():])
            if prefix:
                try:
                    if ipaddress.ip_network(f'{address}/{prefix[1]}', strict=True) in PRIVATE_NETWORKS:
                        continue
                except ValueError:
                    pass
            if any(address.version == network.version and address in network for network in PRIVATE_NETWORKS):
                reasons.add('private network address')
    for match in ASSIGNMENT.finditer(text):
        key, value = match.groups()
        # Documentation placeholders and narrow, explicitly synthetic constants
        # are permitted; a test filename never exempts arbitrary real tokens.
        if value.startswith(('<', '${')) or value.lower() in {
                'example', 'placeholder', 'redacted', 'your-code', 'your-token',
                'fixture-password', 'fixture-secret', 'fixture-token'}:
            continue
        if fixture and value in {'012345', '001234', '000000', '123456'}:
            continue
        if value in EXACT_FIXTURE_CODES.get(relative,set()):
            continue
        if relative == 'native/ui_capture_convert.py' and key.lower() == 'pairing_code' and value == SYNTHETIC_CAPTURE_PAIRING:
            continue
        if len(value) >= 8 or (len(value) == 6 and value.isdecimal()):
            reasons.add('literal credential or pairing value')
    for match in BEARER.finditer(text):
        if fixture and match[1] in {'012345', '001234', '000000', '123456'}:
            continue
        if match[1] in EXACT_FIXTURE_CODES.get(relative,set()):
            continue
        reasons.add('literal bearer credential')
    return sorted(reasons)


def scan(root):
    root = Path(root).resolve()
    issues = []
    if not root.is_dir():
        return [{'path': '.', 'reason': 'scan root is not a directory'}]

    def add(relative, reason):
        issues.append({'path': relative, 'reason': reason})

    def walk(folder):
        try:
            entries = sorted(os.scandir(folder), key=lambda entry: entry.name)
        except OSError:
            add(folder.relative_to(root).as_posix(), 'unreadable directory')
            return
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            # Clone metadata is outside the published source tree. No Git
            # commands, index access or subprocesses are used by this checker.
            if relative == '.git':
                continue
            if entry.is_symlink():
                add(relative, 'symbolic link'); continue
            lower = relative.lower()
            if lower.split('/')[0] in PRIVATE_TOP or any(
                    lower == prefix or lower.startswith(prefix + '/') for prefix in PRIVATE_PREFIXES) or (
                    entry.is_dir(follow_symlinks=False) and entry.name.lower() in {'accounts','credentials','secrets'}):
                add(relative, 'private or generated tree'); continue
            name = entry.name.lower()
            if name in PRIVATE_NAMES or name.startswith('.env.') and name != '.env.example':
                add(relative, 'private data filename'); continue
            if entry.is_dir(follow_symlinks=False):
                walk(path); continue
            try:
                info = entry.stat(follow_symlinks=False)
                if not stat.S_ISREG(info.st_mode):
                    add(relative, 'non-regular file'); continue
                if info.st_nlink > 1:
                    add(relative, 'hard-linked file'); continue
                if path.suffix.lower() in ARTIFACT_SUFFIXES:
                    add(relative, 'binary, archive or raw diagnostic artifact'); continue
                if info.st_size > MAX_BYTES:
                    add(relative, 'file exceeds review scan limit'); continue
                # Latin-1 preserves byte values so accidental secrets embedded
                # in a binary/font/prebuilt asset can still match ASCII rules.
                text = path.read_bytes().decode('latin-1')
            except OSError:
                add(relative, 'unreadable file'); continue
            for reason in content_reasons(relative, text):
                add(relative, reason)
    walk(root)
    return sorted(issues, key=lambda item: (item['path'], item['reason']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', default='.', type=Path)
    parser.add_argument('--json', action='store_true', help='Print filename/reason objects only.')
    args = parser.parse_args()
    issues = scan(args.root)
    if args.json:
        print(json.dumps(issues, indent=2))
    elif issues:
        for issue in issues:
            print(f"{issue['path']}: {issue['reason']}")
    else:
        print('Public tree check passed; manual publication review is still required.')
    return 1 if issues else 0


if __name__ == '__main__':
    raise SystemExit(main())
