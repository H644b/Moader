#!/usr/bin/env python3
"""Independent Civ7 source/profile checks; synthetic records are never device proof.

Default: read an already provisioned pinned source closure. --online downloads
only the immutable upstream source archive into a temporary directory. Every
Node child denies network/listeners, target processes and unexpected imports.
No acquisition, live run, console endpoint, compiler or native binary executes.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
REVISION = '51a804cbce09216e1a42e65929644fd436b4c8bf'
TREE_SHA = '63bd7bfeb195c7cd7f2ae320414c4bc904755c2bca10b7495acc40f9ef6eec1f'
PROVISION_TREE_SHA = 'd83cadc568b65a724f32b848f1681540487a798b354ac3286f71e41e94a9ac75'
BRIDGE_SHA = 'a984878b95cf32de16619a187dcee3bdd4f9205f149a78daafcdb310b931c32e'
SOURCE_SHA = 'd4bcfcaa1e4debf5dd205a8f99effe38ca272de0d7d9d93d0ef45f7d6c75fe6d'
PROFILE_SHA = '294461fbd97913ad3eace5f4b60f30d3ca4b999b421e8dc16f0e726b45adb690'
SDK_SHA = 'dbb8879c513cbd695104df5192ba894ed9e614fcabd89734f255bcd774a87abb'
THREAD_SHA = '647b0c42cb2898d423e5db7f2fd3c6c879e08d7593d3fcf918f000bf3e7b83e4'
PAYLOAD_SHA = 'd590cbfc5d9152c24d5a36f083f8470a2f3164be6b78130129d44132b939701a'
PROCESS_SHA = 'd15b27c8967fad6307519e3813e8e3d4abdf798a433b28617a9f4a97a8f5b04f'
PROCESS_CLIENT_SHA = '0c3ce1d7d5ef22f3b1c2f5da2f57efcd725a0cd14e27961145a89436dc75333d'
APP_ORDER = ['thread-code.json', 'event-code.json', 'timing-code.json',
             'hash-code.json', 'sdk-proxy-code.json',
             'thread-scheduling-reference.json', 'compat-affinity-reference.json']
GUARD = r'''
const fs=require('node:fs'),Module=require('node:module');
function deny(){fs.writeFileSync(process.env.MOADER_CIV7_IO_SENTINEL,'unexpected IO');throw Error('Civ7 host test forbids network/listeners/target processes');}
const net=require('node:net'),http=require('node:http'),https=require('node:https'),tls=require('node:tls'),dgram=require('node:dgram'),cp=require('node:child_process');
net.Socket.prototype.connect=net.Server.prototype.listen=deny;
http.request=http.get=https.request=https.get=tls.connect=deny;
dgram.Socket.prototype.bind=dgram.Socket.prototype.connect=dgram.Socket.prototype.send=deny;
for(const name of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[name]=deny;
globalThis.fetch=deny;
const original=Module._load;
Module._load=function(name,parent,main){
 const file=Module._resolveFilename(name,parent,main);
 if(typeof file==='string'&&/\/(?:rpc|endpoint|fs-memory|pipeline)\.cjs$/.test(file))deny();
 return original.apply(this,arguments);
};
'''
# These upstream APIs are pure source/artifact operations. Importing run.cjs
# does not invoke main. Any accidental network/subprocess call remains fatal.
SOURCE_HARNESS = r'''
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
const source=process.argv[1],run=process.argv[2];
const target=require(path.join(source,'engine/shared/target.cjs'));
(async()=>{
 const profile=target.loadTarget('civ7',{targetRoot:path.join(source,'targets/civ7'),portRoot:source});
 const actual=await require(path.join(source,'engine/run.cjs')).sourceDigest(profile);
 assert.equal(actual,process.env.MOADER_CIV7_EXPECTED_SOURCE);
 await target.materializeTarget(profile,run);
 const materialized=target.loadTarget('civ7',{materializedRoot:run,portRoot:source});
 assert.equal(materialized.name,'civ7');
 assert.equal(target.sdkFunctions(materialized).allocate.offset,0x512f40);
 assert.throws(()=>target.loadTarget('smilebasic',{materializedRoot:run}),/Unsupported target configuration/);
 console.log(JSON.stringify({sourceDigest:actual,target:materialized.name}));
})().catch(e=>{console.error(e.stack);process.exitCode=1;});
'''
NATIVE_HARNESS = r'''
const assert=require('node:assert/strict'),path=require('node:path');
const root=process.argv[1],native=path.join(root,'runtime/modloader/lib/native');
const {identifyFs,verifyBulk}=require(path.join(native,'identity.cjs'));
const {validateFsMappings,validateFsAbi}=require(path.join(native,'fs-abi.cjs'));
const {selectNotifier}=require(path.join(native,'bridge.cjs'));
(async()=>{
 const identity={pid:8n,programId:'0400000000000000'},data=Buffer.alloc(64),bytes=Buffer.from('native');
 data.writeBigUInt64LE(identity.pid);data.writeBigUInt64LE(BigInt('0x'+identity.programId),8);data.writeBigUInt64LE(BigInt(bytes.length),48);
 const reply={result:0,data,length:bytes.length,bytes};
 assert.deepEqual(verifyBulk(reply,identity,bytes.length),bytes);
 assert.throws(()=>verifyBulk(reply,{...identity,pid:9n},bytes.length),/identity/);
 for(const offset of [32,40,48]){
  const wrong=Buffer.from(data);wrong.writeBigUInt64LE(99n,offset);
  assert.throws(()=>verifyBulk({...reply,data:wrong},identity,bytes.length),/identity/);
 }
 assert.throws(()=>verifyBulk({...reply,length:0,bytes:Buffer.alloc(0)},identity,1),/invalid length/);
 assert.throws(()=>verifyBulk({...reply,bytes:Buffer.alloc(1)},identity,bytes.length),/invalid length/);
 assert.throws(()=>verifyBulk({...reply,data:Buffer.alloc(55)},identity,bytes.length),/invalid length/);
 assert.equal((await identifyFs({info:async()=>({result:0,pid:'8',programId:identity.programId})},1,identity)).pid,8n);
 await assert.rejects(()=>identifyFs({info:async()=>({result:0,pid:'8',programId:'0100c3601518c000'})},1),/not native FS/);
 await assert.rejects(()=>identifyFs({info:async()=>({result:0,pid:'9',programId:identity.programId})},1,identity),/differs/);
 assert.equal(validateFsMappings([{offset:0,end:0x300000,permissions:4}]).loggingExecutable,true);
 assert.throws(()=>validateFsMappings([{offset:0,end:0x300000,permissions:1}]),/not executable/);
 assert.throws(()=>validateFsAbi(Buffer.alloc(2416640)),/Unsupported FS function layout/);
 const notifier={E:0x100000001,B:0x100000002},state={worker:{joined:true},notifiers:[42]},proof={notifier:{object:notifier.E,backend:notifier.B,handle:42}};
 assert.equal(selectNotifier(state,proof,notifier),42);
 assert.throws(()=>selectNotifier({...state,worker:{joined:false}},proof,notifier),/active/);
 assert.throws(()=>selectNotifier(state,{notifier:{...proof.notifier,object:notifier.E+1}},notifier),/differs/);
 assert.throws(()=>selectNotifier({...state,notifiers:[43]},proof,notifier),/unavailable/);
 console.log('Native FS identity, alias cleanup, mapping, function and notifier gates retained');
})().catch(e=>{console.error(e.stack);process.exitCode=1;});
'''


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files_below(root):
    require(root.is_dir() and not root.is_symlink(), 'Expected ordinary source directory')
    result = {}
    for file in sorted(root.rglob('*')):
        require(not file.is_symlink(), 'Symlink in source closure')
        if file.is_file():
            result[file.relative_to(root).as_posix()] = file.read_bytes()
    return result


def closure(source):
    return {prefix + '/' + name: data for prefix in ('engine', 'targets/civ7')
            for name, data in files_below(source / prefix).items()}


def tree_sha(files):
    return sha(''.join(name + '\0' + sha(data) + '\n'
                       for name, data in sorted(files.items())).encode())


def source_digest(files):
    # Independent transcription of the pinned runner; artifact order is part
    # of its hash. target.json is deliberately absent from this upstream digest.
    names = ['engine/' + name for name in
             ('run.cjs', 'rpc.cjs', 'dump-firmware.cjs', 'bootstrap.bas')]
    names += [name for name in files if name.startswith(
        ('engine/lib/', 'engine/shared/', 'engine/switch2/'))]
    entries = [[name.removeprefix('engine/'), sha(files[name])] for name in sorted(names)]
    artifacts = ['targets/civ7/sdk-functions.json', 'targets/civ7/sdk-layout.json']
    artifacts += ['targets/civ7/app/' + name for name in APP_ORDER]
    entries += [['artifact/' + name, sha(files[name])] for name in artifacts]
    return sha(json.dumps(entries, separators=(',', ':')).encode()), len(entries)


def materialized_files(files):
    result = {name.removeprefix('engine/'): data for name, data in files.items()
              if name.startswith(('engine/lib/', 'engine/shared/', 'engine/switch2/'))}
    for name in ('target.json', 'sdk-functions.json', 'sdk-layout.json'):
        result['shared/' + name] = files['targets/civ7/' + name]
    for name in APP_ORDER:
        result['switch2/app/' + name] = files['targets/civ7/app/' + name]
    return result


def write_files(directory, files):
    for name, data in files.items():
        file = directory / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(data)


def pinned_online_source(directory):
    url = f'https://codeload.github.com/s8n/civ7-fs-kaihen/tar.gz/{REVISION}'
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'Moader-Civ7-host-test'}), timeout=60) as reply:
        data = reply.read((32 << 20) + 1)
    require(len(data) <= 32 << 20, 'Pinned archive exceeds bound')
    prefix = f'civ7-fs-kaihen-{REVISION}/'
    selected = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        rows = archive.getmembers()
        require(len(rows) <= 4096, 'Too many upstream archive entries')
        for row in rows:
            require(not row.issym() and not row.islnk(), 'Upstream archive has links')
            if not row.name.startswith(prefix) or row.isdir():
                continue
            name = row.name[len(prefix):]
            if not name.startswith(('engine/', 'targets/civ7/')) and name != 'bridge/civ_bridge.js':
                continue
            require(row.isfile() and 0 < row.size <= 8 << 20, 'Invalid upstream source member')
            require('\\' not in name and all(part not in ('', '.', '..') for part in name.split('/')),
                    'Invalid upstream source path')
            require(name not in selected, 'Duplicate upstream source path')
            selected[name] = archive.extractfile(row).read()
    require(tree_sha(selected) == PROVISION_TREE_SHA, 'Immutable upstream closure differs from independent pin')
    write_files(directory, selected)
    return directory


def node_run(node, guard, sentinel, code, *args, expected=0):
    env = {name: value for name, value in os.environ.items()
           if not name.startswith(('NS2_', 'NODE_', 'MOADER_CIV7_'))}
    env.update(NODE_OPTIONS='--require=' + str(guard), MOADER_CIV7_IO_SENTINEL=str(sentinel),
               MOADER_CIV7_EXPECTED_SOURCE=SOURCE_SHA)
    result = subprocess.run([node, '-e', code, *map(str, args)], cwd=ROOT,
                            env=env, text=True, capture_output=True, timeout=60)
    require(not sentinel.exists(), 'Node attempted forbidden IO')
    require(result.returncode == expected, 'Node host-only check failed\n' + result.stdout[-2000:] + result.stderr[-4000:])
    return result.stdout


def check_sources(node, source, work, guard, sentinel):
    files = closure(source)
    require(len(files) == 119 and sum(map(len, files.values())) == 707389,
            'Unexpected source closure size/count')
    require(tree_sha(files) == TREE_SHA, 'Pinned source closure differs')
    require(source_digest(files) == (SOURCE_SHA, 113), 'Independent sourceDigest differs')
    provisioned = {**files, 'bridge/civ_bridge.js': (source / 'bridge/civ_bridge.js').read_bytes()}
    require(len(provisioned) == 120 and sum(map(len, provisioned.values())) == 720866 and
            sha(provisioned['bridge/civ_bridge.js']) == BRIDGE_SHA and
            tree_sha(provisioned) == PROVISION_TREE_SHA, '120-file provisioning closure differs')
    for name, expected in {
        'targets/civ7/target.json': PROFILE_SHA,
        'targets/civ7/sdk-functions.json': SDK_SHA,
        'targets/civ7/app/thread-code.json': THREAD_SHA,
        'engine/switch2/ro/payload/main': PAYLOAD_SHA,
        'engine/switch2/ro/payload/process.c': PROCESS_SHA,
        'engine/switch2/process/client.cjs': PROCESS_CLIENT_SHA,
    }.items():
        require(sha(files[name]) == expected, 'Pinned file differs: ' + name)
    require(len(files['engine/switch2/ro/payload/main']) == 4757, 'Diagnostic payload length differs')
    manifest = json.loads(files['engine/switch2/ro/payload/manifest.json'])
    require(manifest['payloadSha256'] == PAYLOAD_SHA and manifest['programId'] == '0400000000000037',
            'Diagnostic manifest identity differs')
    require('engine/switch2/ro/package-build.cjs' not in files, 'Upstream package-build assumption changed')
    # Byte-identity is stronger than matching method names or a copied manifest.
    legacy = ROOT / 'research/sources/ns2jb/engine'
    if legacy.is_dir():
        for name in ('ro/payload/main', 'ro/payload/main.c', 'ro/payload/process.c',
                     'ro/payload/start.S', 'ro/payload/layout.ld', 'ro/payload/manifest.json',
                     'process/client.cjs', 'process/fs-execute.cjs', 'process/bind-notifier.cjs'):
            require((legacy / 'switch2' / name).read_bytes() == files['engine/switch2/' + name],
                    'Retained diagnostic ABI differs: ' + name)
    run = work / 'materialized/switch2-test'
    predicted = materialized_files(files)
    config = json.loads((ROOT / 'config/civ7-provider.json').read_bytes())
    require(config['schemaVersion'] == 1 and config['id'] == 'civ7-s8n' and
            config['revision'] == REVISION and config['sourceSha256'] == SOURCE_SHA and
            config['sourceTreeSha256'] == PROVISION_TREE_SHA, 'Production profile identity differs from independent pins')
    require(config['files'] == {name: sha(data) for name, data in provisioned.items()},
            'Production source file allowlist differs from independent closure')
    require(config['runFiles'] == {name: sha(data) for name, data in predicted.items()},
            'Production run allowlist differs from independent materialization')
    write_files(run, {name: data for name, data in predicted.items()
                      if name.startswith(('lib/', 'shared/', 'switch2/')) and
                      name not in {'shared/target.json', 'shared/sdk-functions.json', 'shared/sdk-layout.json'} and
                      name not in {'switch2/app/' + name for name in APP_ORDER}})
    node_run(node, guard, sentinel, SOURCE_HARNESS, source, run)
    require(files_below(run) == predicted, 'Actual upstream materialization differs from independent reproduction')
    changed = dict(files)
    changed['targets/civ7/target.json'] += b'\n'
    require(source_digest(changed)[0] == SOURCE_SHA and tree_sha(changed) != TREE_SHA,
            'Upstream sourceDigest must not be mistaken for the full target/closure identity')
    print('PASS: authentic 119-file closure, 113-entry sourceDigest and upstream materialization independently match pins.')
    print('PASS: 120-file provisioning closure and production source/run allowlists independently match authentic bytes.')
    print('PASS: diagnostic RO binary/source/process-client ABI identity; full profile pin covers sourceDigest target.json omission.')
    return files, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node', default=shutil.which('node') or 'node')
    parser.add_argument('--source', type=Path, default=ROOT / 'research/sources/civ7-fs-kaihen')
    parser.add_argument('--online', action='store_true', help='Fetch only the immutable pinned upstream source archive')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='moader-civ7-host-') as temporary:
        work = Path(temporary)
        source = pinned_online_source(work / 'source') if args.online else args.source.resolve()
        guard, sentinel = work / 'deny-io.cjs', work / 'forbidden-io'
        guard.write_text(GUARD)
        check_sources(args.node, source, work, guard, sentinel)
        node_run(args.node, guard, sentinel, NATIVE_HARNESS, ROOT)
        print('PASS: same-session native FS identity, alias cleanup, mappings, function hashes and owned-notifier rejection checks.')
        print('Host-only checks complete. No console/RPC, target process, acquisition or native binary executed.')


if __name__ == '__main__':
    main()
