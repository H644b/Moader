'use strict';
const assert=require('node:assert/strict');
const lazy=require('../../lib/lazy-context.cjs');
const native={sdk:{original:Object.fromEntries(['fileRead','fileSize','fileRelease','filesystemRelease','storageRelease','directoryRead','directoryCount','directoryRelease'].map((name,i)=>[name,0x1000+i*16]))},namespace:{original:{open:0x2000,directory:0x2010,type:0x2020}},originalSize:0x3000,lazy:{original:{current:0x4000,program:0x4010,data:0x4020}}};
for(const moader of [false,true])for(const logging of [false,true])for(const trace of [false,true]){
 const plan=lazy.plan({games:[],patches:[]},'switch2-host');plan.moader=moader;
 const text='Moader filesystem runtime\nActivation complete: 0\n';
 if(logging)plan.diagnostics={text,size:Buffer.byteLength(text),readyOffset:text.length-2,...(trace?{tracePath:'/moader/runtime/runtime.trace'}:{})};
 const source={filesystem:{object:0x6100001000n},registry:0x6100003000n,...(logging?{logFile:{object:0x6100002000n,write:0x40001050a0n,flush:0x40001053d0n,setSize:0x4000105410n}}:{}),...(logging&&trace?{traceFile:{object:0x6100004000n}}:{})};
 const layout=lazy.layout(plan),address=0x6000000000n,bytes=lazy.buildContext(plan,source,0x4000000000n,address,native);
 assert.equal(layout.gamesOffset,layout.lazyOffset+264);
 const snapshot=bytes.readBigUInt64LE(layout.lazyOffset+256);
 assert.equal(snapshot,moader&&logging?address+BigInt(layout.snapshotOffset):0n);
 if(snapshot){assert(layout.snapshotOffset>=layout.traceOffset+(trace?49152:0));assert(layout.snapshotOffset+65704<=bytes.length);assert(bytes.subarray(layout.snapshotOffset,layout.snapshotOffset+65704).every(byte=>byte===0));}
 if(logging){assert.equal(bytes.readBigUInt64LE(layout.lazyOffset+112),BigInt(Buffer.byteLength(text)));assert.equal(bytes.subarray(layout.logOffset,layout.logOffset+text.length).toString(),text);assert.equal(bytes.readBigUInt64LE(layout.lazyOffset+120),BigInt(1024*1024-(trace?40960:0)));}
}
console.log('Moader snapshot pointer/config/scratch boundaries, normal/trace/no-log contexts and variable-length report: passed');
