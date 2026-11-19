// SPDX-License-Identifier: GPL-2.0-only
'use strict';
// Exercise the pinned keepMapped completion branch with synthetic ldr:ro.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),fsp=require('node:fs/promises'),path=require('node:path');
const os=require('node:os'),Module=require('node:module');
const transforms=require('./seedsow-source-transforms.cjs');
const pins=require('./seedsow-source-loader.cjs').TRANSFORMED;
const relative='switch2/ro/nro.cjs';
const file=path.resolve(__dirname,'../research/sources/seedsow-fs-kaihen/ns2jb-main/ns2jb-main/engine',relative);

test('completed HEN bootstrap retains its initialized RO session for same-owner inspection',async()=>{
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'moader-seedsow-ro-lease-'));
 const previous=globalThis.__moaderSeedsowHenRoLease;
 const oldSettle=process.env.NS2_RO_REGISTER_SETTLE_MS;
 let session,closed=false;
 try{
  delete globalThis.__moaderSeedsowHenRoLease;
  process.env.NS2_RO_REGISTER_SETTLE_MS='0';
  class Session{
   constructor(label,bytes){this.label=label;this.bytes=bytes;this.base=0x100000;
    this.handles=new Set();this.rpc={query:async address=>({result:0,
      permissions:[5,1,3,3][[0,0x10000,0x11000,0x12000].indexOf(address-0x200000)]})};session=this;}
   async initialize(){}
   async write(){}
   async openService(name){assert.equal(name,'ldr:ro');this.handles.add(10);return 10;}
   async ipc(_label,handle,id){assert.equal(handle,10);
    if(id===4)return{result:0};
    assert.equal(id,0);const data=Buffer.alloc(8);data.writeBigUInt64LE(0x200000n);
    return{result:0,data};}
   async close(){closed=true;}
   async cleanup(){closed=true;}
  }
  const transformed=transforms.transform(relative,fs.readFileSync(file,'utf8'));
  assert.equal(transformed.sha256,pins[relative]);
  const loaded=new Module(file,module);loaded.filename=file;
  loaded.require=name=>{
   if(name==='node:fs/promises')return fsp;
   if(name==='../../shared/paths.cjs')return{runPath:(_a,_b,name)=>path.join(directory,name)};
   if(name==='../app/session.cjs')return{Session,live:{session:'seedsow'},
    check:(ok,message)=>{if(!ok)throw Error(message);}};
   throw Error('Unexpected NRO dependency: '+name);
  };
  loaded._compile(transformed.source,file);
  const image=Buffer.alloc(0x12000);image.write('NRO0',0x10);
  for(const [at,value]of [[0x18,0x12000],[0x24,0x10000],[0x28,0x10000],
   [0x2c,0x1000],[0x30,0x11000],[0x34,0x1000],[0x38,0x1000]])image.writeUInt32LE(value,at);
  const result=await loaded.exports.main({label:'hen-startup',filename:'hen-startup.json',
   image,keepMapped:true,execute:async()=>0});
  assert.equal(result.complete,true);assert.equal(closed,false);
  const lease=globalThis.__moaderSeedsowHenRoLease;
  assert.equal(lease.session,session);assert.equal(lease.handle,10);
  assert.equal(lease.file,result.file);assert.equal(lease.sessionId,'seedsow');
  assert(session.handles.has(10));
  const receipt=JSON.parse(fs.readFileSync(result.file));
  assert.equal(receipt.complete,true);assert.equal(receipt.keepMapped,true);
  assert.deepEqual(receipt.inspectionLease,{handle:10,label:'hen-startup'});
 }finally{
  if(previous===undefined)delete globalThis.__moaderSeedsowHenRoLease;
  else globalThis.__moaderSeedsowHenRoLease=previous;
  if(oldSettle===undefined)delete process.env.NS2_RO_REGISTER_SETTLE_MS;
  else process.env.NS2_RO_REGISTER_SETTLE_MS=oldSettle;
  fs.rmSync(directory,{recursive:true,force:true});
 }
});
