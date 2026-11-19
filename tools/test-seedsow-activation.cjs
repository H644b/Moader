// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Execute the exact hash-pinned Seedsow activation with synthetic PM replies.
// This test never opens the console transport.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),fsp=require('node:fs/promises'),path=require('node:path');
const os=require('node:os'),Module=require('node:module');
const transforms=require('./seedsow-source-transforms.cjs');
const pins=require('./seedsow-source-loader.cjs').TRANSFORMED;
const sourceFile=path.resolve(__dirname,'../research/sources/seedsow-fs-kaihen/ns2jb-main/ns2jb-main/engine/switch2/ro/runtime.cjs');
const relative='switch2/ro/runtime.cjs';

async function fixture({uncertainAt=-1}={}){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'moader-seedsow-activation-'));
 const directory=path.join(root,'ro');fs.mkdirSync(directory);
 const reportFile=path.join(root,'report.json'),launchFile=path.join(root,'launch.json');
 const stock=Buffer.alloc(8);stock.writeBigUInt64LE(168n);
 fs.writeFileSync(path.join(directory,'seedsow-sm-proof.json'),JSON.stringify({report:reportFile,program:{result:0,hex:stock.toString('hex')}}));
 fs.writeFileSync(launchFile,JSON.stringify({session:'seedsow',report:reportFile,
  imageAccepted:true,restored:true,flags:0x10,pid:'211'}));
 const source=transforms.transform(relative,fs.readFileSync(sourceFile,'utf8'));
 assert.equal(source.sha256,pins[relative]);
 const calls=[],memory={reportFile,info:{session:'seedsow'},request:async request=>{
  const at=calls.length;calls.push(request);
  if(at===uncertainAt)throw Object.assign(Error('synthetic uncertain native reply'),
   {nativeRequestUncertain:true});
  if(request.command==='root-native-info')return{worker:{joined:true},services:[
   {name:'pm:shell',handle:25},{name:'pm:dmnt',handle:31}]};
  if(request.command==='root-native-ipc'){
   const pid=Buffer.from(request.hex,'hex').readBigUInt64LE().toString();
   assert.notEqual(pid,'168','retired stock RO must never be sent to PM');
   if(request.id===7){const program=Buffer.alloc(8);program.writeBigUInt64LE(0x0400000000000037n);
    return{result:0,hex:program.toString('hex')};}
   assert.equal(request.id,1);assert.equal(request.handle,25);assert.equal(pid,'210');
   return{result:0};
  }
  if(request.command==='root-native-worker-prepare'){
   assert.equal(request.kind,'pm-start');assert.equal(request.handle,31);
   assert.equal(request.pid,211);return{prepared:true};
  }
  if(request.command==='root-native-worker-release')return{result:0};
  if(request.command==='root-native-worker-values')return{values:['0','0'],sentinel:'18446744073709551615'};
  if(request.command==='root-native-worker-settle')return{responses:[{result:0}],values:['0','0']};
  throw Error('Unexpected synthetic command: '+request.command);
 }};
 const loaded=new Module(sourceFile,module);loaded.filename=sourceFile;
 loaded.require=name=>{
  if(name==='node:fs/promises')return fsp;
  if(name==='node:path')return path;
  if(name==='node:crypto')return require('node:crypto');
  if(name==='node:net')return{createServer:()=>{throw Error('network forbidden in activation test');}};
  if(name==='../tools/fs-memory.cjs')return{connect:async()=>memory,
   encodeU64:value=>{const bytes=Buffer.alloc(8);bytes.writeBigUInt64LE(value);return bytes;}};
  if(name==='./layout.cjs')return{directory,check:(ok,message)=>{if(!ok)throw Error(message);},
   ptr:()=>{throw Error('unused pointer');},validate:()=>{throw Error('unused validate');}};
  if(name==='./probe.cjs')return{main:()=>{throw Error('unused probe');}};
  throw Error('Unexpected activation dependency: '+name);
 };
 loaded._compile(source.source,sourceFile);
 return{root,directory,launchFile,memory,calls,activate:loaded.exports.activate};
}

test('matched HEN activation verifies and retires diagnostic RO before worker start',async()=>{
 const f=await fixture(),old={...process.env};
 try{
  process.env.NS2_PM_START_DUMP='0';process.env.NS2_EXCEPTION_CHECK='0';
  process.env.NS2_STOCK_TERMINATE='pre';
  const result=await f.activate(f.launchFile,f.memory,210);
  assert.equal(result.originalPid,'210');assert.equal(result.pid,'211');
  assert.deepEqual(f.calls.slice(0,5).map(row=>[row.command,row.id||null,
   row.hex?Buffer.from(row.hex,'hex').readBigUInt64LE().toString():null]),[
    ['root-native-info',null,null],['root-native-ipc',7,'211'],
    ['root-native-ipc',7,'210'],['root-native-ipc',1,'210'],
    ['root-native-worker-prepare',null,null]]);
  const proof=JSON.parse(fs.readFileSync(path.join(f.directory,'seedsow-activation.json')));
  assert.equal(proof.originalPid,'210');assert.equal(proof.preTermination.result,0);
  assert.equal(proof.start.result,0);
 }finally{process.env=old;fs.rmSync(f.root,{recursive:true,force:true});}
});

test('uncertain diagnostic PM reply stops before termination or start',async()=>{
 const f=await fixture({uncertainAt:2}),old={...process.env};
 try{
  process.env.NS2_PM_START_DUMP='0';process.env.NS2_EXCEPTION_CHECK='0';
  process.env.NS2_STOCK_TERMINATE='pre';
  await assert.rejects(()=>f.activate(f.launchFile,f.memory,210),/synthetic uncertain native reply/);
  assert.equal(f.calls.length,3);
  const proof=JSON.parse(fs.readFileSync(path.join(f.directory,'seedsow-activation.json')));
  assert.equal(proof.originalPid,'210');assert.equal(proof.termination,undefined);
  assert.equal(proof.start,undefined);
 }finally{process.env=old;fs.rmSync(f.root,{recursive:true,force:true});}
});
