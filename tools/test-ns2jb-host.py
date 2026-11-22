#!/usr/bin/env python3
"""Host-only selected-profile preparation test; --online fetches pinned public data.

Never run upstream run.cjs/rpc.cjs, execute a target binary, or execute the
printed installation command. Synthetic run files are not live device proof.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import importlib.util
import io
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from unittest.mock import patch

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent.parent
BACKEND_URL = 'https://github.com/H644b/Moader/releases/download/v0.1.0-dev/Moader-backend-v0.1.0-dev.zip'
BACKEND_SHA = '1af8f2fad0013ae83e2fcdd854a94002e68b75ab672f95d950c08410d40c5897'
BINARY_SHA = {
    'dist/Moader.nro':'46089375b83380b63812767532637f4f48007ea6f39cc0e54ca4de7bc42cfa6a',
    'dist/moader.nso':'71cdeafc9a13a65637ecf4e4d94f5c7525520e9a005e4bace6a50a73a87c0785',
    'vendor/kaihen-mo/hen/main':'e6341b1932b450e25f04ccf425c3034bd45dd3cccfadbf5e5fb2778b6a673e5c',
}
TOOLS = ['setup-backend.py','install-from-rpc.py','install-moader.cjs','install-inputs.cjs',
         'install-kaihen.cjs','install-runtime.cjs','ready-kaihen.cjs','sd-install.cjs']
GUARD = r'''
const fs=require('node:fs'),Module=require('node:module');
function deny(){fs.writeFileSync(process.env.MOADER_HOST_IO_SENTINEL,'unexpected Node IO');throw Error('Host test forbids network, RPC and child execution');}
const net=require('node:net'),http=require('node:http'),https=require('node:https'),tls=require('node:tls'),dgram=require('node:dgram'),cp=require('node:child_process');
net.Socket.prototype.connect=net.Server.prototype.listen=deny;
http.request=http.get=https.request=https.get=tls.connect=deny;
dgram.Socket.prototype.bind=dgram.Socket.prototype.connect=dgram.Socket.prototype.send=deny;
for(const key of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[key]=deny;
const load=Module._load;
Module._load=function(name,parent,main){
 const file=Module._resolveFilename(name,parent,main);
 if(typeof file==='string' && (file.includes('/research/sources/') || /\/(?:run|rpc|endpoint)\.cjs$/.test(file)))deny();
 return load.apply(this,arguments);
};
'''
GATE = r'''
const {verifiedRun}=require(process.argv[1]);
try{verifiedRun(process.argv[2]);console.log('ACCEPT');}
catch(error){console.log('REJECT');process.exitCode=3;}
'''
PYTHON_GUARD = r'''
import json,os,sys
from pathlib import Path
node=os.environ['MOADER_HOST_NODE'];kit=Path(os.environ['MOADER_HOST_KIT'])
python=os.environ['MOADER_HOST_PYTHON']
profile=os.environ['MOADER_HOST_PROFILE']
profile_args=['--host-profile',profile] if profile!='smilebasic-kurumuz' else []
probe='require("./runtime/modloader/lib/native/artifacts.cjs").artifacts(); console.log("Native source/prebuilt hashes verified (local only)")'
def audit(event,values):
 if event!='subprocess.Popen':return
 executable,argv,cwd,env=values
 kind=None
 if isinstance(argv,(list,tuple)):
  if list(argv)==[node,'--version']:kind='node-version'
  elif list(argv)==[node,'-e',probe]:kind='prebuilt-probe'
  elif list(argv)==[node,str(kit/'tools/install-moader.cjs'),*profile_args,'--check']:kind='installer-check'
  elif list(argv[1:])==[str(kit/'tools/setup-backend.py'),'--root',str(kit),'--node',node,*profile_args] and Path(argv[0]).samefile(python):kind='local-setup'
 if kind is None or str(executable)!=str(argv[0]):
  Path(os.environ['MOADER_HOST_IO_SENTINEL']).write_text('unexpected Python child execution')
  raise RuntimeError('Host test forbids companion CLI/RPC/target process execution')
 with open(os.environ['MOADER_HOST_PROCESS_AUDIT'],'a') as output:output.write(kind+'\n')
sys.addaudithook(audit)
'''


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(command, root, env, expected=0):
    result = subprocess.run(command,cwd=root,env=env,text=True,capture_output=True,timeout=300)
    require(result.returncode == expected, 'Host command failed: '+Path(str(command[0])).name+'\n'+result.stdout[-2000:]+result.stderr[-4000:])
    return result.stdout


def tree(root):
    return {str(p.relative_to(root)):(sha(p.read_bytes()),p.stat().st_mode & 0o777)
            for p in root.rglob('*') if p.is_file()}


def synthetic_run(root):
    directory=root/'upstream run folders/switch2-host'
    (directory/'switch2').mkdir(parents=True)
    result={'verified':True,'rpc':{'mode':'held','state':'listening'}}
    proof={'session':directory.name,'verified':True,'restored':True,
           'identity':{'pid':'8','programId':'0400000000000000'},
           'mapping':{'memory':{'address':'0x100000000'}}}
    current={**proof,'file':'switch2/fs-proof.json','cleanup':{'deferred':True},'retainedEndpoint':{'ready':True}}
    for path,value in [('result.json',result),('switch2/fs-proof.json',proof),('switch2/fs-execution-current.json',current)]:
        (directory/path).write_text(json.dumps(value))
    # Executing this file, or any upstream RPC module, is a hard test failure.
    (directory/'rpc.cjs').write_text("throw Error('Synthetic RPC must never execute');\n")
    return directory,result,current,proof


def gate_checks(node, kit, run_dir, base, env):
    result,current,proof=base
    paths=['result.json','switch2/fs-execution-current.json','switch2/fs-proof.json']
    def save(values):
        for path,value in zip(paths,values):(run_dir/path).write_text(json.dumps(value))
    command=[node,'-e',GATE,str(kit/'runtime/modloader/install.cjs'),str(run_dir)]
    run(command,kit,env)
    variants=[({**result,'completed':'synthetic completion'},current,proof),
              ({**result,'rpc':{'state':'closed'}},current,proof),
              ({**result,'verified':False},current,proof),
              (result,{**current,'verified':False},proof),
              (result,current,{**proof,'verified':False}),
              (result,current,{**proof,'session':'wrong-fixture'})]
    for values in variants:
        save(values);run(command,kit,env,expected=3)
    save(base)
    (run_dir/'switch2/fs-execution-current.json').rename(run_dir/'switch2/ro-frontend-current.json')
    run(command,kit,env,expected=3)
    (run_dir/'switch2/ro-frontend-current.json').rename(run_dir/'switch2/fs-execution-current.json')
    print('PASS: actual verifiedRun accepts held FS shape; rejects completed, closed, unverified, wrong-session and RO-only fixtures.')


def copy_source(kit):
    ignore=shutil.ignore_patterns('__pycache__','.git','state','logs','build','node_modules','.DS_Store')
    for directory in ['include','runtime/modloader/lib','runtime/modloader/native','runtime/kaihen']:
        shutil.copytree(ROOT/directory,kit/directory,ignore=ignore)
    for name in [*(f'tools/{name}' for name in TOOLS),*(p.relative_to(ROOT).as_posix() for p in (ROOT / 'tools').glob('*.cjs')),
                 *(p.relative_to(ROOT).as_posix() for p in (ROOT / 'config').glob('*.json')),'runtime/modloader/install.cjs',
                 'software-builds/kaihen-mo.json','software-builds/kaihen-mo.patch']:
        destination=kit/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,destination)


def baseline_members(data):
    """Select one complete native/proof baseline from a validated ZIP; no extraction."""
    prefix='Moader-backend-v0.1.0-dev/'
    selected={};seen=set();total=0
    with zipfile.ZipFile(io.BytesIO(data)) as package:
        require(len(package.infolist())<=4096,'Baseline archive has too many members')
        for info in package.infolist():
            name=info.filename;parts=name.rstrip('/').split('/')
            require(name and '\\' not in name and '\0' not in name and not PurePosixPath(name).is_absolute() and
                    all(part not in ('','.','..') for part in parts),'Unsafe baseline archive member')
            require(name not in seen,'Duplicate baseline archive member');seen.add(name)
            require(name.startswith(prefix) or name==prefix.rstrip('/'),'Unexpected baseline archive root')
            kind=stat.S_IFMT(info.external_attr>>16)
            require(kind in (0,stat.S_IFREG,stat.S_IFDIR) and not info.flag_bits&1,'Baseline links/special/encrypted members are forbidden')
            require((info.is_dir() and kind in (0,stat.S_IFDIR)) or (not info.is_dir() and kind in (0,stat.S_IFREG)),
                    'Baseline member type disagrees with its name')
            require(0<=info.file_size<=32<<20,'Baseline member exceeds size bound')
            total+=info.file_size;require(total<=64<<20,'Baseline archive exceeds total size bound')
            if info.is_dir():continue
            relative=name[len(prefix):]
            if relative in BINARY_SHA or relative in ('software-builds/kaihen-mo.json','software-builds/kaihen-mo.patch') or relative.startswith('runtime/kaihen/'):
                selected[relative]=package.read(info)
        require(package.testzip() is None,'Baseline archive CRC differs')
    for name,expected in BINARY_SHA.items():require(name in selected and sha(selected[name])==expected,'Baseline binary checksum mismatch')
    require('software-builds/kaihen-mo.json' in selected and selected.get('software-builds/kaihen-mo.patch'),'Missing baseline proof/patch')
    proof=json.loads(selected['software-builds/kaihen-mo.json'])
    require(isinstance(proof,dict) and proof.get('schema',1)==1 and proof.get('hen_sha256')==BINARY_SHA['vendor/kaihen-mo/hen/main'] and
            not any(key in proof for key in ('components','bootstrap_sha256','source_tree_sha256','protocol_version')),'Baseline proof is not the qualified legacy HEN')
    for required in ('runtime/kaihen/hen/Makefile','runtime/kaihen/hen/source/main.cpp','runtime/kaihen/bootstrap/Makefile',
                     'runtime/kaihen/bootstrap/source/main.cpp','runtime/kaihen/libkaihen/include/kaihen.h'):
        require(selected.get(required),'Incomplete baseline native source closure')
    return selected


def release_binaries(kit, work, baseline=False):
    proof=json.loads((kit/'software-builds/kaihen-mo.json').read_text())
    if proof.get('schema') in (2,3) and not baseline:
        spec=json.loads((kit/'config/release.json').read_text())
        descriptors={
            'dist/Moader.nro':spec['binaries']['Moader.nro'],
            'dist/moader.nso':spec['binaries']['moader.nso'],
            'vendor/kaihen-mo/hen/main':spec['patched_hen'],
            'vendor/kaihen-mo/bootstrap.bin':spec['patched_bootstrap'],
        }
        if proof['schema']==3:
            descriptors['vendor/kaihen-mo/ro/main']=spec['patched_ro']
        expected={}
        for relative,item in descriptors.items():
            require(item['source']==relative,'Unexpected candidate binary source')
            source=ROOT/relative;require(source.is_file() and not source.is_symlink(),'Missing pinned local candidate bytes')
            payload=source.read_bytes();require(len(payload)==item['bytes'] and sha(payload)==item['sha256'],'Candidate binary checksum/size mismatch')
            if relative.startswith('vendor/'):
                name='hen/main' if relative.endswith('/hen/main') else 'ro/main' if relative.endswith('/ro/main') else 'bootstrap.bin'
                require(proof['components'][name]=={'bytes':len(payload),'sha256':sha(payload)},'Candidate pair/config proof differs')
            destination=kit/relative;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(payload);expected[relative]=sha(payload)
        print('PASS: current recorded candidate app/service and matched native component pins agree; bytes treated only as data, no console-test claim.')
        return expected
    require(baseline,'Current candidate mode requires schema-2 or schema-3 paired inputs; public legacy compatibility requires explicit --baseline')
    request=urllib.request.Request(BACKEND_URL,headers={'User-Agent':'Moader-host-integration'})
    with urllib.request.urlopen(request,timeout=90) as response:
        data=response.read((8<<20)+1)
    require(len(data)<=8<<20 and sha(data)==BACKEND_SHA,'Pinned public backend kit checksum/size mismatch')
    selected=baseline_members(data)
    # This is the disposable test kit, never ROOT or a mutable development
    # checkout. Replace its entire native closure so no protocol-7 source can
    # be combined accidentally with the pinned legacy binaries/proof.
    require(kit.resolve().is_relative_to(work.resolve()) and kit.resolve()!=ROOT.resolve(),'Baseline destination is not an isolated test kit')
    if (kit/'runtime/kaihen').exists():shutil.rmtree(kit/'runtime/kaihen')
    for relative,payload in selected.items():
        destination=kit/relative;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(payload)
    require((kit/'dist/Moader.nro').read_bytes()[16:20]==b'NRO0','Missing NRO header')
    require((kit/'dist/moader.nso').read_bytes()[:4]==b'NSO0','Missing resident NSO header')
    print('PASS: current host tools with the immutable public legacy baseline: three checked binaries plus its matching proof/patch and complete native source; not current candidate validation.')
    return BINARY_SHA


def paired_setup_checks(node, work):
    """Synthetic proof/bytes only; no compiler, network, Git or console call."""
    def module(name, file):
        spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/file)
        loaded = importlib.util.module_from_spec(spec); spec.loader.exec_module(loaded); return loaded
    backend = module('paired_backend_test', 'setup-backend.py')
    package = module('paired_release_test', 'package-release.py')
    root = (work/'paired source kit').resolve();root.mkdir()
    def put(name, data):
        destination=root/name;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data);return destination
    def zipped(files):
        data=io.BytesIO()
        with zipfile.ZipFile(data,'w') as archive:
            for name,value in files.items():archive.writestr(name,value)
        return data.getvalue()
    for name,data in {
        'runtime/kaihen/libkaihen/include/kaihen/protocol.h':b'#define HEN_PROTOCOL_VERSION 7\n',
        'runtime/kaihen/hen/source/main.cpp':b'fixture HEN source',
        'runtime/kaihen/bootstrap/source/main.cpp':b'fixture bootstrap source',
        'software-builds/kaihen-mo.patch':b'fixture patch',
        'dist/Moader.nro':b'\0'*16+b'NRO0fixture-app',
        'dist/moader.nso':b'NSO0fixture-resident',
    }.items():put(name,data)
    hen,bootstrap=b'NSO0-paired-fixture-HEN',b'paired-flat-bootstrap'
    hen_file=work/'paired HEN';hen_file.write_bytes(hen)
    bootstrap_file=work/'paired bootstrap';bootstrap_file.write_bytes(bootstrap)
    proof={'schema':2,'base_commit':'e897f991524590833b82fbe8d63a3c6775fb8101','release_base':'20261114c',
           'patch':'software-builds/kaihen-mo.patch','protocol_version':7,'native_build_passed':True,'console_retest_passed':False,
           'source_tree_algorithm':backend.SOURCE_TREE_ALGORITHM,'source_tree_sha256':backend.kaihen_source_sha(root),
           'patch_sha256':sha(b'fixture patch'),'hen_sha256':sha(hen),
           'components':{'hen/main':{'bytes':len(hen),'sha256':sha(hen)},'bootstrap.bin':{'bytes':len(bootstrap),'sha256':sha(bootstrap)}}}
    proof_file=put('software-builds/kaihen-mo.json',json.dumps(proof).encode())
    require(backend.proof(root)==proof,'Python rejected valid paired proof')
    js="const p=require(process.argv[1]); p.qualifyKaihenBuild(process.argv[2]).then(x=>console.log(JSON.stringify(x))).catch(e=>{console.error(e.message);process.exitCode=1;});"
    qualified=json.loads(subprocess.check_output([node,'-e',js,str(ROOT/'tools/install-inputs.cjs'),str(root)],text=True))
    require(qualified['schema']==2 and qualified['protocolVersion']==7 and qualified['identity']==sha(proof_file.read_bytes()),'Python/Node proof identity differs')
    official={name:('official-'+name).encode() for name in
              (*backend.KAIHEN_COMPONENTS,*backend.KAIHEN_PROGRAMS,'licenses/COPYING')}
    archive=work/'official fixture.zip';archive.write_bytes(zipped(official))
    engine={'host/index.cjs':b'fixture selected host source'}
    engine_archive=work/'host fixture.zip';engine_archive.write_bytes(zipped({'engine/'+name:data for name,data in engine.items()}))
    args=backend.parser().parse_args(['--root',str(root),'--offline','--kaihen-archive',str(archive),'--engine-archive',str(engine_archive),
                                      '--hen-binary',str(hen_file),'--bootstrap-binary',str(bootstrap_file)])
    with patch.object(backend,'KAIHEN_SHA',sha(archive.read_bytes())),patch.object(backend,'ENGINE_TREE_SHA',backend.tree_sha(engine)),\
         patch.object(backend,'node_check',return_value='v24.0.0'),patch.object(backend.urllib.request,'urlopen',side_effect=AssertionError('No network')):
        result=backend.setup(args)
        require(result['kaihenProofSchema']==2 and result['bootstrapSha256']==sha(bootstrap) and result['kaihenConsoleRetestPassed'] is False,'Setup scope/pair metadata differs')
        require((root/'vendor/kaihen-mo/bootstrap.bin').read_bytes()==bootstrap,'Setup substituted official bootstrap')
        before=tree(root);require(backend.setup(args)['newFiles']==0 and tree(root)==before,'Paired setup is not immutable/idempotent')
        bootstrap_file.write_bytes(official['bootstrap.bin'])
        try:backend.setup(args);raise AssertionError('Old bootstrap accepted')
        except ValueError as error:require('pinned build/tested bytes' in str(error),'Unexpected mixed-pair failure')
        require(tree(root)==before,'Failed pair qualification wrote inputs');bootstrap_file.write_bytes(bootstrap)
    for changed in [dict(proof,schema=3),dict(proof,schema=True),dict(proof,protocol_version=8),dict(proof,base_commit='0'*40),
                    dict(proof,release_base='other'),dict(proof,source_tree_sha256='0'*64),dict(proof,patch_sha256='0'*64),
                    dict(proof,console_retest_passed=None),dict(proof,native_build_passed=False),
                    dict(proof,components={'hen/main':proof['components']['hen/main']})]:
        proof_file.write_text(json.dumps(changed))
        try:backend.proof(root);raise AssertionError('Invalid pair proof accepted')
        except ValueError:pass
    proof_file.write_text(json.dumps(proof))
    # Exercise paired --build without ever invoking a compiler or subprocess.
    for folder in ('include','source','native','branding','web','tests'):put(folder+'/fixture.txt',b'fixture source')
    put('Makefile',b'fixture make');put('tools/embed-web.py',b'fixture embed')
    calls=[]
    def make(command, **kwargs):
        calls.append(command);require(kwargs.get('check') is True,'Build command lacks failure check')
        target=Path(command[2]);require(target.is_relative_to(work),'Build escaped isolated tree')
        if target.name in ('hen','bootstrap'):
            (target/'build').mkdir()
            (target/'build'/('main' if target.name=='hen' else 'bootstrap.bin')).write_bytes(hen if target.name=='hen' else bootstrap)
        else:
            (target/'dist').mkdir()
            for name in ('Moader.nro','moader.nso'):(target/'dist'/name).write_bytes((root/'dist'/name).read_bytes())
    build_work=work/'isolated build';build_work.mkdir();before=tree(root)
    with patch.object(backend,'sdk',return_value=work/'fake sdk'),patch.object(backend.subprocess,'run',side_effect=make):
        pair,_=backend.build_inputs(root,build_work)
    require(pair=={'hen/main':hen,'bootstrap.bin':bootstrap} and len(calls)==3 and tree(root)==before,'Paired build did not remain isolated/complete')
    put('tools/setup-backend.py',(ROOT/'tools/setup-backend.py').read_bytes().replace(
        backend.KAIHEN_SHA.encode(),sha(archive.read_bytes()).encode()))
    put('research/artifacts/kaiHEN-20261114c.zip',archive.read_bytes())
    release={'patched_hen':{'source':'vendor/kaihen-mo/hen/main','bytes':len(hen),'sha256':sha(hen)},
             'patched_bootstrap':{'source':'vendor/kaihen-mo/bootstrap.bin','bytes':len(bootstrap),'sha256':sha(bootstrap)}}
    require(package.native_pair(root,release)[:2]==(hen,bootstrap),'Release pair qualification differs')
    for invalid in [{'patched_hen':release['patched_hen']},{**release,'patched_bootstrap':{**release['patched_bootstrap'],'sha256':'0'*64}}]:
        try:package.native_pair(root,invalid);raise AssertionError('Incomplete/mismatched release pair accepted')
        except ValueError:pass
    exported={p.relative_to(root).as_posix():(p.read_bytes(),0o100644) for p in (root/'runtime/kaihen').rglob('*') if p.is_file()}
    package.qualify_exported_native_source(exported,proof)
    exported.pop('runtime/kaihen/bootstrap/source/main.cpp')
    try:package.qualify_exported_native_source(exported,proof);raise AssertionError('Omitted bootstrap source accepted')
    except ValueError:pass
    # Exercise the real ZIP/manifest writer with source/Git/app-build boundaries
    # mocked. No Git command or compiler is executed by this fixture.
    exported={p.relative_to(root).as_posix():(p.read_bytes(),0o100644) for p in (root/'runtime/kaihen').rglob('*') if p.is_file()}
    release.update({'tag':'v0.0.0-fixture','version':'0.0.0','prerelease':True,'repository':'example/Moader','appAndResidentHardwareTested':False,
                    'binaries':{name:{'source':'dist/'+name,'bytes':(root/'dist'/name).stat().st_size,'sha256':sha((root/'dist'/name).read_bytes())}
                                for name in ('Moader.nro','moader.nso')}})
    put('config/release.json',json.dumps(release).encode())
    output=work/'synthetic releases'
    def app_zip(command, **kwargs):
        require(command==[sys.executable,str(root/'tools/package-homebrew.py')] and kwargs.get('check') is True,'Unexpected package child')
        (root/'dist/Moader-homebrew.zip').write_bytes(zipped({'switch/Moader.nro':(root/'dist/Moader.nro').read_bytes(),
                                                            'kaihen/plugins/moader.nso':(root/'dist/moader.nso').read_bytes()}))
    # This fixture validates the paired native ZIP and manifest. Keep unrelated
    # desktop-runtime downloads out of its isolated, offline source tree.
    with patch.object(package,'ROOT',root),patch.object(package,'public_source',return_value=('0'*40,exported)),\
         patch.object(package,'git',return_value=b'1700000000'),patch.object(package.subprocess,'run',side_effect=app_zip),\
         patch.object(package,'host_node_runtimes',return_value={}),\
         patch.object(package,'host_uv_runtimes',return_value={}),\
         patch.object(package,'host_electron_runtimes',return_value={}),\
         patch.object(package.urllib.request,'urlopen',side_effect=AssertionError('Paired fixture must remain offline')),\
         patch.object(sys,'argv',['package-release.py','--output',str(output)]):
        package.main()
    with zipfile.ZipFile(output/'Moader-backend-v0.0.0-fixture.zip') as archive:
        prefix='Moader-backend-v0.0.0-fixture/'
        require(archive.read(prefix+'vendor/kaihen-mo/hen/main')==hen and archive.read(prefix+'vendor/kaihen-mo/bootstrap.bin')==bootstrap,'Host ZIP omitted or changed paired binary')
        require(b'Moader-Setup-v0.0.0-fixture.zip' in archive.read(prefix+'START_HERE.txt'),
                'Command-line kit did not direct users to the separate Setup asset')
    with zipfile.ZipFile(output/'Moader-Setup-v0.0.0-fixture.zip') as archive:
        prefix='Moader-Setup-v0.0.0-fixture/'
        require(archive.read(prefix+'vendor/kaihen-mo/hen/main')==hen and
                b'Open Moader Setup' in archive.read(prefix+'START_HERE.txt'),
                'Standalone Setup archive omitted the matched backend or entry instructions')
    require(all(package.setup_only_member(name) for name in (
                'Moader Setup', 'Moader Setup.app/Contents/Info.plist',
                'Moader Setup Windows/Moader Setup.exe', 'setup/controller.cjs',
                'host-runtimes/node-v24.22.3/linux-x64/node')) and
            not package.setup_only_member('tools/setup-backend.py') and
            not package.setup_only_member('setup/licenses/uv-LICENSE-MIT.txt'),
            'Setup-only archive partition changed')
    recorded=json.loads((output/'release-manifest.json').read_text())
    require(recorded['patchedBootstrapSha256']==sha(bootstrap) and recorded['kaihenProtocolVersion']==7 and
            recorded['kaihenConsoleRetestPassed'] is False and recorded['appAndResidentHardwareTested'] is False and
            recorded['setupGuiBundled'] is False and recorded['setupGuiSelfContained'] is True and
            recorded['setupGuiAsset']=='Moader-Setup-v0.0.0-fixture.zip',
            'Release manifest overstates pair/hardware validation or misidentifies Setup')
    with patch.dict(globals(),{'ROOT':work/'missing candidate root'}),\
         patch.object(urllib.request,'urlopen',side_effect=AssertionError('Candidate mode must not download a baseline')):
        try:release_binaries(root,work);raise AssertionError('Missing candidate silently accepted')
        except AssertionError as error:require('Missing pinned local candidate bytes' in str(error),'Candidate mode attempted implicit fallback')
    # Synthetic archive checks use temporary fixture pins, never production
    # trust anchors or a live download. Every allowed native member is copied.
    baseline_files={name:('legacy fixture '+name).encode() for name in BINARY_SHA}
    baseline_files['dist/Moader.nro']=b'\0'*16+b'NRO0legacy-fixture'
    baseline_files['dist/moader.nso']=b'NSO0legacy-fixture'
    legacy_pins={name:sha(data) for name,data in baseline_files.items()}
    baseline_files.update({'software-builds/kaihen-mo.json':json.dumps({'hen_sha256':legacy_pins['vendor/kaihen-mo/hen/main']}).encode(),
                           'software-builds/kaihen-mo.patch':b'legacy fixture patch'})
    for name in ('hen/Makefile','hen/source/main.cpp','bootstrap/Makefile','bootstrap/source/main.cpp','libkaihen/include/kaihen.h','common/extra.hpp'):
        baseline_files['runtime/kaihen/'+name]=b'legacy native source'
    prefix='Moader-backend-v0.1.0-dev/'
    baseline_zip=zipped({prefix+name:data for name,data in baseline_files.items()})
    baseline_kit=work/'baseline isolated kit';(baseline_kit/'software-builds').mkdir(parents=True)
    (baseline_kit/'software-builds/kaihen-mo.json').write_text('{"schema":2}')
    (baseline_kit/'runtime/kaihen').mkdir(parents=True);(baseline_kit/'runtime/kaihen/new-only.hpp').write_text('not baseline')
    with patch.dict(BINARY_SHA,legacy_pins,clear=True),patch.dict(globals(),{'BACKEND_SHA':sha(baseline_zip)}),\
         patch.object(urllib.request,'urlopen',return_value=io.BytesIO(baseline_zip)):
        release_binaries(baseline_kit,work,baseline=True)
        require(not (baseline_kit/'runtime/kaihen/new-only.hpp').exists(),'Baseline retained candidate native source')
        require(all((baseline_kit/name).read_bytes()==data for name,data in baseline_files.items()),'Baseline closure/proof bytes differ')
        for unsafe in ('../escape','/absolute','runtime/kaihen/../escape','runtime\\bad'):
            malformed=zipped({**{prefix+name:data for name,data in baseline_files.items()},prefix+unsafe:b'bad'})
            try:baseline_members(malformed);raise AssertionError('Unsafe baseline path accepted')
            except AssertionError as error:require('Unsafe baseline' in str(error),'Unexpected malformed baseline failure')
        bad=io.BytesIO()
        with zipfile.ZipFile(bad,'w') as archive:
            member=zipfile.ZipInfo(prefix+'runtime/kaihen/link');member.create_system=3;member.external_attr=(stat.S_IFLNK|0o777)<<16
            archive.writestr(member,b'elsewhere')
        try:baseline_members(bad.getvalue());raise AssertionError('Baseline symlink accepted')
        except AssertionError as error:require('special/encrypted' in str(error),'Unexpected baseline symlink failure')
    print('PASS: explicit legacy baseline closure replacement, unsafe-member rejection, and missing-candidate refusal without fallback.')
    print('PASS: paired proof in Python/Node, offline setup, old-bootstrap rejection, isolated mocked pair build and matched release ZIP/source gates; no compiler/network/console/Git.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--online',action='store_true',help='Fetch pinned public release/dependencies and exercise real local preparation.')
    parser.add_argument('--node',default='node')
    parser.add_argument('--host-profile', choices=['smilebasic-kurumuz','civ7-s8n'], default='smilebasic-kurumuz')
    parser.add_argument('--pair-only',action='store_true',help='Only synthetic paired-build proof/setup/package fixtures; no downloads or compiler.')
    parser.add_argument('--baseline',action='store_true',help='With --online, test current host tools using the immutable public legacy binaries AND matching proof/native source; not current candidate validation.')
    args=parser.parse_args()
    node=shutil.which(args.node);require(node is not None,'Node executable is unavailable')
    if args.pair_only:
        require(not args.online and not args.baseline,'--pair-only cannot request online/baseline preparation')
        with tempfile.TemporaryDirectory(prefix='moader paired setup ') as temporary:paired_setup_checks(node,Path(temporary))
        return
    require(not args.baseline or args.online,'--baseline requires explicit --online')
    version=subprocess.check_output([node,'--version'],text=True).strip()
    require(re.fullmatch(r'v24\.\d+\.\d+',version) is not None,'Run this integration with Linux/macOS Node 24')
    with tempfile.TemporaryDirectory(prefix='moader ns2jb host integration ') as temporary:
        work=Path(temporary).resolve();kit=work/'Moader public kit with spaces';kit.mkdir()
        copy_source(kit)
        sentinel=work/'unexpected-node-io';guard=work/'guard preparation.cjs';guard.write_text(GUARD)
        python_guard=work/'python process guard';python_guard.mkdir()
        (python_guard/'sitecustomize.py').write_text(PYTHON_GUARD)
        process_audit=work/'preparation-processes.txt'
        env={**{key:value for key,value in os.environ.items() if not key.startswith(('NS2_', 'MOADER_HOST_'))},'PYTHONDONTWRITEBYTECODE':'1','MOADER_HOST_IO_SENTINEL':str(sentinel),
             'NODE_OPTIONS':'--require='+json.dumps(str(guard)),
             'PYTHONPATH':str(python_guard)+os.pathsep+os.environ.get('PYTHONPATH',''),
             'MOADER_HOST_NODE':node,'MOADER_HOST_PYTHON':sys.executable,'MOADER_HOST_KIT':str(kit),
             'MOADER_HOST_PROFILE':args.host_profile,
             'MOADER_HOST_PROCESS_AUDIT':str(process_audit),
             'PATH':str(Path(node).parent)+os.pathsep+os.environ.get('PATH','')}
        directory,result,current,proof=synthetic_run(work)
        gate_checks(node,kit,directory,(result,current,proof),env)
        if not args.online:
            print('PASS: host-only static gate checks; online preparation not requested.');return
        binary_sha=release_binaries(kit,work,baseline=args.baseline)
        original_run=tree(directory)
        profile_args=['--host-profile',args.host_profile] if args.host_profile!='smilebasic-kurumuz' else []
        setup=[sys.executable,str(kit/'tools/setup-backend.py'),'--root',str(kit),'--node',node,*profile_args]
        run(setup,kit,env)
        before=tree(kit)
        run([*setup,'--check'],kit,env)
        require(tree(kit)==before,'Read-only setup check changed local inputs')
        for name,expected in binary_sha.items():require(sha((kit/name).read_bytes())==expected,'Setup replaced a pinned binary')
        wrapper=[sys.executable,str(kit/'tools/install-from-rpc.py'),'--root',str(kit),*profile_args]
        relative_run=os.path.relpath(directory,kit)
        before=tree(kit);run([*wrapper,'--run-dir',relative_run,'--node','node'],kit,env)
        require(tree(kit)==before,'Direct-path parsing changed local setup')
        manifests=[]
        for requested_run,requested_node in [(relative_run,'node'),(str(directory),node)]:
            output=run([*wrapper,'--run-dir',requested_run,'--node',requested_node,'--prepare'],kit,env)
            commands=[shlex.split(line) for line in output.splitlines() if '--prepared' in line]
            require(len(commands)==1 and len(commands[0])==5,'Expected exactly one shell-safe prepared installation command')
            command=commands[0];manifest=Path(command[3])
            require(command[:3]==[node,str(kit/'tools/install-moader.cjs'),'--prepared'] and command[4]==str(directory),'Printed direct command changed Node/run/installer')
            require(manifest.is_file() and manifest.name=='manifest.json' and manifest.parent.name.startswith('prepared-') and manifest.parent.parent==kit/'build/install','Printed manifest is not a unique local snapshot')
            require(manifest.stat().st_mode & 0o222==0,'Prepared manifest is writable')
            if args.host_profile=='civ7-s8n':
                require(json.loads(manifest.read_text()).get('hostProfile',{}).get('id')=='civ7-s8n','Prepared snapshot lost its Civ7 host-profile binding')
            manifests.append(manifest)
        require(manifests[0]!=manifests[1],'Preparation reused a mutable snapshot')
        require(tree(directory)==original_run,'Host preparation changed the upstream run')
        require(not (kit/'state').exists(),'Preparation entered native installation')
        require(not sentinel.exists(),'Preparation attempted Node network/RPC/child execution')
        processes=process_audit.read_text().splitlines()
        require(set(processes)<= {'node-version','prebuilt-probe','installer-check','local-setup'} and
                processes.count('installer-check')==2 and processes.count('local-setup')==2,
                'Preparation process audit differs from the local-only command contract')
        for name,expected in binary_sha.items():require(sha((kit/name).read_bytes())==expected,'Preparation replaced a pinned binary')
        print('PASS: '+args.host_profile+' '+('current host-tool compatibility with the public legacy baseline' if args.baseline else 'recorded candidate host preparation')+', pinned dependency downloads, read-only check, paths with spaces and two unique --prepared commands; no console/RPC/target execution.')
    print('Scope: host preparation only; real Linux/WSL networking, ns2jb acquisition and native installation require separate device qualification.')


if __name__=='__main__':
    main()
