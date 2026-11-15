#!/usr/bin/env node
'use strict';
// Read-only, current-application inspection. Never launches or acquires anything.
const path=require('node:path'),fs=require('node:fs/promises'),crypto=require('node:crypto');
const {TextDecoder}=require('node:util');
const {guardTransports}=require('./ready-kaihen.cjs');
const {guardSession,isUncertain}=require('../runtime/modloader/lib/game-files/uncertain.cjs');
const root=path.resolve(__dirname,'..'),utf8=new TextDecoder('utf-8',{fatal:true});
const check=(condition,message)=>{if(!condition)throw Error(message);};

// Derive command IDs from retained owning sources before loading a live module.
async function readProtocol(){
 const [hen,control,ro,moader,game,trace]=await Promise.all([
  'runtime/kaihen/libkaihen/include/kaihen/protocol.h','runtime/kaihen/common/control.h',
  'runtime/kaihen/ro/source/wire.h','include/moader/ipc.hpp',
  'runtime/kaihen/common/game_arm.h','runtime/kaihen/hen/source/game_trace.hpp',
 ].map(name=>fs.readFile(path.join(root,name),'utf8')));
 function number(source,name){
  const value=source.match(new RegExp('\\b'+name+'\\s*(?:=\\s*|[ \\t]+)(0x[0-9a-f]+|[0-9]+)\\b','i'))?.[1];
  check(value!==undefined,'Missing retained numeric protocol definition: '+name);return Number(value);
 }
 check(/enum Command\s*:\s*uint32_t\s*\{\s*Summary\s*,/.test(moader),'Moader Summary command layout changed');
 check(/static_assert\(sizeof\(SummaryReply\)\s*==\s*64\)/.test(moader),'Moader Summary ABI changed');
 check(/sizeof\(HenStatus\)\s*==\s*16/.test(hen)&&/sizeof\(HenGameStatus\)\s*==\s*24/.test(game)&&
  /sizeof\(GameTraceSnapshot\)\s*==\s*24/.test(trace),'Retained HEN status ABI changed');
 const protocol={
  initialize:number(ro,'RoCmd_Initialize'),homebrew:number(hen,'KaihenRoCommand_GetHomebrewSession'),
  status:number(hen,'HenHomebrewCommand_GetStatus'),game:number(control,'HenControlCommand_GetGameStatus'),
  trace:number(control,'HenControlCommand_MoaderGameTrace'),count:number(hen,'HenHomebrewCommand_GetPluginCount'),
  name:number(hen,'HenHomebrewCommand_GetPluginName'),open:number(hen,'HenHomebrewCommand_OpenPlugin'),
  henVersion:number(hen,'HEN_PROTOCOL_VERSION'),maxPlugins:number(hen,'HEN_MAX_PLUGINS'),nameBytes:number(hen,'HEN_PLUGIN_NAME_SIZE'),
  summary:0,summaryVersion:number(moader,'Version'),webFlag:number(moader,'WebRunning'),backendFlag:number(moader,'BackendConnected'),foregroundFlag:number(moader,'Foreground'),
 };
 check(protocol.maxPlugins<=16&&protocol.maxPlugins>0&&protocol.nameBytes===64,'Unsupported retained plugin bounds');
 return protocol;
}
function malformed(message){return Object.assign(Error('Invalid inspection reply: '+message),{nativeRequestUncertain:true});}

// The common Session.ipc parser exposes bytes to the end of its allocation.
// Bound scalar replies by HIPC num_data_words instead; retained cmif_server.hpp
// and ro/source/wire.cpp reserve 16 alignment bytes plus the CMIF header.
function decodeReply(raw,ownHandle,expectedBytes){
 if(!Buffer.isBuffer(raw)||raw.length<8||raw.length>0x100)throw malformed('truncated HIPC header');
 const first=raw.readUInt32LE(),second=raw.readUInt32LE(4),words=second&0x3ff;
 let at=8;const copies=[],moves=[];
 if(second>>>31){
  if(at+4>raw.length)throw malformed('truncated special header');
  const special=raw.readUInt32LE(at);at+=4;
  if(special&1)at+=8;
  const copyCount=special>>>1&15,moveCount=special>>>5&15;
  if(at+(copyCount+moveCount)*4>raw.length)throw malformed('truncated handles');
  for(const [count,out]of [[copyCount,copies],[moveCount,moves]])for(let i=0;i<count;i++,at+=4){
   const handle=raw.readUInt32LE(at);
   if(!handle||handle>=0xffff8000)throw malformed('invalid returned handle');
   ownHandle(handle);out.push(handle);
  }
 }
 if(first!==0||(second>>>10&15)!==0)throw malformed('unexpected type or buffer descriptors');
 const end=at+words*4,header=(at+15)&~15,payload=header+16;
 if(end>raw.length||payload>end||raw.toString('ascii',header,header+4)!=='SFCO'||raw.readUInt32LE(header+4)>1)
  throw malformed('truncated or invalid CMIF frame');
 const result=raw.readUInt32LE(header+8),size=result?0:expectedBytes;
 if(words<Math.ceil((32+size)/4)||payload+size>end)throw malformed('truncated scalar data');
 return{result,copies,moves,data:raw.subarray(payload,payload+size)};
}
function text(bytes,label){
 const end=bytes.indexOf(0);check(end>=0,'Unterminated '+label);
 return utf8.decode(bytes.subarray(0,end));
}
function summary(data,p){
 const version=data.readUInt32LE(),flags=data.readUInt32LE(4);
 check(version===p.summaryVersion,'Unsupported Moader Summary version');
 const generation=data.readBigUInt64LE(8).toString(),appliedGeneration=data.readBigUInt64LE(16).toString();
 return{version,flags,generation,appliedGeneration,modCount:data.readUInt32LE(24),port:data.readUInt16LE(28),
  ipv4:[...data.subarray(32,36)].join('.'),lastResult:data.readUInt32LE(36),detail:text(data.subarray(40,64),'Moader Summary detail'),
  webRunning:!!(flags&p.webFlag),backendConnected:!!(flags&p.backendFlag),foreground:!!(flags&p.foregroundFlag),
  routingGenerationCurrent:!!(flags&p.backendFlag)&&generation===appliedGeneration};
}
async function inspect({Session,packet,argumentsWithPid,protocol:p,transportGuard={error:null},run}){
 const result={at:new Date().toISOString(),run,complete:false,controllerHandlesClosed:false,resourcesRetained:false,
  plugins:{count:0,names:[],enumerationComplete:false},moader:{listed:false,opened:false,serviceReachable:false,resident:false}};
 const s=guardSession(new Session('moader-hen-inspect'));let error;
 async function request(label,handle,id,{input=Buffer.alloc(0),options={},bytes=0,copies=0}={}){
  result.stage=label;
  await s.write(s.base,packet(id,input,options));
  const transport=await s.call('send',s.base,4096,handle);
  check(!transport,label+' transport failed: 0x'+transport.toString(16));
  let raw;
  try{raw=await s.readOwned(s.base,0x100);}catch(failure){
   // Send returned, but its copied handles/reply have not been qualified.
   // Preserve this allocation for inspection even for a definite read error.
   failure.nativeRequestUncertain=true;throw failure;
  }
  const reply=decodeReply(raw,h=>s.handles.add(h),bytes);
  check(!reply.moves.length&&reply.copies.length===(reply.result?0:copies),label+' returned unexpected handles');
  return reply;
 }
 function success(reply,label){check(!reply.result,label+' failed: 0x'+reply.result.toString(16));return reply;}
 try{
  result.stage='initialize inspection session';await s.initialize();
  const ro=await s.openService('ldr:ro');
  success(await request('Initialize current process on RO',ro,p.initialize,{input:argumentsWithPid(),options:{pid:true,copies:[0xffff8001]}}),'RO Initialize');
  const hen=success(await request('Open HEN session',ro,p.homebrew,{input:Buffer.alloc(8),options:{pid:true},copies:1}),'Open HEN').copies[0];
  const status=success(await request('Read HEN startup',hen,p.status,{bytes:16}),'HEN status').data;
  result.hen={pid:status.readBigUInt64LE().toString(),protocol:status.readUInt32LE(8),result:status.readUInt32LE(12)};
  check(result.hen.protocol===p.henVersion,'Running HEN protocol differs from retained source');
  const game=success(await request('Read HEN game state',hen,p.game,{bytes:24}),'HEN game state').data;
  result.game={phase:game.readUInt32LE(),result:game.readUInt32LE(4),serial:game.readUInt32LE(8),mounts:game.readUInt32LE(12),pid:game.readBigUInt64LE(16).toString()};
  const trace=await request('Read Moader game trace',hen,p.trace,{bytes:24});result.traceResult=trace.result;
  if(!trace.result)result.trace=Object.fromEntries(['sequence','point','result','value','requests','steps'].map((name,i)=>[name,trace.data.readUInt32LE(i*4)]));
  const count=success(await request('Read published plugin count',hen,p.count,{bytes:4}),'Plugin count').data.readUInt32LE();
  check(count<=p.maxPlugins,'Published plugin count exceeds retained bound');result.plugins.count=count;
  for(let i=0;i<count;i++){
   const input=Buffer.alloc(4);input.writeUInt32LE(i);
   const name=text(success(await request('Read published plugin '+i,hen,p.name,{input,bytes:p.nameBytes}),'Plugin name').data,'plugin name');
   check(name.length>0&&!result.plugins.names.includes(name),'Empty or duplicate published plugin name');result.plugins.names.push(name);
  }
  result.plugins.enumerationComplete=true;result.moader.listed=result.plugins.names.includes('moader');
  if(result.moader.listed){
   const input=Buffer.alloc(p.nameBytes);input.write('moader');
   const opened=await request('Open published Moader service',hen,p.open,{input,copies:1});result.moader.openResult=opened.result;
   if(!opened.result){
    result.moader.opened=true;
    const reply=await request('Read independent Moader Summary',opened.copies[0],p.summary,{bytes:64});result.moader.summaryResult=reply.result;
    if(!reply.result){result.moader.summary=summary(reply.data,p);result.moader.serviceReachable=true;result.moader.resident=!result.moader.summary.foreground;}
   }
  }
 }catch(failure){error=failure;result.failedStage=result.stage;}
 if(error&&(isUncertain(error)||s.uncertainError||transportGuard.error)){
  error.nativeRequestUncertain=true;s.uncertainError=error;transportGuard.error=error;
 }
 if(s.uncertainError||transportGuard.error){result.resourcesRetained=true;result.nativeOperationUncertain=true;}
 else try{
  result.stage='close owned inspection handles';await s.cleanup();
  check(!s.base&&!s.handles.size,'Inspection cleanup was deferred; inspect owned resources');result.controllerHandlesClosed=true;
 }catch(failure){if(error)result.cleanupError=String(failure.stack||failure);else error=failure;result.failedStage??=result.stage;result.resourcesRetained=true;result.nativeOperationUncertain=!!(s.uncertainError||transportGuard.error||isUncertain(failure));}
 if(result.resourcesRetained)result.retainedResources={allocation:s.base,handles:[...s.handles]};
 result.complete=!error;
 if(error){result.error=String(error.stack||error);result.inspectBeforeRetry=true;error.inspection=result;throw error;}
 result.stage='inspection complete';return result;
}
async function main(args=process.argv.slice(2)){
 check(args.length===1&&args[0]&&!args[0].startsWith('--'),'Usage: node tools/inspect-hen.cjs CURRENT_RUN_DIRECTORY');
 const protocol=await readProtocol(),run=path.resolve(args[0]);process.env.NS2_RUN_DIR=run;
 const engine=path.join(root,'research/sources/ns2jb/engine'),load=name=>require(path.join(engine,name));
 const transportGuard=guardTransports({rpc:load('shared/rpc.cjs'),endpoint:load('switch2/tools/endpoint.cjs')});
 const {Session}=load('switch2/app/session.cjs'),{packet}=load('shared/ipc.cjs'),{argumentsWithPid}=load('switch2/ro/nro.cjs');
 let result,error;
 try{result=await inspect({Session,packet,argumentsWithPid,protocol,transportGuard,run});}
 catch(failure){error=failure;result=failure.inspection??{at:new Date().toISOString(),run,complete:false,stage:'construct inspection session',error:String(failure.stack||failure)};}
 const target=path.join(root,'evidence/kaihen/status-'+Date.now()+'-'+crypto.randomUUID()+'.json');
 try{
  await fs.mkdir(path.dirname(target),{recursive:true});
  await fs.writeFile(target,JSON.stringify(result,null,2)+'\n',{mode:0o600,flag:'wx'});
 }catch(storageError){if(error){error.evidenceWriteError=String(storageError.stack||storageError);throw error;}throw storageError;}
 if(error){error.inspectionFile=target;throw error;}
 console.log(JSON.stringify({...result,file:target},null,2));return{...result,file:target};
}
module.exports={readProtocol,decodeReply,inspect,main};
if(require.main===module)main().catch(error=>{console.error(error.stack);if(error.inspectionFile)console.error('Inspection stopped; inspect '+error.inspectionFile+' before further native work.');if(error.evidenceWriteError)console.error('Could not retain local inspection evidence: '+error.evidenceWriteError);process.exitCode=1;});
