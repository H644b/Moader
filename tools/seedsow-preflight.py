#!/usr/bin/env python3
"""Capture bounded Seedsow Demo helper signatures without acquiring FS or changing code."""
from __future__ import annotations
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
LIMIT = 64 * 1024


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, target):
        raise ValueError('Preflight redirect rejected; request was not replayed')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def plan(profile):
    require(profile['id'] == 'seedsow-malev0' and profile['target'] == 'seedsow', 'Wrong provider')
    rows = []
    for item in profile['codeQualification']['signatures']:
        rows.append({**item, 'length': len(bytes.fromhex(item['bytes'])),
                     'requiredForNativeHandoff': item.get('requiredForNativeHandoff', True)})
    for item in profile['codeQualification']['unqualifiedPrimitives']:
        row = dict(item)
        rows.append({**row, 'length': 128, 'requiredForNativeHandoff': True})
    require(1 <= len(rows) <= 32, 'Unexpected signature count')
    names = set()
    for row in rows:
        require(re.fullmatch(r'[a-z][a-z0-9-]{0,63}', row['name']) and row['name'] not in names, 'Invalid signature name')
        require(row['mapping'] in ('sdk', 'main') and isinstance(row['offset'], int) and 0 < row['offset'] < 0x2000000, 'Invalid signature address')
        require(0 < row['length'] <= 4096, 'Signature exceeds bound')
        names.add(row['name'])
    return rows


def lua_source(rows, nonce):
    require(re.fullmatch(r'[a-f0-9]{32}', nonce), 'Invalid challenge')
    # N.peek is destructive: it changes a Lua type word at address+8. Do not
    # use it for code inspection. The launcher's emitter peek reads the source
    # and stores into our own private string buffer instead.
    source = '''local N=assert(_G.n2h,"n2h is not ready")
assert(N.platform=="Ounce","Switch 2 required")
assert(N.game_id==nil or N.game_id=="seedsow","Launcher game identity differs")
assert(type(N.run_chain)=="function" and type(N.str_addr)=="function","Read emitter unavailable")
assert(N.offsets_main.malloc==0x262160 and N.offsets_main.free==0x2621b0,"Demo allocator profile differs")
assert(N.offsets_sdk.longjmp==0x5511b4 and N.offsets_sdk.vcall8_restore==0x3d9e54,"Demo SDK profile differs")
assert(N.gadgets.g_call2==0xc27d8 and N.gadgets.g_store_x0_x19==0x173db4,"Demo emitter profile differs")
local SDK,MAIN=N.sdk_base,N.main_base
assert(type(SDK)=="number" and type(MAIN)=="number" and SDK>0 and MAIN>0 and SDK%4096==0 and MAIN%4096==0,"Invalid module bases")
local function readCode(address,n)
 local b=string.rep("\\0",math.ceil(n/8)*8+16)
 local dst=N.str_addr(b)+0x18
 for at=0,math.ceil(n/8)*8-1,8 do
  N.run_chain(function(E) E:peek(address+at,dst+at) end)
 end
 return (b:sub(1,n):gsub(".",function(c) return string.format("%02x",c:byte()) end))
end
'''
    source += 'print("MOADER_PREFLIGHT ' + nonce + '")\n'
    source += 'print(string.format("BASE sdk %.0f",SDK))\nprint(string.format("BASE main %.0f",MAIN))\n'
    for row in rows:
        base = 'SDK' if row['mapping'] == 'sdk' else 'MAIN'
        source += f'print("CODE {row["name"]} "..readCode({base}+{row["offset"]},{row["length"]}))\n'
    source += 'print("MOADER_PREFLIGHT_END ' + nonce + '")\n'
    require(len(source.encode()) < 16 * 1024, 'Preflight request exceeds bound')
    return source


def classify_samples(code, rows):
    require(set(code) == {row['name'] for row in rows}, 'Incomplete or unexpected code samples')
    samples = []
    for row in rows:
        raw = bytes.fromhex(code[row['name']])
        require(len(raw) == row['length'], 'Truncated code sample: ' + row['name'])
        expected = row.get('bytes')
        samples.append({'name': row['name'], 'mapping': row['mapping'], 'offset': row['offset'],
                        'requiredForNativeHandoff': row['requiredForNativeHandoff'],
                        'bytes': raw.hex(), 'sha256': hashlib.sha256(raw).hexdigest(),
                        'matchesPinnedBytes': None if expected is None else raw.hex() == expected})
    required = [sample for sample in samples if sample['requiredForNativeHandoff']]
    return {'samples': samples,
            'allExistingSignaturesMatch': all(s['matchesPinnedBytes'] is not False for s in samples),
            'allRequiredPinnedSignaturesMatch': all(
                s['matchesPinnedBytes'] is not False for s in required),
            'requiredPrimitiveBaselinesComplete': all(
                s['matchesPinnedBytes'] is not None for s in required),
            'qualificationComplete': False,
            'reason': 'Required helper samples still need independent expected-code baselines; '
                      'a self-capture is not an expected-code baseline. Nonrequired try-wait '
                      'samples are retained only as source-drift evidence because the '
                      'PM-completion bootstrap does not call pthread_trywait.'}


def parse_response(body, rows, nonce):
    require(len(body) <= LIMIT, 'Preflight response exceeds bound')
    text = body.decode('utf-8', errors='strict')
    require(text.startswith('status: ok\n') or text.startswith('status: ok\r\n'), 'Launcher did not accept preflight')
    start, end = 'MOADER_PREFLIGHT ' + nonce, 'MOADER_PREFLIGHT_END ' + nonce
    lines = text.splitlines()
    require(lines.count(start) == 1 and lines.count(end) == 1, 'Missing, repeated or stale challenge')
    first, last = lines.index(start), lines.index(end)
    require(first < last, 'Preflight challenge order differs')
    bases, code = {}, {}
    for line in lines[first+1:last]:
        base = re.fullmatch(r'BASE (sdk|main) ([0-9]+)', line)
        sample = re.fullmatch(r'CODE ([a-z][a-z0-9-]{0,63}) ([a-f0-9]+)', line)
        if base:
            key, value = base[1], int(base[2])
            require(key not in bases and 0 < value < 2**48 and value % 4096 == 0, 'Invalid or repeated module base')
            bases[key] = value
        elif sample:
            require(sample[1] not in code, 'Repeated code sample')
            code[sample[1]] = sample[2]
        else:
            raise ValueError('Unexpected preflight output')
    require(set(bases) == {'sdk', 'main'}, 'Incomplete module identity')
    return {'moduleBases': bases, **classify_samples(code, rows)}


def review_report(report, profile):
    """Reclassify an existing capture without contacting the console or writing files."""
    require(isinstance(report, dict) and report.get('schema') == 1, 'Unsupported preflight report')
    require(report.get('provider') == profile['id'] and report.get('revision') == profile['revision'],
            'Preflight provider identity differs')
    require(report.get('complete') is True and report.get('fsAcquisitionAttempted') is False and
            report.get('codeOrKernelPatchAttempted') is False and
            report.get('nativeHandoffReady') is False, 'Preflight report boundary differs')
    bases = report.get('moduleBases')
    require(isinstance(bases, dict) and set(bases) == {'sdk', 'main'} and all(
        isinstance(value, int) and 0 < value < 2**48 and value % 4096 == 0
        for value in bases.values()), 'Invalid captured module identity')
    captured = report.get('samples')
    require(isinstance(captured, list), 'Missing captured samples')
    code = {}
    for sample in captured:
        require(isinstance(sample, dict) and re.fullmatch(r'[a-z][a-z0-9-]{0,63}', sample.get('name', '')),
                'Invalid captured sample')
        require(sample['name'] not in code and isinstance(sample.get('bytes'), str),
                'Repeated or invalid captured sample')
        raw = bytes.fromhex(sample['bytes'])
        require(sample.get('sha256') == hashlib.sha256(raw).hexdigest(),
                'Captured sample digest differs: ' + sample['name'])
        code[sample['name']] = raw.hex()
    return {'consoleConnected': False, 'sourceReportComplete': True, 'moduleBases': bases,
            **classify_samples(code, plan(profile))}


def capture(console, port, output, profile, opener=None):
    address = ipaddress.ip_address(console)
    require(address.version == 4 and not address.is_loopback and not address.is_unspecified and not address.is_multicast, 'Provide the Switch IPv4 address')
    require(1 <= port <= 65535, 'Invalid HTTP port')
    nonce, rows = secrets.token_hex(16), plan(profile)
    output = Path(output)
    require(not output.exists(), 'Use a new private output directory')
    output.mkdir(mode=0o700, parents=True)
    record = {'schema': 1, 'purpose': 'Seedsow Demo read-only helper preflight', 'provider': profile['id'],
              'revision': profile['revision'], 'console': str(address), 'port': port,
              'challenge': nonce, 'fsAcquisitionAttempted': False, 'codeOrKernelPatchAttempted': False,
              'nativeHandoffReady': False, 'complete': False}
    file = output/'preflight.json'
    def save():
        with open(file, 'w', encoding='utf-8') as stream:
            os.chmod(file, 0o600)
            json.dump(record, stream, indent=2)
            stream.write('\n')
    save()
    try:
        if opener is None:
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect()).open
        script = lua_source(rows, nonce).encode()
        request = urllib.request.Request(f'http://{address}:{port}/eval', data=script,
                                         headers={'Content-Type': 'text/plain'}, method='POST')
        # Exactly one request. A timeout/error is retained, never retried.
        with opener(request, timeout=30) as response:
            require(response.status == 200, 'Launcher HTTP status differs')
            body = response.read(LIMIT+1)
        require(len(body) <= LIMIT, 'Preflight response exceeds bound')
        reply = output/'launcher-reply.txt'
        reply.write_bytes(body)
        reply.chmod(0o600)
        record.update(parse_response(body, rows, nonce))
        record['complete'] = True
        save()
        return record
    except Exception as error:
        record['error'] = str(error)
        save()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--console', help='Explicit current Switch address; no address discovery')
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--plan', action='store_true', help='Inspect the local read plan; no connection')
    parser.add_argument('--review-report', type=Path,
                        help='Reclassify an existing private capture read-only; no connection or write')
    args = parser.parse_args()
    profile = json.loads((ROOT/'config/seedsow-provider.json').read_text())
    rows = plan(profile)
    if args.plan:
        require(not args.console and not args.output and not args.review_report,
                '--plan does not accept a live console, output or report')
        print(json.dumps({'consoleConnected': False, 'signatures': len(rows), 'bytes': sum(r['length'] for r in rows), 'fsAcquisitionAttempted': False}))
        return
    if args.review_report:
        require(not args.console and not args.output,
                '--review-report does not accept a live console or output')
        record = json.loads(args.review_report.read_text())
        value = review_report(record, profile)
        print(json.dumps({key: value[key] for key in ('consoleConnected', 'sourceReportComplete',
              'allExistingSignaturesMatch', 'allRequiredPinnedSignaturesMatch',
              'requiredPrimitiveBaselinesComplete', 'qualificationComplete', 'reason')}))
        return
    require(args.console and args.output, 'Provide an explicit current --console and a new --output directory')
    value = capture(args.console, args.port, args.output, profile)
    print(json.dumps({'report': str(args.output/'preflight.json'), 'samples': len(value['samples']), 'pinnedSignaturesMatch': value['allExistingSignaturesMatch'], 'nativeHandoffReady': False}))


if __name__ == '__main__':
    main()
