#!/usr/bin/env node
'use strict';
// One fresh FS-capable run, one guarded native installation. Never acquire or retry.
const fs=require('node:fs/promises'),path=require('node:path');
const project=path.resolve(__dirname,'..'),check=(value,text)=>{if(!value)throw Error(text);};
function guardRuntimeTransports(run){
 process.env.NS2_RUN_DIR=path.resolve(run);
 const {guardTransports}=require('./ready-kaihen.cjs'),state={error:null};
 // Runtime clients load the selected run's sources; SD staging uses the
 // retained engine. Both must stop after either transport becomes uncertain.
 for(const engine of new Set([path.resolve(run),path.join(project,'research/sources/ns2jb/engine')]))
  guardTransports({rpc:require(path.join(engine,'shared/rpc.cjs')),endpoint:require(path.join(engine,'switch2/tools/endpoint.cjs'))},state);
 return state;
}
async function install(run,directory,prepared){
 run=path.resolve(run);directory=path.resolve(directory);
 require('../runtime/modloader/install.cjs').verifiedRun(run);
 await fs.mkdir(directory,{recursive:true});
 const journal=path.join(directory,'moader-runtime.json');
 const record={session:path.basename(run),started:new Date().toISOString(),stage:'preflight',complete:false};
 await fs.writeFile(journal,JSON.stringify(record,null,2)+'\n',{flag:'wx',mode:0o600});
 const save=()=>fs.writeFile(journal,JSON.stringify(record,null,2)+'\n',{mode:0o600});
 const phase=async(name,action)=>{record.stage=name;await save();console.log(name);const value=await action();record[name]=value;await save();return value;};
 let transportGuard;
 try{
  const inputs=require('./install-inputs.cjs');
  await inputs.validateMoaderPrepared(prepared);
  transportGuard=guardRuntimeTransports(run);
  const {captureNative}=require('../runtime/modloader/lib/native-capture.cjs');
  const capture=await phase('qualify fresh FS code and mappings',()=>captureNative(run,directory));
  const profile=JSON.parse(await fs.readFile(path.join(directory,'native-fs/manifest.json')));
  check(profile.verified===true&&profile.session===path.basename(run)&&profile.runtimeLoggingCompatible===true,
   'Moader requires the same-session verified fresh native capture and qualified write/flush ABI');
  // captureNative validates function bytes and the unused gateway preimage.
  // An already installed runtime fails that qualification before SD staging;
  // never provision over a live writer or skip this gate to reset its logs.
  record.freshFilesystemQualified=true;
  const plan=require('../runtime/modloader/lib/lazy-context.cjs').plan({games:[],patches:[]},path.basename(run));
  plan.moader=true;plan.nativeProfile=profile;plan.runtimeDiagnostics=true;plan.runtimeTrace=false;
  const folder=path.join(directory,'prepared');await fs.mkdir(folder,{recursive:true});
  await fs.writeFile(path.join(folder,plan.payload.filename),Buffer.alloc(0));
  record.metadata=await phase('stage and verify runtime metadata',()=>require('./sd-install.cjs').stage(run,prepared.metadata));
  inputs.validateMetadataReadback(prepared.metadata,record.metadata);
  const diagnostics=require('../runtime/modloader/lib/diagnostics.cjs');
  const report=diagnostics.report({session:path.basename(run),sdPath:'/moader/runtime/runtime.log',collection:{games:[],patches:[]},plan,status:'prepared',trace:false});
  record.report=await phase('create Moader runtime report',()=>diagnostics.writeReport(run,report));
  plan.diagnostics={...report,buffer:undefined,text:report.buffer.toString('utf8'),size:report.buffer.length};
  await fs.writeFile(path.join(folder,'overlay.json'),JSON.stringify(plan,null,2)+'\n');
  const {buildAll}=require('../runtime/modloader/lib/native.cjs'),{buildContext}=require('../runtime/modloader/lib/context.cjs');
  await phase('check relocation and native context layout',async()=>{
   const native=await buildAll(0x4000000000n,0x6000000000n,plan,{capture,directory:path.join(directory,'offline-build')});
   const context=buildContext(plan,{filesystem:{object:0x6100001000n},registry:0x6100003000n,
    logFile:{object:0x6100002000n,write:0x40001050a0n,flush:0x40001053d0n,setSize:0x4000105410n}},0x4000000000n,0x6000000000n,native);
   return{contextBytes:context.length,runtimeBytes:native.runtimeBytes,gatewayBytes:native.codeBytes};
  });
  const resident=await phase('attach resident filesystem routing',()=>require('../runtime/modloader/lib/resident.cjs').install(run,plan,directory,capture,null,name=>console.log('  '+name)));
  check(resident.verified&&resident.active&&resident.controllerHandlesClosed&&resident.diagnosticsOwned===true&&resident.diagnosticWriteErrors==='0',
   'Native runtime did not reach verified idle handoff with required Moader diagnostics');
  record.complete=true;record.finished=new Date().toISOString();await save();return record;
 }catch(error){record.error=String(error.stack||error);record.nativeOperationUncertain=!!transportGuard?.error;record.inspectBeforeRetry=true;await save();throw error;}
}
module.exports={install,guardRuntimeTransports};
