#!/usr/bin/env python3
"""Host-only selected-profile preparation test; --online fetches pinned public data.

Never run upstream run.cjs/rpc.cjs, execute a target binary, or execute the
printed installation command. Synthetic run files are not live device proof.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

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
    for directory in ['include','runtime/modloader/lib','runtime/modloader/native']:
        shutil.copytree(ROOT/directory,kit/directory,ignore=ignore)
    for name in [*(f'tools/{name}' for name in TOOLS),*(p.relative_to(ROOT).as_posix() for p in (ROOT / 'tools').glob('*.cjs')),
                 *(p.relative_to(ROOT).as_posix() for p in (ROOT / 'config').glob('*.json')),'runtime/modloader/install.cjs',
                 'software-builds/kaihen-mo.json','software-builds/kaihen-mo.patch']:
        destination=kit/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,destination)


def release_binaries(kit, work):
    request=urllib.request.Request(BACKEND_URL,headers={'User-Agent':'Moader-host-integration'})
    with urllib.request.urlopen(request,timeout=90) as response:
        data=response.read((8<<20)+1)
    require(len(data)<=8<<20 and sha(data)==BACKEND_SHA,'Pinned public backend kit checksum/size mismatch')
    archive=work/'public baseline backend.zip';archive.write_bytes(data)
    with zipfile.ZipFile(archive) as package:
        for relative,expected in BINARY_SHA.items():
            member='Moader-backend-v0.1.0-dev/'+relative
            require(package.namelist().count(member)==1,'Missing/duplicate baseline binary')
            info=package.getinfo(member);require(info.file_size<=32<<20,'Baseline binary too large')
            payload=package.read(info);require(sha(payload)==expected,'Baseline binary checksum mismatch')
            destination=kit/relative;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(payload)
    require((kit/'dist/Moader.nro').read_bytes()[16:20]==b'NRO0','Missing NRO header')
    require((kit/'dist/moader.nso').read_bytes()[:4]==b'NSO0','Missing resident NSO header')
    print('PASS: actual public baseline kit and all three checked binaries match pinned release hashes; binaries treated only as data.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--online',action='store_true',help='Fetch pinned public release/dependencies and exercise real local preparation.')
    parser.add_argument('--node',default='node')
    parser.add_argument('--host-profile', choices=['smilebasic-kurumuz','civ7-s8n'], default='smilebasic-kurumuz')
    args=parser.parse_args()
    node=shutil.which(args.node);require(node is not None,'Node executable is unavailable')
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
        release_binaries(kit,work)
        original_run=tree(directory)
        profile_args=['--host-profile',args.host_profile] if args.host_profile!='smilebasic-kurumuz' else []
        setup=[sys.executable,str(kit/'tools/setup-backend.py'),'--root',str(kit),'--node',node,*profile_args]
        run(setup,kit,env)
        before=tree(kit)
        run([*setup,'--check'],kit,env)
        require(tree(kit)==before,'Read-only setup check changed local inputs')
        for name,expected in BINARY_SHA.items():require(sha((kit/name).read_bytes())==expected,'Setup replaced a tested binary')
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
        for name,expected in BINARY_SHA.items():require(sha((kit/name).read_bytes())==expected,'Preparation replaced a tested binary')
        print('PASS: '+args.host_profile+' real pinned public downloads, local kit setup, read-only check, shell paths with spaces and two unique direct --prepared commands; no console/RPC/target execution.')
    print('Scope: host preparation only; real Linux/WSL networking, ns2jb acquisition and native installation require separate device qualification.')


if __name__=='__main__':
    main()
