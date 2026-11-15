#!/usr/bin/env node
'use strict';
// Import the actual pinned Civ7 consumer graph in an isolated synthetic run.
// This verifies the private module/transport boundary without any native call.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
let forbidden=0;const deny=()=>{forbidden++;throw Error('Host import test cannot connect or start a process');};
const net=require('node:net'),http=require('node:http'),cp=require('node:child_process');
net.createConnection=net.connect=net.createServer=http.request=http.createServer=deny;
for(const name of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[name]=deny;
const hosts=require('./host-session.cjs'),transport=require('./host-transport.cjs');
const ROOT=path.resolve(__dirname,'..'),config=JSON.parse(fs.readFileSync(path.join(ROOT,'config/civ7-provider.json')));
const work=fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(),'moader-host-import-'))),kit=path.join(work,'kit'),run=path.join(kit,'runs/switch2-test'),session=path.basename(run);
function put(root,name,data){const f=path.join(root,name);fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,Buffer.isBuffer(data)||typeof data==='string'?data:JSON.stringify(data));}
(async()=>{try{
 put(kit,'config/civ7-provider.json',config);
 for(const name of Object.keys(config.files)){
  const bytes=fs.readFileSync(path.join(ROOT,config.sourceDirectory,name));put(path.join(kit,config.sourceDirectory),name,bytes);
  if(/^engine\/(?:lib|shared|switch2)\//.test(name))put(run,name.slice(7),bytes);
 }
 const replacements={'shared/target.json':'target.json','shared/sdk-functions.json':'sdk-functions.json','shared/sdk-layout.json':'sdk-layout.json'};
 for(const name of ['thread-code.json','event-code.json','timing-code.json','hash-code.json','sdk-proxy-code.json','thread-scheduling-reference.json','compat-affinity-reference.json'])replacements['switch2/app/'+name]='app/'+name;
 for(const [target,source]of Object.entries(replacements))put(run,target,fs.readFileSync(path.join(ROOT,config.sourceDirectory,'targets/civ7',source)));
 const report='switch2/memory/report.json',proof='switch2/process/proof.json',current={verified:true,restored:true,session,file:proof,cleanup:{deferred:true},retainedEndpoint:{ready:true,session}};
 put(run,'result.json',{platform:2,target:'civ7',sourceSha256:config.sourceSha256,verified:true,rpc:{mode:'held',state:'listening'},fs:current});
 put(run,'connection.json',{port:39380,token:'0'.repeat(64),addresses:{platform:2,target:'civ7',session,sdk:0x4100000000,scratch:0x4200000000}});
 put(run,'switch2/fs-execution-current.json',current);put(run,proof,{verified:true,session,report,identity:{pid:'8',programId:'0400000000000000'},mapping:{memory:{address:'4294967296'}},notifier:{object:0x100000010},trials:[{verified:true,restored:true},{verified:true,restored:true}]});
 put(run,'switch2/current-acquisition.json',{report});put(run,report,{session,retainedMemory:{ready:true,zeroCountVerified:true,pending:false}});
 put(run,'switch2/ro/current-execution.json',{verified:true,session,report,pid:'9'});
 const context=hosts.qualifyRun(run,'civ7-s8n',kit),state=transport.install(context);
 const rpc=hosts.load(context,'shared/rpc.cjs');assert.equal(rpc.a.session,session);assert.equal(typeof rpc.callAddress6,'function');assert.equal(state.error,null);
 const required={
  'switch2/app/session.cjs':'Session','switch2/process/client.cjs':'Client','switch2/tools/fs-memory.cjs':'connect',
  'switch2/app/domain.cjs':'send','switch2/app/filesystem.cjs':'openFile','switch2/ro/package.cjs':'dispatch',
  'switch2/ro/runtime.cjs':'handoff','switch2/ro/nro.cjs':'main','switch2/ro/start-hen.cjs':'main',
 };
 for(const [name,key]of Object.entries(required))assert.equal(typeof hosts.load(context,name)[key],'function',name+' export');
 const launchFile=path.join(run,'switch2/ro/launch.cjs'),before=fs.readFileSync(launchFile);
 const launch=hosts.compile(context,'switch2/ro/launch.cjs','\nmodule.exports.moaderLaunchMain=main;\n');assert.equal(typeof launch.moaderLaunchMain,'function');assert(fs.readFileSync(launchFile).equals(before));
 assert.equal(hosts.load(context,'switch2/tools/fs-memory.cjs'),hosts.load(context,'switch2/tools/fs-memory.cjs'));
 assert.equal(hosts.load(context,'switch2/app/session.cjs').live,rpc.a);
 assert.equal(hosts.load(context,'switch2/ro/sdk-thread.cjs').pthreadHandle.tagMask,7);
 for(const name of Object.keys(config.runFiles))assert.equal(require.cache[path.join(run,name)],undefined,'run module leaked into global cache');
 assert.equal(forbidden,0);console.log('PASS: actual Civ7 Session/process/SD/HEN graph, authenticated transport injection, private cache and checked launch exposure; zero native/network/process IO');
 put(run,'switch2/current-acquisition.json',{report,socketPath:path.join(run,'memory.sock')});
 put(run,'switch2/ro/root/current-root.json',{rootHandle:7});
 const endpoint=hosts.load(context,'switch2/tools/endpoint.cjs'),originalRequest=endpoint.request;
 const {Session}=hosts.load(context,'switch2/app/session.cjs'),originalInitialize=Session.prototype.initialize;
 const sentinel=Error('Selected Civ7 endpoint sentinel');let requests=0,initializations=0;
 endpoint.request=async(socketPath,command)=>{
  requests++;assert.equal(socketPath,path.join(run,'memory.sock'));assert.deepEqual(command,{command:'root-native-info'});throw sentinel;
 };
 Session.prototype.initialize=async function(...args){initializations++;return originalInitialize.apply(this,args);};
 try{
  const {attachGame}=require('../runtime/modloader/lib/game-files/session.cjs');
  await assert.rejects(()=>attachGame(run),error=>error===sentinel);
  assert.equal(requests,1);assert.equal(initializations,0);assert.equal(forbidden,0);
 }finally{endpoint.request=originalRequest;Session.prototype.initialize=originalInitialize;}
 console.log('PASS: actual attachGame uses the selected Civ7 endpoint exactly once and stops before Session initialization or any real IO');
}finally{fs.rmSync(work,{recursive:true,force:true});}})().catch(error=>{console.error(error.stack);process.exitCode=1;});
