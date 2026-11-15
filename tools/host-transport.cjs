'use strict';
// Single-shot, bounded Civ7 host transports. No sockets are opened on import.
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),net=require('node:net'),crypto=require('node:crypto');
const hosts=require('./host-session.cjs'),installed=new WeakMap(),LIMIT=16*1024*1024;
const check=(ok,message)=>{if(!ok)throw Error(message);};
function create(connection,state={error:null},deps={}){
 const web=deps.http||http,sockets=deps.net||net;
 check(Number.isInteger(connection.port)&&connection.port>0&&connection.port<=65535&&/^[a-f0-9]{64}$/.test(connection.token),'Invalid Civ7 authenticated connection');
 const guarded=operation=>async(...args)=>{
  if(state.error)throw state.error;
  try{return await operation(...args);}catch(error){error.nativeRequestUncertain=true;state.error=error;throw error;}
 };
 const decode=bytes=>{const value=JSON.parse(bytes.toString('utf8'));check(value&&typeof value==='object'&&!Array.isArray(value),'Invalid native transport reply');if(value.error)throw Error(String(value.error));return value;};
 const request=guarded(body=>new Promise((resolve,reject)=>{
  let done=false;const finish=(error,value)=>{if(done)return;done=true;error?reject(error):resolve(value);};
  const req=web.request({host:'127.0.0.1',port:connection.port,path:'/',method:'POST',headers:{'content-type':'application/json',authorization:'Bearer '+connection.token}},res=>{
   const parts=[];let size=0,ended=false;
   res.on('data',part=>{const bytes=Buffer.from(part);size+=bytes.length;if(size>LIMIT){finish(Error('Native HTTP reply exceeded its limit'));req.destroy();return;}parts.push(bytes);});
   res.on('error',error=>finish(error));res.on('aborted',()=>finish(Error('Native HTTP reply aborted')));
   res.on('close',()=>{if(!ended)finish(Error('Native HTTP reply ended incompletely'));});
   res.on('end',()=>{ended=true;try{check(res.complete!==false,'Native HTTP reply is incomplete');const value=decode(Buffer.concat(parts));check(res.statusCode===200,'Native HTTP status '+res.statusCode);finish(null,value);}catch(error){finish(error);}});
  });
  req.setTimeout(180000,()=>{finish(Error('Native HTTP request timed out; it was not replayed'));req.destroy();});
  req.on('error',error=>finish(error));req.end(JSON.stringify(body,(_,value)=>typeof value==='bigint'?value.toString():value));
 }));
 const endpointRequest=guarded((socketPath,command,timeoutMs=300000)=>new Promise((resolve,reject)=>{
  check(typeof socketPath==='string'&&socketPath&&!/[\x00-\x1f\x7f]/.test(socketPath),'Invalid memory endpoint');
  const tcp=/^([^:]+):(\d+)$/.exec(socketPath);if(tcp)check(['127.0.0.1','localhost'].includes(tcp[1])&&+tcp[2]>0&&+tcp[2]<=65535,'Memory TCP endpoint must be loopback');
  const socket=sockets.createConnection(tcp?{host:tcp[1],port:+tcp[2]}:socketPath),parts=[];let size=0,ended=false,done=false;
  const finish=(error,value)=>{if(done)return;done=true;error?reject(error):resolve(value);};
  socket.setTimeout(timeoutMs,()=>{finish(Error('Memory request timed out; it was not replayed'));socket.destroy();});
  socket.on('error',error=>finish(error));socket.on('close',()=>{if(!ended)finish(Error('Memory reply disconnected before end'));});
  socket.on('connect',()=>socket.write(JSON.stringify(command,(_,value)=>typeof value==='bigint'?value.toString():value)+'\n'));
  socket.on('data',part=>{const bytes=Buffer.from(part);size+=bytes.length;if(size>LIMIT){finish(Error('Memory reply exceeded its limit'));socket.destroy();return;}parts.push(bytes);});
  socket.on('end',()=>{ended=true;try{finish(null,decode(Buffer.concat(parts)));}catch(error){finish(error);}});
 }));
 const decodeCall=reply=>{check(typeof reply.result==='string'&&/^(?:0x[0-9a-f]+|\d+)$/i.test(reply.result)&&typeof reply.output==='string','Invalid native call reply');return{result:BigInt(reply.result),output:Buffer.from(reply.output,'base64')};};
 const call=(command,key)=>guarded(async(value,args)=>decodeCall(await request({command,[key]:value,args})));
 const api={request,call:call('call','offset'),callAddress:call('call-address','address'),callAddress6:call('call-address-6','address'),
  log:guarded(message=>request({command:'console-log',message:String(message).slice(0,200)})),
  pair:guarded(async calls=>{const reply=await request({command:'pair',calls});check(Array.isArray(reply.results)&&reply.results.length===calls.length&&typeof reply.output==='string','Invalid native pair reply');return{results:reply.results.map(BigInt),output:Buffer.from(reply.output,'base64')};}),
  query:guarded(async address=>{const reply=await request({command:'query',address});const at=Number(reply.address),size=Number(reply.size);check(Number.isSafeInteger(at)&&Number.isSafeInteger(size)&&size>0,'Invalid native mapping reply');return{...reply,result:0,address:at,size};}),
  read:guarded(async(address,length)=>{check(Number.isSafeInteger(address)&&Number.isInteger(length)&&length>=0&&length<=0x4000000,'Invalid memory transfer');const parts=[];for(let at=0;at<length;at+=0x80000){const size=Math.min(0x80000,length-at),reply=await request({command:'read',address:address+at,length:size});check(typeof reply.data==='string','Missing native read bytes');const bytes=Buffer.from(reply.data,'base64');check(bytes.length===size,'Memory read was truncated');parts.push(bytes);}return Buffer.concat(parts);}),
  write:guarded(async(address,bytes)=>{check(Number.isSafeInteger(address)&&Buffer.isBuffer(bytes)&&bytes.length<=0x4000000,'Invalid memory transfer');for(let at=0;at<bytes.length;at+=0x80000)await request({command:'write',address:address+at,data:bytes.subarray(at,at+0x80000).toString('base64')});})};
 return{rpc:api,endpointRequest,state,guarded};
}
function install(context,state={error:null}){
 check(context.profile.id==='civ7-s8n','Bounded transport requires the Civ7 profile');
 const rpc=hosts.load(context,'shared/rpc.cjs'),endpoint=hosts.load(context,'switch2/tools/endpoint.cjs');
 if(installed.has(rpc)){
  const existing=installed.get(rpc);if(existing!==state)Object.defineProperty(state,'error',{get:()=>existing.error,set:value=>{existing.error=value;},configurable:true});return existing;
 }
 const transport=create(hosts.runJson(context.run,'connection.json'),state);Object.assign(rpc,transport.rpc);
 endpoint.request=transport.endpointRequest;
 endpoint.read=transport.guarded(async(socketPath,address,length)=>{
  check(Number.isSafeInteger(address)&&Number.isInteger(length)&&length>0&&length<=0x4000000,'Invalid endpoint memory read');
  const result=await endpoint.request(socketPath,{command:'read',address,length}),file=hosts.safeFile(context.run,path.relative(context.run,result.outputFile).split(path.sep).join('/'));
  const stat=await fs.stat(file);check(stat.size===length,'Endpoint memory file size differs');const bytes=await fs.readFile(file);
  check(result.address===address&&result.length===length&&bytes.length===length&&crypto.createHash('sha256').update(bytes).digest('hex')===result.sha256,'Saved endpoint read differs');return{result,bytes};
 });
 installed.set(rpc,state);return state;
}
module.exports={create,install,LIMIT};
