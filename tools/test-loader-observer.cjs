#!/usr/bin/env node
'use strict';
// These fixtures execute the production observer with a fake RO transport.
const assert = require('node:assert/strict');
const {openObserver, readReply} = require('./loader-observer.cjs');
const net = require('node:net'), http = require('node:http');
let connections = 0, passed = 0;
net.createConnection = http.request = () => { connections++; throw Error('Observer tests cannot contact a console'); };
const BASE = 0x6100000000, FS = 0x0400000000000000n;
const {response}=require('../research/sources/ns2jb/engine/shared/ipc.cjs');
function reply(pid, payload) {
  const data = Buffer.alloc(208);
  data.writeBigUInt64LE(BigInt(pid), 0); data.writeBigUInt64LE(FS, 8);
  data.writeBigUInt64LE(0x6200000000n, 24); data.writeBigUInt64LE(BigInt(payload.length), 48);
  payload.copy(data, 56);
  // Use the actual pinned response parser and RO's fixed frame layout.
  const raw=Buffer.alloc(256);raw.writeUInt32LE((0x80000000|Math.ceil((32+56+payload.length)/4))>>>0,4);
  raw.writeUInt32LE(1,8);raw.writeBigUInt64LE(209n,12);raw.write('SFCO',32);data.copy(raw,48);
  return response(raw);
}
function fixture(options = {}) {
  const events = [], state = {error:null}, record = {}, snapshots = [];
  const bytes = Buffer.alloc(0x4000, 0x47);
  const client = {
    record:{session:'synthetic',report:'synthetic-report'}, pid:'209', file:'synthetic-protocol.json', pending:false,
    async fsProcess() { events.push('export'); return 123; },
    async info(handle) { assert.equal(handle,123); events.push('identify'); return {result:0,pid:'8',programId:FS.toString(16).padStart(16,'0'),...options.identity}; },
    async request(id, handle, values) {
      assert.equal(id,0x524f0011); assert.equal(handle,123); assert.equal(values[0],8n);
      const at=Number(values[1]), length=Number(values[2]);
      events.push({read:at,length});
      assert(length<=128 && at%4096+length<=4096);
      if(options.timeout) { client.pending=true; throw Error('Synthetic native request timed out'); }
      const r=reply(8,bytes.subarray(at-BASE,at-BASE+length));
      if(options.corrupt) options.corrupt(r);
      return r;
    },
    async close() { events.push('close'); if(options.closeFailure) throw Error('Synthetic close failure'); },
  };
  if(options.roPid) client.pid=options.roPid;
  const args={run:'synthetic-run',memory:{info:{session:'synthetic'},reportFile:'synthetic-report'},
    execution:{pid:'209'},state,record,save:async()=>snapshots.push(structuredClone(record))};
  return {events,state,record,snapshots,client,bytes,args,open:()=>openObserver(args,{openClient:async()=>client})};
}
async function test(name, action) { await action(); passed++; console.log('ok '+name); }
(async()=>{
  await test('bounded process reads split at page boundaries and close before retirement',async()=>{
    const f=fixture(), o=await f.open();
    assert.deepEqual(await o.read(BASE+4090,136),f.bytes.subarray(4090,4226));
    assert.deepEqual(f.events.filter(x=>x.read),[{read:BASE+4090,length:6},{read:BASE+4096,length:128},{read:BASE+4224,length:2}]);
    await o.close(); await o.close(); assert.equal(f.events.filter(x=>x==='close').length,1);
    assert.equal(f.record.closed,true); assert.equal(f.record.resourcesRetained,false);
    await assert.rejects(()=>o.read(BASE,8),/Invalid/);
  });
  await test('every process response binds FS identity and alias release',async()=>{
    const good=reply(8,Buffer.alloc(8)); assert.equal(readReply(good,8n,8).length,8);
    for(const [offset,value] of [[0,9n],[8,FS+1n],[16,1n],[32,1n],[40,1n],[48,7n]]){
      const bad={result:0,data:Buffer.from(good.data)};bad.data.writeBigUInt64LE(value,offset);
      assert.throws(()=>readReply(bad,8n,8),/identity|mapping|cleanup/);
    }
    for(const bad of [{result:1,data:good.data},{result:0,data:good.data.subarray(1)},{result:0,data:Buffer.concat([good.data,Buffer.alloc(1)])},{result:0,data:good.data.subarray(0,64)}])
      assert.throws(()=>readReply(bad,8n,8),/failed|truncated/);
    const excess={result:0,data:Buffer.from(good.data)};excess.data[64]=1;
    assert.throws(()=>readReply(excess,8n,8),/unexpected data/);
  });
  await test('pending or malformed read forbids further reads and all cleanup',async()=>{
    for(const options of [{timeout:true},{corrupt:r=>r.data.writeBigUInt64LE(1n,40)}]){
      const f=fixture(options),o=await f.open();await assert.rejects(()=>o.read(BASE,8));
      assert.equal(o.uncertain,true);assert.equal(f.state.error.nativeRequestUncertain,true);
      const count=f.events.length;await assert.rejects(()=>o.read(BASE,8));await o.close();
      assert.equal(f.events.length,count);assert.equal(f.record.resourcesRetained,true);
    }
  });
  await test('wrong diagnostic RO and wrong exported FS stop before observing',async()=>{
    for(const options of [{roPid:'210'},{identity:{programId:'0400000000000037'}},{identity:{result:1}},{identity:{pid:'0'}}]){
      const f=fixture(options);await assert.rejects(f.open);assert(f.state.error);
      assert(!f.events.some(x=>x.read));assert(!f.events.includes('close'));assert.equal(f.record.resourcesRetained,true);
    }
  });
  await test('Loader preflight reads actual binding without trusting display names',async()=>{
    const f=fixture(), raw=Buffer.alloc(0x88),row={address:BASE,domain:BASE+0x1000,metadata:BASE+0x2000,wait:44,receive:44,request:BASE+0x3000};
    raw.writeBigUInt64LE(BigInt(BASE+0x200),0);raw.writeUInt32LE(44,0x30);raw.writeUInt32LE(44,0x48);
    raw.writeBigUInt64LE(BigInt(row.request),0x60);raw.writeBigUInt64LE(0x100n,0x68);
    raw.writeBigUInt64LE(BigInt(row.domain),0x70);raw.writeBigUInt64LE(BigInt(row.metadata),0x78);
    row.raw=raw.toString('hex');raw.copy(f.bytes);const o=await f.open();await o.qualify(row);
    assert.equal(f.record.preflightReadVerified,true);f.bytes.writeUInt32LE(45,0x48);
    await assert.rejects(()=>o.qualify(row),/differs/);await o.close();assert(!f.events.includes('close'));
  });
  await test('close failure retains safety latch and cannot be retried',async()=>{
    const f=fixture({closeFailure:true}),o=await f.open();await assert.rejects(()=>o.close(),/close failure/);
    await o.close();assert.equal(f.events.filter(x=>x==='close').length,1);assert.equal(f.record.unresolved,true);
  });
  await test('external transport uncertainty blocks the independent observer too',async()=>{
    const f=fixture(),o=await f.open();f.state.error=Error('Synthetic retained transport lost');
    const count=f.events.length;await assert.rejects(()=>o.read(BASE,8),/lost/);await o.close();assert.equal(f.events.length,count);
  });
  assert.equal(connections,0);console.log(passed+' Loader observer checks passed; no console access');
})().catch(error=>{console.error(error);process.exitCode=1;});
