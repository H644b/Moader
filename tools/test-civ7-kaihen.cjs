#!/usr/bin/env node
'use strict';
// Independent host adapter checks. Synthetic files/PM replies only; no upstream
// acquisition, network, RPC connection, child process or native binary runs.
const assert=require('node:assert/strict'),fs=require('node:fs'),fsp=require('node:fs/promises'),os=require('node:os'),path=require('node:path'),Module=require('node:module'),vm=require('node:vm'),crypto=require('node:crypto');
const ROOT=path.resolve(__dirname,'..');let forbidden=0,passed=0;const failures=[];
function deny(){forbidden++;throw Error('Civ7 adapter test forbids network/child/native execution');}
const net=require('node:net'),http=require('node:http'),https=require('node:https'),tls=require('node:tls'),dgram=require('node:dgram'),cp=require('node:child_process');
net.Socket.prototype.connect=net.Server.prototype.listen=deny;http.request=http.get=https.request=https.get=tls.connect=deny;
dgram.Socket.prototype.bind=dgram.Socket.prototype.connect=dgram.Socket.prototype.send=deny;
for(const name of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[name]=deny;
globalThis.fetch=deny;
const originalLoad=Module._load;
Module._load=function(name,parent,main){const file=Module._resolveFilename(name,parent,main);if(typeof file==='string'&&/\/(?:rpc|endpoint|fs-memory|pipeline)\.cjs$/.test(file))deny();return originalLoad.apply(this,arguments);};
const adapter=require('./host-kaihen.cjs'),filename=path.join(ROOT,'tools/host-kaihen.cjs');
const work=fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(),'moader-civ7-kaihen-')));let serial=0;
const RO_PROGRAM=0x0400000000000037n;
async function test(name,fn){try{await fn();passed++;console.log('PASS: '+name);}catch(error){failures.push({name,error:error.message});console.error('FAIL: '+name+' — '+error.message);}}
function put(root,name,data){const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,Buffer.isBuffer(data)?data:JSON.stringify(data));return file;}
function parsePackage(bytes){
 assert.equal(bytes.toString('ascii',0,4),'PFS0');assert.equal(bytes.readUInt32LE(12),0);
 const count=bytes.readUInt32LE(4),nameBytes=bytes.readUInt32LE(8),namesAt=16+count*24,dataAt=namesAt+nameBytes;
 assert.equal(count,2);assert.equal(nameBytes,15);assert(dataAt<=bytes.length);
 const names=bytes.subarray(namesAt,dataAt),entries=[];
 for(let index=0;index<count;index++){
  const row=16+24*index,offset=Number(bytes.readBigUInt64LE(row)),size=Number(bytes.readBigUInt64LE(row+8)),nameOffset=bytes.readUInt32LE(row+16);
  assert.equal(bytes.readUInt32LE(row+20),0);assert(Number.isSafeInteger(offset)&&Number.isSafeInteger(size));assert(size>0&&offset>=0&&offset+size<=bytes.length-dataAt);
  assert(nameOffset<names.length);const end=names.indexOf(0,nameOffset);assert(end>=nameOffset);
  entries.push({name:names.toString('ascii',nameOffset,end),offset,size,nameOffset,data:bytes.subarray(dataAt+offset,dataAt+offset+size)});
 }
 assert.deepEqual(entries.map(row=>row.name),['main','main.npdm']);assert.deepEqual(entries.map(row=>row.nameOffset),[0,5]);
 assert.equal(entries[0].offset,0);assert.equal(entries[1].offset%32,0);assert(entries[1].offset>=entries[0].size&&entries[1].offset-entries[0].size<32);
 assert(bytes.subarray(dataAt+entries[0].size,dataAt+entries[1].offset).every(value=>value===0));assert.equal(dataAt+entries[1].offset+entries[1].size,bytes.length);
 return entries;
}
function bootstrap(text,address=0x6123456780n){
 const image=Buffer.alloc(0x12000),offset=Math.ceil(text.length/16)*16;image.write('NRO0',0x10);
 for(const [at,value]of [[0x18,0x12000],[0x24,0x10000],[0x28,0x10000],[0x2c,0x1000],[0x30,0x11000],[0x34,0x1000],[0x38,0x1000]])image.writeUInt32LE(value,at);
 text.copy(image,0x80);
 // Independent literal encoding, with a 64-bit address above 4 GiB.
 Buffer.from('00f07d927000005800021fd61f2003d5','hex').copy(image,0x80+offset);image.writeBigUInt64LE(address,0x80+offset+16);
 return{image,offset,address,text};
}
function activationFixture(){
 const run=path.join(work,'switch2-test'+(++serial)),session=path.basename(run),report='switch2/synthetic-report.json';
 const execution={verified:true,session,report,pid:'47',programId:'0400000000000037'};
 const launch={session,report,pid:'83',imageAccepted:true,restored:true,flags:0x10};
 const file=put(run,'switch2/ro/synthetic-launch.json',launch);put(run,'switch2/ro/current-execution.json',execution);
 const stock=Buffer.alloc(8);stock.writeBigUInt64LE(11n);put(run,'switch2/ro/'+session+'-sm-proof.json',{report,program:{result:0,hex:stock.toString('hex')}});
 const context={run,profile:{id:'civ7-s8n'}},calls=[];
 function memory({fault=-1,replyFault=-1,wrongProgram=-1,infoOverride}={}){
  return{reportFile:report,request:async request=>{
   const index=calls.length;calls.push(structuredClone(request));
   if(index===fault)throw Object.assign(Error('independent uncertain IPC '+index),{code:'ETIMEDOUT',nativeRequestUncertain:true});
   if(request.command==='root-native-info')return infoOverride||{worker:{joined:true},services:[{name:'pm:shell',handle:25},{name:'pm:dmnt',handle:31}]};
   assert.equal(request.command,'root-native-ipc');assert.equal(request.hex.length,16);
   if(index===replyFault)return{result:0,transport:0x1234,hex:''};
   const bytes=Buffer.alloc(8);bytes.writeBigUInt64LE(index===wrongProgram?0x0100c3601518c000n:RO_PROGRAM);
   return{result:0,transport:0,hex:bytes.toString('hex')};
  }};
 }
 function journal(){const names=fs.readdirSync(path.join(run,'switch2/ro')).filter(name=>name.startsWith('moader-activation-'));assert.equal(names.length,1);return JSON.parse(fs.readFileSync(path.join(run,'switch2/ro',names[0])));}
 return{run,session,report,execution,launch,file,context,calls,memory,journal,save(){put(run,'switch2/ro/current-execution.json',execution);put(run,'switch2/ro/synthetic-launch.json',launch);}};
}
function adapterWithHost(host){
 const loaded=new Module(filename,module);loaded.filename=filename;loaded.paths=Module._nodeModulePaths(path.dirname(filename));
 loaded.require=name=>name==='./host-session.cjs'?host:module.require(name);
 loaded._compile(fs.readFileSync(filename,'utf8'),filename);return loaded.exports;
}
function pinnedSdkThread({plain=false,thread=0x6123456007,states=[1],createResult=0,waitError,joinResult=0,verifyFailure}={}){
 const sourceRoot=path.resolve(process.argv[2]||path.join(ROOT,'research/sources/civ7-fs-kaihen'));
 const config=JSON.parse(fs.readFileSync(path.join(ROOT,'config/civ7-provider.json'))),relative='engine/switch2/ro/sdk-thread.cjs',bytes=fs.readFileSync(path.join(sourceRoot,relative));
 const digest=value=>crypto.createHash('sha256').update(value).digest('hex');
 assert.equal(config.revision,'51a804cbce09216e1a42e65929644fd436b4c8bf');
 assert.equal(config.files[relative],'21f8cc4116a7031f0670bff70956df8d1e132391842d22c11cad094b299d463d');assert.equal(digest(bytes),config.files[relative]);
 const tableBytes=fs.readFileSync(path.join(sourceRoot,'targets/civ7/app/thread-code.json'));
 assert.equal(config.files['targets/civ7/app/thread-code.json'],'647b0c42cb2898d423e5db7f2fd3c6c879e08d7593d3fcf918f000bf3e7b83e4');assert.equal(digest(tableBytes),config.files['targets/civ7/app/thread-code.json']);
 const table=JSON.parse(tableBytes),code=table.functions;if(plain)delete table.pthreadHandle;
 const live={sdk:0x6123400000},calls=[],saves=[],backing={owned:true},record={},entry=0x6234500000;let clock=0,waitCount=0;
 const check=(ok,message)=>{if(!ok)throw Error(message);};
 const session={base:0x6345600000,threads:new Set(),backing,
  verifyCode:async(expected,name)=>{assert.equal(expected,code[name]);calls.push({kind:'verify',name});if(name===verifyFailure)throw Error('synthetic code mismatch');},
  write:async(address,data)=>{calls.push({kind:'write',address,bytes:Buffer.from(data)});},
  readOwned:async(address,size)=>{assert.equal(size,8);const bytes=Buffer.alloc(8);bytes.writeBigUInt64LE(BigInt(address===session.base+0x6000?thread:0x12345678));calls.push({kind:'read',address});return bytes;},
  cleanup:async()=>{backing.owned=false;calls.push({kind:'cleanup'});},
  rpc:{call4:async(address,...args)=>{calls.push({kind:'create',address,args});return createResult;},
   call3:async(address,...args)=>{
    if(address===live.sdk+code.tryWait.offset){calls.push({kind:'wait',address,args});if(waitError)throw waitError;return states[Math.min(waitCount++,states.length-1)];}
    assert.equal(address,live.sdk+code.join.offset);calls.push({kind:'join',address,args});return joinResult;
   }}
 };
 const controlledRequire=name=>{
  if(name==='../app/session.cjs')return{live,check};if(name==='../app/worker.cjs')return{code};if(name==='../app/thread-code.json')return table;
  throw Error('Unmocked SDK-thread dependency: '+name);
 };
 // Evaluate exactly the hashed source with only its three local dependencies
 // supplied. Virtual clock/polls make timeout coverage bounded and repeatable.
 const sandbox=vm.createContext({Buffer,Date:{now:()=>clock},setTimeout:callback=>{clock+=10;callback();return 0;}}),loaded={exports:{}};
 new vm.Script('(function(require,module,exports){\n'+bytes.toString('utf8')+'\n})',{filename:'pinned-sdk-thread.cjs'}).runInContext(sandbox)(controlledRequire,loaded,loaded.exports);
 const save=async()=>{saves.push(structuredClone(record));};
 return{api:loaded.exports,session,record,calls,saves,backing,thread,live,code,entry,execute:config=>loaded.exports.execute(session,entry,record,save,config)};
}
(async()=>{
 await test('PFS0 has exact independent two-entry geometry and unchanged contents',()=>{
  for(const size of [4,31,32,33,4097]){
   const image=Buffer.alloc(size,0x97);image.write('NSO0');const policy=Buffer.alloc(257,0x53),beforeImage=Buffer.from(image),beforePolicy=Buffer.from(policy);
   const rows=parsePackage(adapter.packageBytes(image,policy));assert.deepEqual(rows[0].data,image);assert.deepEqual(rows[1].data,policy);assert.deepEqual(image,beforeImage);assert.deepEqual(policy,beforePolicy);
  }
 });
 await test('PFS0 rejects empty, invalid-framing and oversized input',()=>{
  for(const image of [Buffer.alloc(0),Buffer.from('NSO'),Buffer.from('bad!')])assert.throws(()=>adapter.packageBytes(image,Buffer.from('policy')));
  assert.throws(()=>adapter.packageBytes(Buffer.from('NSO0'),Buffer.alloc(0)));
  assert.throws(()=>adapter.packageBytes(Buffer.from('NSO0'),Buffer.alloc(1024*1024+1)));
  const oversized=Buffer.alloc(32*1024*1024+1);oversized.write('NSO0');assert.throws(()=>adapter.packageBytes(oversized,Buffer.from('policy')));
 });
 await test('package publication is exclusive and preserves existing output',async()=>{
  const dir=path.join(work,'package');fs.mkdirSync(dir);const image=put(dir,'main',Buffer.from('NSO0synthetic')),policy=put(dir,'policy',Buffer.from('synthetic-policy')),output=path.join(dir,'package.nsp');
  const result=await adapter.buildPackage(image,policy,output);assert.equal(result.file,output);assert.equal(result.size,fs.statSync(output).size);parsePackage(fs.readFileSync(output));assert.equal(fs.statSync(output).mode&0o777,0o600);
  const before=fs.readFileSync(output);await assert.rejects(()=>adapter.buildPackage(image,policy,output),{code:'EEXIST'});assert.deepEqual(fs.readFileSync(output),before);
 });
 await test('bootstrap retains exact prefix, zero alignment and 24-byte tagged shim',()=>{
  for(const length of [1,15,16,17,0xff60]){
   const f=bootstrap(Buffer.alloc(length,0xa5)),before=Buffer.from(f.image),adapterBytes=adapter.tryWaitAdapter(f.address);
   assert.equal(adapterBytes.length,24);assert.deepEqual([...Array(4)].map((_,index)=>adapterBytes.readUInt32LE(index*4)),[0x927df000,0x58000070,0xd61f0200,0xd503201f]);assert.equal(adapterBytes.readBigUInt64LE(16),f.address);
   assert.equal(adapter.validateBootstrapImage(f.image,f.text,f.address),f.offset);assert.deepEqual(f.image,before);
  }
 });
 await test('bootstrap rejects prefix, padding, opcode, literal, magic and size corruption',()=>{
  const f=bootstrap(Buffer.alloc(17,0xa5));
  for(const offset of [0x80,0x80+17,0x80+f.offset,0x80+f.offset+8,0x80+f.offset+16,0x10]){const image=Buffer.from(f.image);image[offset]^=1;assert.throws(()=>adapter.validateBootstrapImage(image,f.text,f.address));}
  assert.throws(()=>adapter.validateBootstrapImage(f.image.subarray(0,-1),f.text,f.address));
  const huge=bootstrap(Buffer.alloc(0xff70,0xa5));assert.throws(()=>adapter.validateBootstrapImage(huge.image,huge.text,huge.address));
  assert.throws(()=>adapter.tryWaitAdapter(f.address,3));assert.throws(()=>adapter.tryWaitAdapter(0));
 });
 await test('bootstrap rejects corrupt NRO segment/header geometry',()=>{
  const f=bootstrap(Buffer.alloc(17,0xa5));
  for(const offset of [0x18,0x24,0x28,0x2c,0x30,0x34,0x38]){const image=Buffer.from(f.image);image[offset]^=1;assert.throws(()=>adapter.validateBootstrapImage(image,f.text,f.address),'Corrupt NRO field at 0x'+offset.toString(16));}
 });
 await test('activation verifies/starts new RO and retires current diagnostic, never stock PID',async()=>{
  const f=activationFixture(),result=await adapter.activate(f.context,f.file,47,f.memory());
  assert.equal(result.pid,'83');assert.equal(result.originalPid,'47');assert.deepEqual(f.calls.slice(1).map(row=>[row.handle,row.id,Buffer.from(row.hex,'hex').readBigUInt64LE().toString()]),[[31,7,'83'],[31,7,'47'],[31,1,'83'],[25,1,'47']]);
  assert(!f.calls.some(row=>row.hex&&Buffer.from(row.hex,'hex').readBigUInt64LE()===11n));const journal=f.journal();assert.equal(journal.complete,true);assert.equal(journal.role,'diagnostic-ro');assert.equal(journal.originalPid,'47');assert.equal(journal.steps.length,4);assert(journal.steps.every(row=>row.pending===false));
 });
 for(const [name,mutate]of [
  ['wrong diagnostic PID',f=>f.execution.pid='48'],['diagnostic session mismatch',f=>f.execution.session='switch2-Other'],['diagnostic report mismatch',f=>f.execution.report='wrong'],['retired diagnostic',f=>f.execution.verified=false],
  ['same PID roles',f=>f.launch.pid='47'],['invalid launch PID',f=>f.launch.pid='0'],['unaccepted image',f=>f.launch.imageAccepted=false],['unrestored launch',f=>f.launch.restored=false],['wrong launch flags',f=>f.launch.flags=0],['wrong launch session',f=>f.launch.session='switch2-Other']
 ])await test(name+' stops before any PM dispatch',async()=>{const f=activationFixture();mutate(f);f.save();await assert.rejects(()=>adapter.activate(f.context,f.file,47,f.memory()));assert.equal(f.calls.length,0);});
 for(let fault=0;fault<5;fault++)await test('uncertainty at request '+fault+' stops all later activation dispatch',async()=>{
  const f=activationFixture();await assert.rejects(()=>adapter.activate(f.context,f.file,47,f.memory({fault})),/independent uncertain IPC/);assert.equal(f.calls.length,fault+1);
  if(fault){const record=f.journal();assert.notEqual(record.complete,true);assert.equal(record.steps.at(-1).pending,true);assert.equal(record.steps.length,fault);}
 });
 await test('uncertain activation journal retains precise error context at every IPC',async()=>{
  for(let fault=1;fault<5;fault++){const f=activationFixture();await assert.rejects(()=>adapter.activate(f.context,f.file,47,f.memory({fault})));const record=f.journal();assert.equal(record.steps.at(-1).pending,true);assert(JSON.stringify(record).includes('independent uncertain IPC '+fault),'Pending activation journal omitted error context at request '+fault);}
 });
 for(let replyFault=1;replyFault<5;replyFault++)await test('transport failure reply at request '+replyFault+' blocks later activation dispatch',async()=>{
  const f=activationFixture();await assert.rejects(()=>adapter.activate(f.context,f.file,47,f.memory({replyFault})),/failed/);assert.equal(f.calls.length,replyFault+1);assert.notEqual(f.journal().complete,true);
 });
 for(const wrongProgram of [1,2])await test('wrong RO program in identity reply '+wrongProgram+' stops before launch/termination',async()=>{
  const f=activationFixture();await assert.rejects(()=>adapter.activate(f.context,f.file,47,f.memory({wrongProgram})),/qualified RO program/);assert.equal(f.calls.length,wrongProgram+1);
 });
 await test('busy worker/missing PM sessions reject before activation dispatch',async()=>{
  for(const infoOverride of [{worker:{joined:false},services:[]},{worker:{joined:true},services:[]}]){const f=activationFixture();await assert.rejects(()=>adapter.activate(f.context,f.file,47,f.memory({infoOverride})));assert.equal(f.calls.length,1);}
 });
 await test('startHen validates image before frontend dispatch and restores wrapper after error',async()=>{
  const text=Buffer.alloc(17,0xa5),f=bootstrap(text),bootstrapFile=put(work,'synthetic-bootstrap.bin',text),originalCalls=[];
  const frontend={main:async options=>{originalCalls.push(options);throw Object.assign(Error('synthetic timeout'),{nativeRequestUncertain:true});}},original=frontend.main;
  const table={pthreadHandle:{form:'tagged',tagMask:7,join:'tagged',tryWait:'untagged'},functions:{tryWait:{offset:0x80}}};
  const host={runJson:()=>table,load:(context,relative)=>relative==='switch2/app/session.cjs'?{live:{sdk:Number(f.address)-0x80}}:relative==='switch2/ro/nro.cjs'?frontend:{main:async()=>frontend.main({label:'hen-startup',image:f.image})}};
  const isolated=adapterWithHost(host);await assert.rejects(()=>isolated.startHen({run:work},'image',bootstrapFile,'game',[]),/synthetic timeout/);assert.equal(originalCalls.length,1);assert.equal(frontend.main,original);
  f.image[0x80]^=1;await assert.rejects(()=>isolated.startHen({run:work},'image',bootstrapFile,'game',[]),/bootstrap/);assert.equal(originalCalls.length,1);assert.equal(frontend.main,original);
 });
 await test('authentic pinned waitHandle strips only tagged Civ7 low bits without pointer truncation',()=>{
  const f=pinnedSdkThread();assert.equal(f.api.waitHandle(f.thread),0x6123456000);
  for(const value of [0x6123456000,0x6123456001,0x6123456006,Number.MAX_SAFE_INTEGER+1])assert.throws(()=>f.api.waitHandle(value),/untagged thread/);
  assert.equal(f.calls.length,0);
 });
 await test('authentic pinned execute sends untagged TryWait and original tagged pthread_join',async()=>{
  const f=pinnedSdkThread(),returned=await f.execute({waitMs:20});assert.equal(returned,0x12345678);
  assert.deepEqual(f.calls.filter(row=>row.kind==='verify').map(row=>row.name),['create','join','tryWait','tryWaitImplementation']);
  const slots=f.session.base+0x6000,create=f.calls.find(row=>row.kind==='create'),wait=f.calls.find(row=>row.kind==='wait'),join=f.calls.find(row=>row.kind==='join');
  assert.equal(create.address,f.live.sdk+f.code.create.offset);assert.deepEqual(create.args,[slots,0,f.entry,slots+16]);assert.deepEqual(wait.args,[0x6123456000,0,0]);assert.deepEqual(join.args,[f.thread,slots+8,0]);
  assert.equal(f.record.thread,f.thread);assert.equal(f.record.waitThread,0x6123456000);assert.equal(f.record.joined,true);assert.equal(f.record.pendingThread,undefined);assert.equal(f.session.threads.size,0);assert.equal(f.backing.owned,true);
  assert.deepEqual(f.saves.filter(row=>row.pendingThread).map(row=>row.pendingThread),['create','wait','join']);
 });
 await test('plain profile fallback preserves pointer unchanged through wait and join',async()=>{
  const f=pinnedSdkThread({plain:true,thread:0x6123456000});assert.equal(f.api.pthreadHandle.form,'plain');assert.equal(f.api.waitHandle(f.thread),f.thread);assert.equal(f.api.waitHandle(f.thread+7),f.thread+7);
  await f.execute({waitMs:20});assert.equal(f.calls.find(row=>row.kind==='wait').args[0],f.thread);assert.equal(f.calls.find(row=>row.kind==='join').args[0],f.thread);assert.equal(f.record.waitThread,undefined);assert.equal(f.record.joined,true);
 });
 await test('untagged Civ7 create result rejects before wait/join and retains owned thread/backing',async()=>{
  const f=pinnedSdkThread({thread:0x6123456000});await assert.rejects(()=>f.execute({waitMs:20}),/untagged thread/);assert(f.session.threads.has(f.thread));assert.equal(f.backing.owned,true);assert.equal(f.calls.filter(row=>['wait','join','cleanup'].includes(row.kind)).length,0);assert.notEqual(f.record.joined,true);
 });
 await test('SDK thread poll timeout retains tagged thread/backing without joining or cleanup',async()=>{
  const f=pinnedSdkThread({states:[0]});await assert.rejects(()=>f.execute({waitMs:20}),/retain its mapped image and backing/);assert.equal(f.calls.filter(row=>row.kind==='wait').length,2);assert(f.session.threads.has(f.thread));assert.equal(f.backing.owned,true);assert.equal(f.record.pendingThread,'wait');assert.equal(f.saves.at(-1).pendingThread,'wait');assert.equal(f.calls.filter(row=>['join','cleanup'].includes(row.kind)).length,0);
 });
 await test('uncertain SDK wait retains thread/backing and does not join',async()=>{
  const timeout=Object.assign(Error('synthetic wait timed out'),{nativeRequestUncertain:true}),f=pinnedSdkThread({waitError:timeout});await assert.rejects(()=>f.execute({waitMs:20}),/synthetic wait timed out/);assert(f.session.threads.has(f.thread));assert.equal(f.backing.owned,true);assert.equal(f.calls.filter(row=>['join','cleanup'].includes(row.kind)).length,0);assert.equal(f.record.pendingThread,'wait');
 });
 await test('SDK code mismatch rejects before thread creation',async()=>{
  const f=pinnedSdkThread({verifyFailure:'tryWaitImplementation'});await assert.rejects(()=>f.execute({waitMs:20}),/synthetic code mismatch/);assert.equal(f.calls.filter(row=>row.kind==='create').length,0);assert.equal(f.session.threads.size,0);
 });
 await test('failed pthread_create retains creating marker without wait/join',async()=>{
  const f=pinnedSdkThread({createResult:1});await assert.rejects(()=>f.execute({waitMs:20}),/pthread_create failed/);assert(f.session.threads.has('creating'));assert.equal(f.calls.filter(row=>['wait','join','cleanup'].includes(row.kind)).length,0);assert.equal(f.backing.owned,true);
 });
 assert.equal(forbidden,0);console.log(JSON.stringify({passed,failed:failures.length,forbiddenIO:forbidden,syntheticOnly:true}));if(failures.length)process.exitCode=1;
})().catch(error=>{console.error(error.stack);process.exitCode=1;}).finally(()=>fsp.rm(work,{recursive:true,force:true}));
