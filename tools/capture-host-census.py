#!/usr/bin/env python3
"""Capture only an explicitly supplied Moader census listener; never discover it.

A failed GET means the listener's state is unknown, not that a console process
has exited. This tool sends GET only and never initiates recovery or writes files
on the console. The explicitly selected output is written on this computer.
"""
import argparse
import datetime
import getpass
import http.client
import json
import math
import os
import pathlib
import stat
import time
import urllib.error
import urllib.parse
import urllib.request

ROUTE = '/api/diagnostics/host-census'
MAX_BODY = 256 * 1024


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def endpoint(value):
    try:
        parsed = urllib.parse.urlsplit(value)
        valid = (not any(c.isspace() or ord(c) < 32 or ord(c) == 127 for c in value)
                 and parsed.scheme in ('http', 'https') and parsed.hostname and parsed.port
                 and not parsed.username and not parsed.password and parsed.path == ROUTE
                 and not parsed.query and not parsed.fragment)
    except ValueError:
        valid = False
    if not valid:
        raise argparse.ArgumentTypeError('Supply the exact listener URL, including its explicit port and host-census route, without credentials or parameters.')
    return value


def duration(value):
    n = float(value)
    if not math.isfinite(n) or not 0.05 <= n <= 60:
        raise argparse.ArgumentTypeError('Use a finite duration from 0.05 to 60 seconds.')
    return n


def read_token(path):
    if path is None:
        value = getpass.getpass('Pairing code (hidden): ')
    else:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0))
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077 or info.st_nlink != 1 or info.st_size > 32:
                raise ValueError('Token file must be owned by you, regular, without links and private (0600).')
            value = os.read(fd, 32).decode('ascii').strip()
        finally:
            os.close(fd)
    if len(value) != 6 or any(c not in '0123456789' for c in value):
        raise ValueError('Pairing code must contain exactly six digits.')
    return value


def capture(url, token, stream, interval, timeout, count=0):
    # No environment proxies, redirects, cookies, discovery, remembered endpoint
    # or recovery actions. Each request targets precisely the supplied URL.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    attempt = 0
    while not count or attempt < count:
        started = time.monotonic()
        entry = {'capturedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                 'attempt': attempt + 1, 'listenerState': 'unknown'}
        stop = False
        try:
            req = urllib.request.Request(url, headers={'Authorization': 'Bearer ' + token,
                                                       'Accept': 'application/json'}, method='GET')
            with opener.open(req, timeout=timeout) as response:
                body = response.read(MAX_BODY + 1)
                if len(body) > MAX_BODY:
                    raise ValueError('Oversized response')
                data = json.loads(body)
                if not isinstance(data, dict) or data.get('schema') != 1 or not isinstance(data.get('available'), bool):
                    raise ValueError('Unexpected response')
                entry.update(listenerState='responded', status=response.status, census=data)
        except urllib.error.HTTPError as error:
            entry.update(listenerState='responded', error='http', status=error.code)
            # Avoid repeated failed pairing attempts; do not print response/error
            # objects, which may include caller-supplied URLs or server text.
            stop = error.code in (401, 403, 429) or 300 <= error.code < 400
        except (urllib.error.URLError, OSError, TimeoutError, http.client.HTTPException):
            entry['error'] = 'transport'
        except (ValueError, UnicodeError):
            entry['error'] = 'invalid-response'
        entry['elapsedSeconds'] = round(time.monotonic() - started, 6)
        stream.write(json.dumps(entry, separators=(',', ':')) + '\n')
        stream.flush()
        attempt += 1
        if stop or (count and attempt >= count):
            return 2 if stop else 0
        time.sleep(max(0, interval - (time.monotonic() - started)))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--endpoint', required=True, type=endpoint)
    parser.add_argument('--output', required=True, type=pathlib.Path, help='New private JSONL file; existing paths are refused.')
    parser.add_argument('--token-file', type=pathlib.Path, help='Private 0600 file, otherwise prompt without echo. Never pass the code on the command line.')
    parser.add_argument('--interval', type=duration, default=1.0)
    parser.add_argument('--timeout', type=duration, default=3.0)
    parser.add_argument('--count', type=int, default=0, help='Stop after this many GETs; zero continues until interrupted.')
    args = parser.parse_args()
    if args.count < 0:
        parser.error('--count cannot be negative')
    try:
        token = read_token(args.token_file)
        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            os.fchmod(stream.fileno(), 0o600)
            return capture(args.endpoint, token, stream, args.interval, args.timeout, args.count)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, UnicodeError):
        print('Capture stopped: check the private token file and new output path. No console recovery was attempted.', file=__import__('sys').stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
