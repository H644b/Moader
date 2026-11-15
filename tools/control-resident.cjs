#!/usr/bin/env node
'use strict';
// Ordinary public-plugin control only. No acquisition, mounting or patches.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {readProtocol,decodeReply}=require('./inspect-hen.cjs');
const {guardTransports}=require('./ready-kaihen.cjs');
const {guardSession,isUncertain}=require('../runtime/modloader/lib/game-files/uncertain.cjs');
const ROOT=path.resolve(__dirname,'..'),check=(value,message)=>{if(!value)throw Error(message);};
const USAGE='Usage: node tools/control-resident.cjs CURRENT_RUN_DIRECTORY web on|off | CURRENT_RUN_DIRECTORY sessions N (1..32) | CURRENT_RUN_DIRECTORY error';
function options(args){
 check(typeof args[0]==='string'&&args[0]&&!args[0].startsWith('--'),USAGE);
 if(args.length===2&&args[1]==='error')return{run:path.resolve(args[0]),action:'error'};
 check(args.length===3,USAGE);
 if(args[1]==='web'){check(['on','off'].includes(args[2]),USAGE);return{run:path.resolve(args[0]),action:'web',enabled:args[2]==='on'};}
 check(args[1]==='sessions'&&/^(?:[1-9]|[12][0-9]|3[0-2])$/.test(args[2]),USAGE);
 return{run:path.resolve(args[0]),action:'sessions',count:Number(args[2])};
}
async function controlProtocol(){
 const protocol=await readProtocol(),header=await fs.readFile(path.join(ROOT,'include/moader/ipc.hpp'),'utf8');
 // These are implicit consecutive enum values, not guessed numeric IDs.
 check(/enum Command\s*:\s*uint32_t\s*\{\s*Summary\s*,\s*WebEnabled\s*,/.test(header),'Retained WebEnabled command ordering changed');
 const commands=header.match(/enum Command\s*:\s*uint32_t\s*\{([^}]+)\}/)?.[1].split(',').map(name=>name.trim());
 check(commands&&commands.every(name=>/^[A-Za-z_]\w*$/.test(name))&&commands.includes('LastError'),'Retained implicit LastError command enum changed');
 const control=await fs.readFile(path.join(ROOT,'native/control.hpp'),'utf8');
 check(/if \(id == moader::ipc::WebEnabled\)\s*\{\s*if \(input_size < sizeof\(u32\)\)/.test(control)&&
  /u32 enabled; std::memcpy\(&enabled, input, sizeof\(enabled\)\)/.test(control),'Retained WebEnabled u32 input ABI changed');
 check(/if \(id == moader::ipc::LastError\)\s*\{\s*copy_text\(static_cast<char\*>\(output\), 64, context.error\); \*output_size = 64; return 0;/.test(control),'Retained LastError text ABI changed');
 return{...protocol,webEnabled:protocol.summary+1,webInputBytes:4,lastError:commands.indexOf('LastError'),errorBytes:64};
}
function latch(state,error){error.nativeRequestUncertain=true;state.error ||= error;return state.error;}
function summary(data,p){
 check(data.readUInt32LE()===p.summaryVersion,'Unsupported Moader Summary version');
 const flags=data.readUInt32LE(4);check(!(flags&p.foregroundFlag),'Moader Summary identifies foreground mode; refusing resident control');
 const end=data.subarray(40,64).indexOf(0);check(end>=0,'Unterminated Moader Summary detail');
 const generation=data.readBigUInt64LE(8).toString(),appliedGeneration=data.readBigUInt64LE(16).toString();
 return{version:data.readUInt32LE(),flags,generation,appliedGeneration,modCount:data.readUInt32LE(24),port:data.readUInt16LE(28),
  ipv4:[...data.subarray(32,36)].join('.'),lastResult:data.readUInt32LE(36),
  detail:new TextDecoder('utf-8',{fatal:true}).decode(data.subarray(40,40+end)),resident:true,
  webRunning:!!(flags&p.webFlag),backendConnected:!!(flags&p.backendFlag),routingGenerationCurrent:!!(flags&p.backendFlag)&&generation===appliedGeneration};
}
async function control({Session,packet,argumentsWithPid,protocol:p,state={error:null},request:action,record,save}){
 const s=guardSession(new Session('moader-resident-control'));let error;
 async function stage(name){if(state.error||s.uncertainError)throw state.error||s.uncertainError;record.stage=name;await save();}
 async function request(label,handle,id,{input=Buffer.alloc(0),options={},bytes=0,copies=0}={}){
  await stage(label);await s.write(s.base,packet(id,input,options));
  const transport=await s.call('send',s.base,4096,handle);check(!transport,label+' transport failed: 0x'+transport.toString(16));
  let raw;try{raw=await s.readOwned(s.base,256);}catch(failure){throw latch(state,failure);}
  const reply=decodeReply(raw,h=>{if(s.handles.has(h))throw latch(state,Error('Returned handle was already owned; ownership is unknown'));s.handles.add(h);},bytes);
  check(!reply.moves.length&&reply.copies.length===(reply.result?0:copies),label+' returned unexpected handles');return reply;
 }
 function success(reply,label){check(!reply.result,label+' failed: 0x'+reply.result.toString(16));return reply;}
 async function openClient(hen,client){
  const input=Buffer.alloc(p.nameBytes);input.write('moader');
  const reply=await request('Open published Moader client '+client.index,hen,p.open,{input,copies:1});client.openResult=reply.result;await save();
  const handle=success(reply,'Open Moader client').copies[0];client.opened=true;await save();return handle;
 }
 async function readSummary(handle,label){return summary(success(await request(label,handle,p.summary,{bytes:64}),label).data,p);}
 async function closeClient(handle,client){
  await stage('Close owned Moader client '+client.index);client.closeAttempted=true;await save();
  try{await s.close(handle);check(!s.handles.has(handle),'Closed Moader handle remains owned');}
  catch(failure){throw latch(state,failure);} // Do not retry a failed close in outer cleanup.
  client.closeComplete=true;await save();
 }
 try{
  await stage('initialize resident-control session');await s.initialize();
  const ro=await s.openService('ldr:ro');
  success(await request('Initialize current process on RO',ro,p.initialize,{input:argumentsWithPid(),options:{pid:true,copies:[0xffff8001]}}),'RO Initialize');
  const hen=success(await request('Open HEN homebrew session',ro,p.homebrew,{input:Buffer.alloc(8),options:{pid:true},copies:1}),'Open HEN').copies[0];
  const status=success(await request('Read current HEN status',hen,p.status,{bytes:16}),'HEN status').data;
  record.hen={pid:status.readBigUInt64LE().toString(),protocol:status.readUInt32LE(8),result:status.readUInt32LE(12)};await save();
  check(record.hen.protocol===p.henVersion&&record.hen.result===0&&BigInt(record.hen.pid)>0n,'Current HEN startup/protocol is not qualified');
  if(action.action==='web'){
   const client={index:1,closeComplete:false};record.clients.push(client);const handle=await openClient(hen,client);
   record.before=await readSummary(handle,'Read resident Summary before web control');await save();
   const input=Buffer.alloc(p.webInputBytes);input.writeUInt32LE(action.enabled?1:0);
   record.commandAttempted=true;record.commandPending=true;await save();
   const reply=await request('Set resident web '+(action.enabled?'on':'off'),handle,p.webEnabled,{input});
   record.commandResult=reply.result;record.commandPending=false;await save();
   // A definite completed rejection is also followed by one independent
   // Summary, never a retry, so its observed state is retained accurately.
   record.after=await readSummary(handle,'Read resident Summary after web control');
   record.flagMatchesRequested=record.after.webRunning===action.enabled;await save();
   check(!reply.result,'WebEnabled failed: 0x'+reply.result.toString(16));
   check(record.flagMatchesRequested,'Completed web command did not produce the requested Summary flag');
   await closeClient(handle,client);record.clientsCompleted=1;
  }else if(action.action==='error'){
   const client={index:1,closeComplete:false};record.clients.push(client);const handle=await openClient(hen,client);
   record.before=await readSummary(handle,'Read resident Summary before LastError');await save();
   const reply=await request('Read resident LastError',handle,p.lastError,{bytes:p.errorBytes});record.lastErrorResult=reply.result;await save();
   const data=success(reply,'LastError').data,end=data.indexOf(0);check(end>=0,'Unterminated resident LastError text');
   record.errorText=new TextDecoder('utf-8',{fatal:true}).decode(data.subarray(0,end));await save();
   await closeClient(handle,client);record.clientsCompleted=1;
  }else{
   for(let i=0;i<action.count;i++){
    const client={index:i+1,closeComplete:false};record.clients.push(client);const handle=await openClient(hen,client);
    client.summary=await readSummary(handle,'Read resident Summary for client '+client.index);
    if(i===0)record.before=client.summary;record.after=client.summary;await save();
    await closeClient(handle,client);record.clientsCompleted=i+1;await save();
   }
  }
 }catch(failure){error=failure;record.failedStage=record.stage;}
 if(error&&(isUncertain(error)||state.error||s.uncertainError)){latch(state,error);s.uncertainError=state.error;}
 if(state.error||s.uncertainError){record.nativeOperationUncertain=true;record.resourcesRetained=true;}
 else try{
  await stage('close owned outer RO and HEN sessions');await s.cleanup();
  check(!s.base&&!s.handles.size,'Resident control cleanup was deferred');record.cleanupComplete=true;
 }catch(failure){latch(state,failure);if(error)record.cleanupError=String(failure.stack||failure);else error=failure;record.failedStage??=record.stage;record.nativeOperationUncertain=true;record.resourcesRetained=true;}
 if(record.resourcesRetained)record.retainedResources={allocation:s.base,handles:[...s.handles]};
 record.complete=!error&&record.cleanupComplete===true;record.inspectBeforeRetry=!record.complete;record.finished=new Date().toISOString();
 if(error)record.error=String(error.stack||error);else record.stage='resident control complete';
 try{await save();}catch(storageError){if(error){error.evidenceWriteError=String(storageError.stack||storageError);error.control=record;throw error;}storageError.control=record;throw storageError;}
 if(error){error.control=record;throw error;}return record;
}
function runtimeDependencies(run,state){
 process.env.NS2_RUN_DIR=run;
 const engine=path.join(ROOT,'research/sources/ns2jb/engine'),load=name=>require(path.join(engine,name));
 const rpc=load('shared/rpc.cjs'),endpoint=load('switch2/tools/endpoint.cjs');guardTransports({rpc,endpoint},state);
 check(rpc.a?.session===path.basename(run),'RPC configuration belongs to another run; use a fresh process for this run');
 const {Session}=load('switch2/app/session.cjs'),{packet}=load('shared/ipc.cjs'),{argumentsWithPid}=load('switch2/ro/nro.cjs');
 return{Session,packet,argumentsWithPid};
}
async function main(args=process.argv.slice(2)){
 const action=options(args),protocol=await controlProtocol(),state={error:null};
 const record={schema:1,run:action.run,action:action.action,...(action.action==='web'?{enabled:action.enabled}:action.action==='sessions'?{count:action.count}:{}),
  started:new Date().toISOString(),stage:'local protocol/argument gate passed',complete:false,cleanupComplete:false,
  nativeOperationUncertain:false,resourcesRetained:false,clients:[],clientsCompleted:0,commandAttempted:false,commandPending:false};
 const file=path.join(ROOT,'evidence/kaihen/resident-control-'+Date.now()+'-'+crypto.randomUUID()+'.json');
 await fs.mkdir(path.dirname(file),{recursive:true});await fs.writeFile(file,JSON.stringify(record,null,2)+'\n',{flag:'wx',mode:0o600});
 const save=()=>fs.writeFile(file,JSON.stringify(record,null,2)+'\n',{mode:0o600});
 const previousRun=process.env.NS2_RUN_DIR;
 try{
  const dependencies=runtimeDependencies(action.run,state);
  const result=await control({...dependencies,protocol,state,request:action,record,save});
  console.log(JSON.stringify({...result,file},null,2));return{...result,file};
 }catch(error){
  if(!error.control){record.error=String(error.stack||error);record.nativeOperationUncertain=!!state.error;record.resourcesRetained=!!state.error;record.inspectBeforeRetry=true;
   try{await save();}catch(storageError){error.evidenceWriteError=String(storageError.stack||storageError);}}
  error.controlFile=file;throw error;
 }finally{if(previousRun===undefined)delete process.env.NS2_RUN_DIR;else process.env.NS2_RUN_DIR=previousRun;}
}
module.exports={options,controlProtocol,control,runtimeDependencies,main};
if(require.main===module)main().catch(error=>{console.error(error.stack);if(error.controlFile)console.error('Stopped; inspect '+error.controlFile+' before further native actions. No automatic retry.');if(error.evidenceWriteError)console.error(error.evidenceWriteError);process.exitCode=1;});
