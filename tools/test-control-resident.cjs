#!/usr/bin/env node
'use strict';
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs'),Module=require('node:module');
const net=require('node:net'),http=require('node:http');let connections=0;
net.createConnection=http.request=()=>{connections++;throw Error('Offline resident-control tests cannot connect');};
const previousEnv=process.env.NS2_RUN_DIR,api=require('./control-resident.cjs');
assert.equal(process.env.NS2_RUN_DIR,previousEnv);assert(!Object.keys(require.cache).some(name=>name.endsWith('/engine/shared/rpc.cjs')));
const {packet:realPacket}=require('../research/sources/ns2jb/engine/shared/ipc.cjs'),{guardTransports}=require('./ready-kaihen.cjs');
const ROOT=path.resolve(__dirname,'..'),RUN='/current/switch2-ControlOffline',tests=[];
function test(name,action){tests.push({name,action});}
function frame(data=Buffer.alloc(0),result=0,copies=[],moves=[]){
 const raw=Buffer.alloc(256);raw.writeUInt32LE((Math.ceil((32+data.length)/4)|((copies.length||moves.length)?0x80000000:0))>>>0,4);let at=8;
 if(copies.length||moves.length){raw.writeUInt32LE(copies.length<<1|moves.length<<5,at);at+=4;for(const h of [...copies,...moves]){raw.writeUInt32LE(h,at);at+=4;}}
 const header=(at+15)&~15;raw.write('SFCO',header);raw.writeUInt32LE(result,header+8);data.copy(raw,header+16);return raw;
}
function parse(frame){let at=8;if(frame.readUInt32LE(4)>>>31){const special=frame.readUInt32LE(at);at+=4+(special&1?8:0)+((special>>>1&15)+(special>>>5&15))*4;}
 const header=(at+15)&~15;assert.equal(frame.toString('ascii',header,header+4),'SFCI');return{id:frame.readUInt32LE(header+8)};}
function record(action){return{run:RUN,action:action.action,complete:false,cleanupComplete:false,nativeOperationUncertain:false,resourcesRetained:false,
 clients:[],clientsCompleted:0,commandAttempted:false,commandPending:false};}
function fixture(p,action={action:'web',enabled:true},options={}){
 const events=[],closed=[],sends=[],state={error:null},r=record(action);let outbound,reply,lastKey,input,web=options.initialWeb??false,summaryReads=0,activeClient=false;
 const fault=key=>{if(options.failAt===key)throw options.failure??Error('Native request timed out; it was not replayed');};
 const rpc={a:{session:path.basename(RUN)},async call(key){events.push(key);fault(key);return 0;},async write(){events.push('write');fault('write');},async read(){events.push('read');fault('read');}};
 const endpoint={async request(){events.push('retained-request');}};guardTransports({rpc,endpoint},state);
 function packet(id,bytes,options){input=Buffer.from(bytes??[]);return realPacket(id,bytes,options);}
 function dispatch(handle){
  const {id}=parse(outbound);let data=Buffer.alloc(0),result=0,copies=[];
  if(handle===10&&id===p.initialize){lastKey='initialize-ro';assert.equal(input.length,8);}
  else if(handle===10&&id===p.homebrew){lastKey='open-hen';copies=[20];}
  else if(handle===20&&id===p.status){lastKey='status';data=Buffer.alloc(16);data.writeBigUInt64LE(55n);data.writeUInt32LE(options.henVersion??p.henVersion,8);data.writeUInt32LE(options.henStartupResult??0,12);}
  else if(handle===20&&id===p.open){lastKey='open-client';assert.equal(input.length,p.nameBytes);assert.equal(input.toString().replace(/\0+$/,''),'moader');
   assert(!activeClient,'Previous plugin client must close before opening another');if(options.openError)result=0x202;else{copies=[30];activeClient=true;}}
  else if(handle===30&&id===p.summary){
   summaryReads++;lastKey='summary:'+summaryReads;data=Buffer.alloc(64);data.writeUInt32LE(options.summaryVersion??p.summaryVersion);
   data.writeUInt32LE((web?p.webFlag:0)|p.backendFlag|(options.foreground?p.foregroundFlag:0),4);data.writeBigUInt64LE(14n,8);data.writeBigUInt64LE(14n,16);
   data.writeUInt16LE(7712,28);Buffer.from([192,0,2,20]).copy(data,32);data.write('Routing connected',40);
   if(options.summaryError){result=0x202;data=Buffer.alloc(0);}
  }else if(handle===30&&id===p.webEnabled){lastKey='web';assert.equal(input.length,4);assert([0,1].includes(input.readUInt32LE()));
   if(options.commandError)result=0x202;else if(!options.flagMismatch)web=!!input.readUInt32LE();
  }else if(handle===30&&id===p.lastError){
   lastKey='last-error';assert.equal(input.length,0);data=Buffer.alloc(64);data.write(options.errorText??'Listener could not start');
   if(options.unterminatedError)data.fill(65);if(options.invalidErrorUtf8){data[0]=0xc3;data[1]=0x28;data[2]=0;}
   if(options.lastErrorFailure){result=0x202;data=Buffer.alloc(0);}
  }else throw Error('Unexpected or unauthorized public control command '+handle+':'+id);
  sends.push({key:lastKey,id,handle,input:Buffer.from(input)});
  if(options.extraHandleAt===lastKey)copies.push(options.duplicateHandle?20:40);
  reply=frame(data,result,copies);
  if(options.truncateAt===lastKey)reply.writeUInt32LE((reply.readUInt32LE(4)&0xfffffc00)|8,4);
  if(options.shortAt===lastKey)reply=reply.subarray(0,10);
 }
 class Session{
  constructor(){this.base=0;this.handles=new Set();}
  async initialize(){try{await this.call('allocate');this.base=0x10000;}finally{await this.write(0,Buffer.alloc(8));}}
  async openService(name){assert.equal(name,'ldr:ro');await rpc.call('open-ro');this.handles.add(10);return 10;}
  async write(at,bytes){await rpc.write();if(bytes.length===256)outbound=bytes;}
  async call(name,base,size,handle){if(name==='send'){dispatch(handle);return rpc.call('send:'+lastKey);}return rpc.call(name);}
  async readOwned(){await rpc.read();return reply;}
  async close(handle){assert(this.handles.has(handle));await this.call('close:'+handle);if(options.closeError&&handle===30)throw Error('Known completed close failed');
   closed.push(handle);this.handles.delete(handle);if(handle===30)activeClient=false;}
  async cleanup(){events.push('cleanup');for(const handle of [...this.handles].reverse())await this.close(handle);await this.call('free');this.base=0;}
 }
 const save=async()=>{};
 return{Session,packet,rpc,endpoint,state,record:r,events,closed,sends,operation:()=>api.control({Session,packet,argumentsWithPid:()=>Buffer.alloc(8),protocol:p,state,request:action,record:r,save})};
}
function mainWith(overrides){const file=path.join(ROOT,'tools/control-resident.cjs'),loaded=new Module(file,module),original=Module.createRequire(file);
 loaded.filename=file;loaded.paths=Module._nodeModulePaths(path.dirname(file));loaded.require=name=>Object.hasOwn(overrides,name)?overrides[name]:original(name);loaded._compile(fs.readFileSync(file,'utf8'),file);return loaded.exports.main;}
(async()=>{
 const p=await api.controlProtocol();
 test('retained WebEnabled command and exact four-byte input ABI are qualified locally',()=>{assert.equal(p.webEnabled,1);assert.equal(p.webInputBytes,4);assert.equal(p.summaryVersion,1);assert.equal(p.henVersion,8);});
 test('CLI accepts explicit web on/off and bounded sequential sessions only',()=>{
  assert.deepEqual(api.options([RUN,'web','off']),{run:RUN,action:'web',enabled:false});assert.equal(api.options([RUN,'sessions','32']).count,32);
  assert.deepEqual(api.options([RUN,'error']),{run:RUN,action:'error'});assert.throws(()=>api.options([RUN,'error','extra']),/Usage/);
  for(const args of [[],[RUN,'web','yes'],[RUN,'sessions','0'],[RUN,'sessions','33'],[RUN,'sessions','01'],[RUN,'sessions','1.5'],[RUN,'web','on','extra']])assert.throws(()=>api.options(args),/Usage/);
 });
 test('read-only error action derives LastError and issues exactly Summary then LastError with no mutation',async()=>{
  assert.equal(p.lastError,6);assert.equal(p.errorBytes,64);
  const f=fixture(p,{action:'error'},{errorText:'bind: address already in use'}),r=await f.operation();
  assert.equal(r.complete,true);assert.equal(r.errorText,'bind: address already in use');assert.equal(r.lastErrorResult,0);assert(r.before.resident);
  assert.equal(r.commandAttempted,false);assert.equal(r.after,undefined);assert.deepEqual(f.sends.filter(s=>s.handle===30).map(s=>s.id),[p.summary,p.lastError]);
  assert(!f.sends.some(s=>s.key==='web'));assert.deepEqual(f.closed,[30,20,10]);
 });
 test('LastError text is bounded NUL-terminated strict UTF-8 and completed errors clean owned handles',async()=>{
  for(const options of [{unterminatedError:true},{invalidErrorUtf8:true},{lastErrorFailure:true}]){
   const f=fixture(p,{action:'error'},options);await assert.rejects(f.operation);assert.equal(f.record.cleanupComplete,true);
   assert.equal(f.record.nativeOperationUncertain,false);assert(!f.sends.some(s=>s.key==='web'));assert.deepEqual(f.closed,[30,20,10]);
  }
 });
 test('LastError truncation or uncertain dispatch stops without follow-on native cleanup',async()=>{
  for(const options of [{truncateAt:'last-error'},{shortAt:'last-error'},{failAt:'send:last-error'}]){
   const f=fixture(p,{action:'error'},options);await assert.rejects(f.operation,e=>e.control.nativeOperationUncertain&&e.control.resourcesRetained);
   assert(!f.events.includes('cleanup'));assert.deepEqual(f.closed,[]);assert(!f.sends.some(s=>s.key==='web'));
  }
 });
 test('LastError is not queried unless HEN and resident Summary gates pass',async()=>{
  for(const options of [{henVersion:5},{henStartupResult:1},{foreground:true},{summaryVersion:99}]){
   const f=fixture(p,{action:'error'},options);await assert.rejects(f.operation);assert(!f.sends.some(s=>s.key==='last-error'||s.key==='web'));
  }
 });
 test('web on and off return independent before/after Summaries and close all owned handles',async()=>{
  for(const enabled of [true,false]){
   const f=fixture(p,{action:'web',enabled},{initialWeb:!enabled}),r=await f.operation();
   assert.equal(r.complete,true);assert.equal(r.before.webRunning,!enabled);assert.equal(r.after.webRunning,enabled);assert.equal(r.commandResult,0);assert.equal(r.flagMatchesRequested,true);
   assert.equal(r.before.generation,'14');assert.equal(r.after.ipv4,'192.0.2.20');assert.equal(r.clientsCompleted,1);assert.deepEqual(f.closed,[30,20,10]);
   assert.equal(f.sends.filter(s=>s.id===p.webEnabled&&s.handle===30).length,1);assert.equal(f.sends.filter(s=>s.handle===30&&s.id===p.summary).length,2);
  }
 });
 test('32 client sessions reuse one outer HEN session and close each client before the next',async()=>{
  const f=fixture(p,{action:'sessions',count:32}),r=await f.operation();assert.equal(r.complete,true);assert.equal(r.clientsCompleted,32);
  assert.equal(r.clients.length,32);assert(r.clients.every(c=>c.closeComplete&&c.summary.resident));assert.equal(f.sends.filter(s=>s.key==='open-hen').length,1);
  assert.equal(f.sends.filter(s=>s.key==='open-client').length,32);assert.equal(f.closed.filter(h=>h===30).length,32);assert.deepEqual(f.closed.slice(-2),[20,10]);
  assert(!f.sends.some(s=>s.key==='web'));assert.equal(r.commandAttempted,false);
 });
 test('HEN startup/protocol and resident Summary gates prevent WebEnabled',async()=>{
  for(const options of [{henVersion:5},{henStartupResult:0x202},{foreground:true},{summaryVersion:99},{summaryError:true},{openError:true}]){
   const f=fixture(p,undefined,options);await assert.rejects(f.operation);assert(!f.sends.some(s=>s.key==='web'));assert.equal(f.record.cleanupComplete,true);
  }
 });
 test('definite command rejection still retains independent after Summary and no retry',async()=>{
  const f=fixture(p,undefined,{commandError:true});await assert.rejects(f.operation,/WebEnabled failed/);assert.equal(f.record.commandResult,0x202);assert.equal(f.record.commandPending,false);
  assert(f.record.before&&f.record.after);assert.equal(f.sends.filter(s=>s.key==='web').length,1);assert.equal(f.record.cleanupComplete,true);
 });
 test('completed toggle with wrong observed flag fails after one command',async()=>{
  const f=fixture(p,undefined,{flagMismatch:true});await assert.rejects(f.operation,/requested Summary flag/);assert.equal(f.record.flagMatchesRequested,false);assert.equal(f.record.cleanupComplete,true);
  assert.equal(f.sends.filter(s=>s.key==='web').length,1);
 });
 test('initial allocation uncertainty suppresses scratch restoration and all cleanup',async()=>{
  const f=fixture(p,undefined,{failAt:'allocate'});await assert.rejects(f.operation,e=>e.control.nativeOperationUncertain&&e.control.resourcesRetained);assert.deepEqual(f.events,['allocate']);
 });
 test('uncertain WebEnabled dispatch leaves pending evidence and blocks both transports without cleanup',async()=>{
  const f=fixture(p,undefined,{failAt:'send:web'});await assert.rejects(f.operation,e=>e.control.nativeOperationUncertain&&e.control.commandPending);
  assert.equal(f.record.commandAttempted,true);assert.equal(f.record.after,undefined);assert(!f.events.includes('cleanup'));assert.deepEqual(f.closed,[]);
  await assert.rejects(()=>f.endpoint.request(),/timed out/);await assert.rejects(()=>f.rpc.write(),/timed out/);assert.equal(f.sends.filter(s=>s.key==='web').length,1);
 });
 test('truncated declared reply before or after command stops without cleanup or another command',async()=>{
  for(const key of ['status','summary:1','summary:2']){
   const f=fixture(p,undefined,{truncateAt:key});await assert.rejects(f.operation,e=>e.control.nativeOperationUncertain);assert(!f.events.includes('cleanup'));
   assert(f.sends.filter(s=>s.key==='web').length<=1);
  }
 });
 test('physically truncated copied-handle reply retains allocation and unknown ownership',async()=>{
  const f=fixture(p,undefined,{shortAt:'open-client'});await assert.rejects(f.operation,e=>e.control.resourcesRetained&&e.control.nativeOperationUncertain);assert(!f.events.includes('cleanup'));
 });
 test('duplicate already-owned returned handle stops as unknown ownership',async()=>{
  const f=fixture(p,undefined,{extraHandleAt:'open-client',duplicateHandle:true});await assert.rejects(f.operation,/already owned/);assert(!f.events.includes('cleanup'));
 });
 test('unexpected complete new handle is owned and closed on definite reply-shape error',async()=>{
  const f=fixture(p,undefined,{extraHandleAt:'summary:1'});await assert.rejects(f.operation,/unexpected handles/);assert.deepEqual(f.closed,[40,30,20,10]);assert.equal(f.record.cleanupComplete,true);
 });
 test('failed individual close never repeats close or opens another client',async()=>{
  for(const options of [{failAt:'close:30'},{closeError:true}]){
   const f=fixture(p,{action:'sessions',count:32},options);await assert.rejects(f.operation,e=>e.control.resourcesRetained&&e.control.nativeOperationUncertain);
   assert.equal(f.sends.filter(s=>s.key==='open-client').length,1);assert.equal(f.events.filter(e=>e==='close:30').length,1);assert(!f.events.includes('cleanup'));
  }
 });
 test('actual main creates exclusive 0600 evidence before dispatch, retains uncertainty, and restores caller env',async()=>{
  const f=fixture(p,undefined,{failAt:'send:web'}),writes=[],engine=path.join(ROOT,'research/sources/ns2jb/engine');
  const main=mainWith({
   'node:fs/promises':{...require('node:fs/promises'),async mkdir(){},async writeFile(file,data,options){assert(file.includes('/evidence/kaihen/resident-control-'));assert.equal(options.mode,0o600);
    if(!writes.length){assert.equal(options.flag,'wx');assert.equal(f.events.length,0);}writes.push(JSON.parse(data));}},
   [path.join(engine,'shared/rpc.cjs')]:f.rpc,[path.join(engine,'switch2/tools/endpoint.cjs')]:f.endpoint,
   [path.join(engine,'switch2/app/session.cjs')]:{Session:f.Session},[path.join(engine,'shared/ipc.cjs')]:{packet:f.packet},
   [path.join(engine,'switch2/ro/nro.cjs')]:{argumentsWithPid:()=>Buffer.alloc(8)},
  });
  const env=process.env.NS2_RUN_DIR;await assert.rejects(()=>main([RUN,'web','on']),e=>e.controlFile&&e.control.nativeOperationUncertain);assert.equal(process.env.NS2_RUN_DIR,env);
  assert.equal(writes.at(-1).commandPending,true);assert.equal(writes.at(-1).complete,false);assert.equal(writes.at(-1).resourcesRetained,true);
  assert(!JSON.stringify(writes).includes('pairingCode'));
 });
 for(const {name,action}of tests){await action();console.log('PASS: '+name);}assert.equal(connections,0);console.log('PASS: '+tests.length+' offline resident-control checks; no endpoint/HTTP connections');
})().catch(e=>{console.error(e.stack);process.exitCode=1;});
