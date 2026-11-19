#!/usr/bin/env node
'use strict';
// Pure host mocks: any endpoint or HTTP connection is a test failure.
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs'),Module=require('node:module');
const net=require('node:net'),http=require('node:http');let connections=0;
net.createConnection=http.request=()=>{connections++;throw Error('Inspector tests cannot connect');};
const root=path.resolve(__dirname,'..'),beforeEnv=process.env.NS2_RUN_DIR;
const {inspect,readProtocol,decodeReply}=require('./inspect-hen.cjs');
assert.equal(process.env.NS2_RUN_DIR,beforeEnv);
assert(!Object.keys(require.cache).some(name=>name.endsWith('/engine/shared/rpc.cjs')),'Import must not load live transport configuration');
const {packet}=require('../research/sources/ns2jb/engine/shared/ipc.cjs');
const {guardTransports}=require('./ready-kaihen.cjs');
const tests=[];function test(name,action){tests.push({name,action});}
function frame(data=Buffer.alloc(0),result=0,copies=[],moves=[]){
 const raw=Buffer.alloc(256),words=Math.ceil((32+data.length)/4);let at=8;
 raw.writeUInt32LE((words|((copies.length||moves.length)?0x80000000:0))>>>0,4);
 if(copies.length||moves.length){raw.writeUInt32LE(copies.length<<1|moves.length<<5,at);at+=4;for(const h of [...copies,...moves]){raw.writeUInt32LE(h,at);at+=4;}}
 const header=(at+15)&~15;raw.write('SFCO',header);raw.writeUInt32LE(result,header+8);data.copy(raw,header+16);return raw;
}
function request(frame){
 let at=8;if(frame.readUInt32LE(4)>>>31){const special=frame.readUInt32LE(at);at+=4+(special&1?8:0)+((special>>>1&15)+(special>>>5&15))*4;}
 const header=(at+15)&~15;assert.equal(frame.toString('ascii',header,header+4),'SFCI');return{id:frame.readUInt32LE(header+8),input:frame.subarray(header+16)};
}
function fixture(protocol,options={}){
 const events=[],closed=[],names=options.names??['echo','moader'];let outbound,lastKey,reply,gameReads=0;
 const fault=key=>{if(options.failAt===key)throw options.failure??Error('Native request timed out; it was not replayed');};
 const rpc={
  async call(key,handle){events.push(key);fault(key);return 0;},
  async write(){events.push('write');fault('write');},async read(){events.push('read');fault('read');},
 };
 const endpoint={async request(){events.push('retained-request');}};
 const transportGuard=guardTransports({rpc,endpoint});
 function dispatch(handle){
  const {id,input}=request(outbound);let data=Buffer.alloc(0),result=0,copies=[];
  if(handle===10&&id===protocol.initialize)lastKey='initialize-ro';
  else if(handle===10&&id===protocol.homebrew){lastKey='open-hen';copies=[20];}
  else if(handle===20&&id===protocol.status){lastKey='status';data=Buffer.alloc(16);data.writeBigUInt64LE(55n);data.writeUInt32LE(protocol.henVersion,8);}
  else if(handle===20&&id===protocol.game){lastKey='game';data=Buffer.alloc(24);
   const phases=options.gamePhases??[protocol.gameReady];
   data.writeUInt32LE(phases[Math.min(gameReads++,phases.length-1)]);
   data.writeBigUInt64LE(77n,16);}
  else if(handle===20&&id===protocol.trace){lastKey='trace';data=Buffer.alloc(24);data.writeUInt32LE(42);if(options.traceError){data=Buffer.alloc(0);result=0x202;}}
  else if(handle===20&&id===protocol.count){lastKey='count';data=Buffer.alloc(4);data.writeUInt32LE(options.count??names.length);}
  else if(handle===20&&id===protocol.name){lastKey='name';const index=input.readUInt32LE();assert(index<names.length);data=Buffer.alloc(protocol.nameBytes);data.write(names[index]);if(options.unterminatedName)data.fill(65);}
  else if(handle===20&&id===protocol.open){lastKey='open-moader';assert.equal(input.subarray(0,64).toString().replace(/\0+$/,''),'moader');copies=options.openError?[]:[30];result=options.openError?0x202:0;}
  else if(handle===30&&id===protocol.summary){
   lastKey='summary';data=Buffer.alloc(64);data.writeUInt32LE(options.summaryVersion??protocol.summaryVersion);data.writeUInt32LE(options.flags??3,4);
   data.writeBigUInt64LE(9n,8);data.writeBigUInt64LE(9n,16);data.writeUInt32LE(2,24);data.writeUInt16LE(7712,28);
   Buffer.from([192,0,2,22]).copy(data,32);data.write('Routing connected',40);
   if(options.summaryError){result=0x202;data=Buffer.alloc(0);}
  }else throw Error('Unexpected or mutating inspection command '+handle+':'+id);
  if(options.unexpectedHandleAt===lastKey)copies.push(40);
  reply=frame(data,result,copies);
  if(options.truncateAt===lastKey)reply.writeUInt32LE((reply.readUInt32LE(4)&0xfffffc00)|8,4);
  if(options.shortFrameAt===lastKey)reply=reply.subarray(0,10);
 }
 class Session{
  constructor(label){this.label=label;this.base=0;this.bytes=0x20000;this.handles=new Set();}
  async initialize(){try{await this.call('allocate');this.base=0x10000;}finally{await this.write(0,Buffer.alloc(8));}}
  async openService(name){assert.equal(name,'ldr:ro');await rpc.call('open-ro');this.handles.add(10);return 10;}
  async write(at,data){await rpc.write();if(data.length===256)outbound=data;}
  async call(kind,base,size,handle){
   if(kind==='send'){dispatch(handle);return rpc.call('send:'+lastKey);}
   return rpc.call(kind);
  }
  async readOwned(){await rpc.read();return reply;}
  async close(handle){await this.call('close:'+handle);closed.push(handle);this.handles.delete(handle);}
  async cleanup(){events.push('cleanup');for(const handle of [...this.handles].reverse()){
   await this.close(handle);
  }await this.call('free');this.base=0;}
 }
 const borrowed=options.borrowed?{session:new Session('hen-startup'),handle:10,
  file:'/current/switch2-Test/switch2/ro/hen-startup.json',sessionId:'seedsow'}:null;
 if(borrowed){borrowed.session.base=0x10000;borrowed.session.handles.add(10);}
 return{Session,rpc,endpoint,transportGuard,events,closed,borrowed,
  operation:()=>inspect({Session,packet,argumentsWithPid:()=>Buffer.alloc(8),protocol,
   transportGuard,run:'/current/switch2-Test',borrowedRo:borrowed})};
}
function stubMain(overrides){
 const file=path.join(root,'tools/inspect-hen.cjs'),loaded=new Module(file,module),original=Module.createRequire(file);
 loaded.filename=file;loaded.paths=Module._nodeModulePaths(path.dirname(file));
 loaded.require=name=>Object.hasOwn(overrides,name)?overrides[name]:original(name);loaded._compile(fs.readFileSync(file,'utf8'),file);return loaded.exports.main;
}
(async()=>{
 const p=await readProtocol();
 test('retained source defines the exact bounded inspection protocol',()=>{
  assert.deepEqual([p.initialize,p.homebrew,p.status,p.game,p.trace,p.count,p.name,p.open,p.summary],[4,65004,0,8,0x4d4f0001,20,21,22,0]);
  assert.equal(p.maxPlugins,16);assert.equal(p.nameBytes,64);assert.equal(p.summaryVersion,1);
 });
 test('HEN/plugins and independent resident Summary decode, with every returned handle closed',async()=>{
  const f=fixture(p),r=await f.operation();assert.equal(r.complete,true);assert.equal(r.controllerHandlesClosed,true);assert.equal(r.hen.pid,'55');
  assert.deepEqual(r.plugins,{count:2,names:['echo','moader'],enumerationComplete:true});
  assert.equal(r.moader.serviceReachable,true);assert.equal(r.moader.resident,true);assert.equal(r.moader.summary.ipv4,'192.0.2.22');
  assert.equal(r.moader.summary.port,7712);assert.equal(r.moader.summary.generation,'9');assert.equal(r.moader.summary.routingGenerationCurrent,true);
  assert.deepEqual(f.closed,[30,20,10]);assert(f.events.includes('send:summary'));
 });
 test('game arm must leave startup and reach waiting-for-game before service success',async()=>{
  const f=fixture(p,{gamePhases:[1,2,p.gameReady]}),r=await f.operation();
  assert.equal(r.game.phase,p.gameReady);assert.equal(f.events.filter(e=>e==='send:game').length,3);
  const fault=fixture(p,{gamePhases:[p.gameFault]});
  await assert.rejects(fault.operation,/game arm faulted/);
  assert(!fault.events.includes('send:trace'));
 });
 test('held Seedsow inspection reuses its initialized RO context and closes only new handles',async()=>{
  const f=fixture(p,{borrowed:true}),r=await f.operation();
  assert.equal(r.complete,true);assert.equal(r.controllerHandlesClosed,true);
  assert.equal(r.borrowedRoRetained,true);assert.equal(r.moader.serviceReachable,true);
  assert.deepEqual(f.closed,[30,20]);assert.deepEqual([...f.borrowed.session.handles],[10]);
  assert(!f.events.includes('allocate')&&!f.events.includes('open-ro')&&
   !f.events.includes('send:initialize-ro')&&!f.events.includes('cleanup'));
 });
 test('held Seedsow inspection refuses a foreign RO lease before any native request',async()=>{
  const f=fixture(p,{borrowed:true});f.borrowed.handle=99;
  await assert.rejects(f.operation,/not owned/);assert.deepEqual(f.events,[]);
 });
 test('uncertain borrowed RO status retains its original and new handles without cleanup',async()=>{
  const f=fixture(p,{borrowed:true,failAt:'send:status'});
  await assert.rejects(f.operation,error=>error.inspection.nativeOperationUncertain&&
   error.inspection.resourcesRetained);
  assert.deepEqual(f.closed,[]);assert.deepEqual([...f.borrowed.session.handles],[10,20]);
  assert(!f.events.includes('cleanup')&&!f.events.includes('send:game'));
 });
 test('missing plugin reports absence without opening a fallback or launching anything',async()=>{
  const f=fixture(p,{names:[]}),r=await f.operation();assert.equal(r.complete,true);assert.equal(r.moader.listed,false);assert.equal(r.moader.serviceReachable,false);
  assert(!f.events.includes('send:open-moader'));assert(!f.events.includes('send:summary'));assert.deepEqual(f.closed,[20,10]);
 });
 test('plugin open and Summary errors remain explicit, with guarded cleanup and no fallback',async()=>{
  for(const options of [{openError:true},{summaryError:true}]){
   const f=fixture(p,options),r=await f.operation();assert.equal(r.complete,true);assert.equal(r.moader.serviceReachable,false);
   assert.equal(r.moader[options.openError?'openResult':'summaryResult'],0x202);assert.equal(r.controllerHandlesClosed,true);
  }
 });
 test('foreground Summary cannot be mistaken for resident proof',async()=>{
  const r=await fixture(p,{flags:33}).operation();assert.equal(r.moader.serviceReachable,true);assert.equal(r.moader.resident,false);assert.equal(r.moader.summary.foreground,true);
 });
 test('plugin enumeration refuses counts above the retained bound',async()=>{
  const f=fixture(p,{count:17});await assert.rejects(f.operation,/exceeds retained bound/);assert(!f.events.includes('send:name'));assert.deepEqual(f.closed,[20,10]);
 });
 test('allocation uncertainty suppresses scratch restoration, cleanup and retained follow-on work',async()=>{
  const f=fixture(p,{failAt:'allocate'});await assert.rejects(f.operation,error=>error.inspection.nativeOperationUncertain&&error.inspection.resourcesRetained);
  assert.deepEqual(f.events,['allocate']);await assert.rejects(()=>f.endpoint.request(),/timed out/);assert.deepEqual(f.events,['allocate']);
 });
 test('post-dispatch uncertainty retains all sessions and blocks both transports',async()=>{
  const f=fixture(p,{failAt:'send:status'});await assert.rejects(f.operation,error=>error.inspection.stage==='Read HEN startup'&&error.inspection.nativeOperationUncertain);
  assert(!f.events.includes('cleanup'));assert.deepEqual(f.closed,[]);assert(!f.events.includes('send:game'));
  await assert.rejects(()=>f.endpoint.request(),/timed out/);await assert.rejects(()=>f.rpc.write(),/timed out/);
 });
 test('unqualified reply read failure retains the completed-send allocation and possible handles',async()=>{
  const f=fixture(p,{failAt:'read',failure:Error('Read operation rejected')});
  await assert.rejects(f.operation,error=>error.inspection.nativeOperationUncertain&&error.inspection.resourcesRetained);
  assert(!f.events.includes('cleanup'));assert(!f.events.includes('send:open-hen'));
 });
 test('uncertain cleanup stops before closing another handle',async()=>{
  const f=fixture(p,{failAt:'close:30'});await assert.rejects(f.operation,error=>error.inspection.nativeOperationUncertain&&error.inspection.resourcesRetained);
  assert(f.events.includes('close:30'));assert(!f.events.includes('close:20'));assert.deepEqual(f.closed,[]);
 });
 test('declared scalar truncation rejects padded allocations before decoding or later queries',async()=>{
  for(const key of ['status','game','trace','count','name','summary']){
   const f=fixture(p,{truncateAt:key});await assert.rejects(f.operation,error=>/truncated scalar/.test(error.message)&&error.inspection.nativeOperationUncertain);
   assert(!f.events.includes('cleanup'));assert.deepEqual(f.closed,[]);
  }
 });
 test('physically truncated handle frames preserve resources instead of guessing ownership',async()=>{
  const f=fixture(p,{shortFrameAt:'open-hen'});await assert.rejects(f.operation,/truncated special header/);assert(!f.events.includes('cleanup'));
 });
 test('unexpected but complete copied handles are owned and closed on definite protocol failure',async()=>{
  const f=fixture(p,{unexpectedHandleAt:'status'});await assert.rejects(f.operation,/unexpected handles/);assert.deepEqual(f.closed,[40,20,10]);
 });
 test('invalid names and unsupported Summary versions are rejected before use',async()=>{
  for(const options of [{unterminatedName:true},{summaryVersion:99}]){
   const f=fixture(p,options);await assert.rejects(f.operation,/Unterminated|Unsupported Moader/);assert(f.events.includes('cleanup'));
  }
 });
 test('optional trace rejection records its result without decoding absent data',async()=>{
  const r=await fixture(p,{traceError:true}).operation();assert.equal(r.traceResult,0x202);assert(!r.trace);assert(r.moader.serviceReachable);
 });
 test('CLI wires guarded transports and preserves a failed inspection in exclusive local evidence',async()=>{
  const f=fixture(p,{failAt:'allocate'}),writes=[],engine=path.join(root,'research/sources/ns2jb/engine');
  const main=stubMain({
   'node:fs/promises':{...require('node:fs/promises'),async mkdir(){},async writeFile(file,data,options){assert(file.includes('/evidence/kaihen/status-'));assert.equal(options.flag,'wx');writes.push(JSON.parse(data));}},
   [path.join(engine,'shared/rpc.cjs')]:f.rpc,[path.join(engine,'switch2/tools/endpoint.cjs')]:f.endpoint,
   [path.join(engine,'switch2/app/session.cjs')]:{Session:f.Session},[path.join(engine,'shared/ipc.cjs')]:{packet},
   [path.join(engine,'switch2/ro/nro.cjs')]:{argumentsWithPid:()=>Buffer.alloc(8)},
  });
  const env=process.env.NS2_RUN_DIR;
  try{await assert.rejects(()=>main(['/current/switch2-Test']),error=>error.inspectionFile&&error.inspection.nativeOperationUncertain);}
  finally{if(env===undefined)delete process.env.NS2_RUN_DIR;else process.env.NS2_RUN_DIR=env;}
  assert.equal(writes.length,1);assert.equal(writes[0].complete,false);assert.equal(writes[0].inspectBeforeRetry,true);assert.deepEqual(f.events,['allocate']);
 });
 test('malformed HIPC declared length cannot extend beyond the actual message buffer',()=>{
  const raw=frame(Buffer.alloc(16));raw.writeUInt32LE(1023,4);assert.throws(()=>decodeReply(raw,()=>{},16),/truncated or invalid CMIF/);
 });
 for(const {name,action}of tests){await action();console.log('PASS: '+name);}
 assert.equal(connections,0);console.log('PASS: '+tests.length+' offline inspector checks; no endpoint/HTTP connections');
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
