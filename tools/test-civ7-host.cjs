#!/usr/bin/env node
'use strict';
// Independent actual-provider regression checks. Only temporary synthetic runs
// are inspected; all network/listeners and native/child execution are forbidden.
const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),Module=require('node:module');
const ROOT=path.resolve(__dirname,'..'),source=path.resolve(process.argv[2]||path.join(ROOT,'research/sources/civ7-fs-kaihen'));
let forbidden=0;
function deny(){forbidden++;throw Error('Independent Civ7 check forbids network and process execution');}
const net=require('node:net'),http=require('node:http'),https=require('node:https'),tls=require('node:tls'),dgram=require('node:dgram'),cp=require('node:child_process');
net.Socket.prototype.connect=net.Server.prototype.listen=deny;
http.request=http.get=https.request=https.get=tls.connect=deny;
dgram.Socket.prototype.bind=dgram.Socket.prototype.connect=dgram.Socket.prototype.send=deny;
for(const name of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[name]=deny;
globalThis.fetch=deny;
const originalLoad=Module._load,imports=[];
Module._load=function(name,parent,main){
 const file=Module._resolveFilename(name,parent,main);
 if(typeof file==='string'&&file.includes('/switch2-test'))imports.push(file);
 if(typeof file==='string'&&/\/(?:rpc|endpoint|fs-memory|pipeline)\.cjs$/.test(file))deny();
 return originalLoad.apply(this,arguments);
};
const providerFile=path.join(ROOT,'tools/host-session.cjs');
const work=fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(),'moader-civ7-provider-')));
const config=JSON.parse(fs.readFileSync(path.join(ROOT,'config/civ7-provider.json')));
const APP=['thread-code.json','event-code.json','timing-code.json','hash-code.json','sdk-proxy-code.json','thread-scheduling-reference.json','compat-affinity-reference.json'];
function put(root,name,value){const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,Buffer.isBuffer(value)||typeof value==='string'?value:JSON.stringify(value));return file;}
function provider(){delete require.cache[providerFile];return require(providerFile);}
let serial=0,passed=0;const failures=[];
function fixture(){
 const kit=path.join(work,'kit-'+(++serial)),run=path.join(kit,'runs/switch2-test'+serial),session=path.basename(run),report='switch2/memory/synthetic-report.json';
 put(kit,'config/civ7-provider.json',config);
 for(const name of Object.keys(config.files))put(path.join(kit,'research/sources/civ7-fs-kaihen'),name,fs.readFileSync(path.join(source,name)));
 // Reproduce pinned run materialization independently, not from runFiles.
 for(const name of Object.keys(config.files))if(/^engine\/(?:lib|shared|switch2)\//.test(name))put(run,name.slice(7),fs.readFileSync(path.join(source,name)));
 for(const name of ['target.json','sdk-functions.json','sdk-layout.json'])put(run,'shared/'+name,fs.readFileSync(path.join(source,'targets/civ7',name)));
 for(const name of APP)put(run,'switch2/app/'+name,fs.readFileSync(path.join(source,'targets/civ7/app',name)));
 const proof={session,report,verified:true,identity:{pid:'8',programId:'0400000000000000'},mapping:{memory:{address:'4294967296'}},notifier:{object:4294967312,backend:4294967328,handle:42},trials:[{verified:true,restored:true},{verified:true,restored:true}]};
 const current={verified:true,restored:true,session,pid:'8',programId:'0400000000000000',file:'switch2/process/synthetic-fs-proof.json',cleanup:{deferred:true},retainedEndpoint:{ready:true,session,socketPath:path.join(run,'memory.sock')}};
 const result={platform:2,target:'civ7',sourceSha256:config.sourceSha256,verified:true,rpc:{mode:'held',state:'listening'},fs:current};
 const connection={port:39380,token:'0'.repeat(64),addresses:{platform:2,target:'civ7',session,sdk:4294967296}};
 const acquisition={report,socketPath:current.retainedEndpoint.socketPath};
 const reportData={session,retainedMemory:{ready:true,zeroCountVerified:true,pending:false}};
 const execution={verified:true,session,report,pid:'9',programId:'0400000000000037',payloadSha256:'d590cbfc5d9152c24d5a36f083f8470a2f3164be6b78130129d44132b939701a'};
 const records={'result.json':result,'connection.json':connection,'switch2/fs-execution-current.json':current,'switch2/process/synthetic-fs-proof.json':proof,'switch2/current-acquisition.json':acquisition,[report]:reportData,'switch2/ro/current-execution.json':execution};
 function save(){for(const [name,data]of Object.entries(records))put(run,name,data);}
 save();return{kit,run,session,proof,current,result,connection,acquisition,reportData,execution,records,save};
}
async function test(name,action){
 try{await action();passed++;console.log('PASS: '+name);}catch(error){failures.push({name,message:error.message});console.error('FAIL: '+name+' — '+error.message);}
}
function qualify(f,p=provider()){return p.qualifyRun(f.run,'civ7-s8n',f.kit);}
async function rejectsMutation(name,mutate){await test(name,()=>{const f=fixture();mutate(f);f.save();const before=imports.length;assert.throws(()=>qualify(f));assert.equal(imports.length,before,'Rejected fixture imported provider code');});}
(async()=>{
 await test('actual pinned source and synthetic held FS qualify without importing run code',()=>{const f=fixture(),p=provider(),before=imports.length;const context=qualify(f,p);assert.equal(context.target,'civ7');assert.equal(context.run,f.run);assert.equal(imports.length,before);});
 await test('prepared host-profile binding is exact and legacy-only when absent',()=>{
  const f=fixture(),p=provider(),profile=p.resolveProfile('civ7-s8n',f.kit),binding=p.binding(profile);
  assert.equal(p.preparedProfile(binding,f.kit).id,'civ7-s8n');assert.equal(p.preparedProfile(undefined,f.kit).id,'smilebasic-kurumuz');
  for(const key of ['id','revision','sourceTreeSha256'])assert.throws(()=>p.preparedProfile({...binding,[key]:'incorrect'},f.kit));
  assert.throws(()=>p.preparedProfile({...binding,consoleTested:true},f.kit));
 });
 for(const [name,mutate]of [
  ['wrong prepared profile',f=>{const p=provider();assert.throws(()=>p.qualifyRun(f.run,'smilebasic-kurumuz',f.kit));f.result.verified=false;}],
  ['closed session',f=>f.result.rpc.state='closed'],['completed session',f=>f.result.completed='synthetic'],
  ['unverified session',f=>f.result.verified=false],['RO frontend',f=>f.result.ro={verified:true}],
  ['app-only session',f=>f.result.app={verified:true}],['wrong source digest',f=>f.result.sourceSha256='f'.repeat(64)],
  ['wrong platform',f=>f.result.platform=1],['missing token',f=>delete f.connection.token],
  ['malformed token',f=>f.connection.token='fixture-token'],['connection target mismatch',f=>f.connection.addresses.target='smilebasic'],
  ['connection session mismatch',f=>f.connection.addresses.session='switch2-Other'],
  ['proof session mismatch',f=>f.proof.session='switch2-Other'],['proof not restored',f=>f.proof.trials[1].restored=false],
  ['proof pending',f=>f.proof.trials[1].pending='restore'],['wrong FS program',f=>f.proof.identity.programId='0100c3601518c000'],
  ['missing retained endpoint',f=>f.current.retainedEndpoint.ready=false],
  ['retained cleanup released',f=>f.current.cleanup.deferred=false],['retained report pending',f=>f.reportData.retainedMemory.pending=true],
  ['acquisition report mismatch',f=>f.acquisition.report='switch2/other.json'],
  ['retired diagnostic RO',f=>{f.execution.verified=false;f.execution.retiredForKaihen=true;}],
  ['diagnostic report mismatch',f=>f.execution.report='switch2/other.json'],
  ['escaped proof path',f=>f.current.file='../outside-proof.json']
 ])await rejectsMutation(name,mutate);
 await test('source SDK corruption rejects before any run import',()=>{const f=fixture();put(path.join(f.kit,'research/sources/civ7-fs-kaihen'),'targets/civ7/sdk-functions.json','{}');assert.throws(()=>qualify(f),/source changed/);});
 await test('materialized SDK corruption rejects before import',()=>{const f=fixture();put(f.run,'shared/sdk-functions.json','{}');assert.throws(()=>qualify(f),/materialized/);});
 await test('extra executable rejects before import',()=>{const f=fixture();put(f.run,'shared/unqualified.cjs','throw Error("never execute")');assert.throws(()=>qualify(f),/Unqualified executable/);});
 await test('symlinked proof rejects before import',()=>{const f=fixture(),file=path.join(f.run,f.current.file),copy=path.join(f.kit,'symlink-proof.json');fs.renameSync(file,copy);fs.symlinkSync(copy,file);assert.throws(()=>qualify(f),/symlink/);});
 await test('source/profile consistency survives forged descriptor run hash',()=>{
  const f=fixture(),modified={...config,runFiles:{...config.runFiles}};modified.runFiles['shared/ipc.cjs']='0'.repeat(64);put(f.kit,'config/civ7-provider.json',modified);
  assert.throws(()=>qualify(f));
 });
 await test('coordinated descriptor and run-byte corruption cannot bypass pinned sources',()=>{
  const f=fixture(),p=provider(),bytes=Buffer.from('module.exports={unqualified:true};\n'),modified={...config,runFiles:{...config.runFiles}};
  modified.runFiles['shared/ipc.cjs']=p.sha(bytes);put(f.kit,'config/civ7-provider.json',modified);put(f.run,'shared/ipc.cjs',bytes);
  assert.throws(()=>qualify(f,p),'Source tree identity must bind the materialized run map');
 });
 await test('symlinked ancestor above selected run root rejects',()=>{
  const f=fixture(),alias=path.join(work,'alias-'+serial);fs.symlinkSync(path.dirname(f.run),alias,'dir');
  assert.throws(()=>provider().qualifyRun(path.join(alias,f.session),'civ7-s8n',f.kit),/symlink/);
 });
 await test('SDK callback path callAddress6 shares stop-on-uncertainty latch',async()=>{
  const {guardTransports}=require('./ready-kaihen.cjs');let calls=0;const timeout=Object.assign(Error('timed out'),{code:'ETIMEDOUT'});
  const rpc={callAddress6:async()=>{calls++;throw timeout;},write:async()=>{calls++;return 0;}},endpoint={request:async()=>{calls++;return 0;}};
  const state=guardTransports({rpc,endpoint});await assert.rejects(()=>rpc.callAddress6(0,[]));assert.equal(state.error,timeout);assert.equal(timeout.nativeRequestUncertain,true);
  await assert.rejects(()=>rpc.write(0,'00'));await assert.rejects(()=>endpoint.request({}));assert.equal(calls,1);
 });
 await test('changed transitive module cannot execute after qualification',()=>{
  const f=fixture(),p=provider(),context=qualify(f,p);globalThis.moaderUnqualifiedDependency=false;
  put(f.run,'shared/ipc.cjs','globalThis.moaderUnqualifiedDependency=true; module.exports={};\n');
  let rejected=false;try{p.load(context,'shared/domain.cjs');}catch{rejected=true;}
  assert.equal(globalThis.moaderUnqualifiedDependency,false,'Unqualified transitive dependency executed');
  assert.equal(rejected,true,'Changed dependency was not rejected');
 });
 await test('closed result cannot be loaded through an old qualified context',()=>{
  const f=fixture(),p=provider(),context=qualify(f,p);f.result.rpc.state='closed';f.save();assert.throws(()=>p.load(context,'shared/ipc.cjs'));
 });
 await test('foreign preloaded module-cache entry cannot replace qualified Civ7 code',()=>{
  const f=fixture(),p=provider(),context=qualify(f,p),file=path.join(f.run,'shared/ipc.cjs');
  require.cache[file]={id:file,filename:file,loaded:true,exports:{unqualified:true}};
  try{assert.throws(()=>p.load(context,'shared/ipc.cjs'),'A matching on-disk hash must not authorize arbitrary cached exports');}
  finally{delete require.cache[file];}
 });
 function legacyContext(p){
  const f=fixture(),profile=p.resolveProfile('smilebasic-kurumuz',ROOT);
  for(const name of ['lib','shared','switch2'])fs.rmSync(path.join(f.run,name),{recursive:true,force:true});
  for(const name of Object.keys(profile.runFiles))put(f.run,name,fs.readFileSync(path.join(profile.sourceDirectory,name)));
  f.result.target='smilebasic';f.connection.addresses.target='smilebasic';f.save();
  return p.qualifyRun(f.run,'smilebasic-kurumuz',ROOT);
 }
 await test('Civ7 then legacy imports cannot mix target environment or module caches',()=>{
  const f=fixture(),p=provider(),civ7=qualify(f,p),legacy=legacyContext(p);
  p.load(civ7,'shared/ipc.cjs');assert.throws(()=>p.load(legacy,'shared/ipc.cjs'));
 });
 await test('legacy then Civ7 imports cannot mix target environment or module caches',()=>{
  const f=fixture(),p=provider(),civ7=qualify(f,p),legacy=legacyContext(p);
  p.load(legacy,'shared/ipc.cjs');assert.throws(()=>p.load(civ7,'shared/ipc.cjs'));
 });
 await test('inherited target overrides are removed before importing selected pure module',()=>{
  const f=fixture(),p=provider(),context=qualify(f,p);process.env.NS2_TARGET='smilebasic';process.env.NS2_TARGET_ROOT='/incorrect';process.env.NS2_TARGET_DIR='/incorrect';process.env.NS2_PORT_ROOT='/incorrect';
  p.load(context,'shared/ipc.cjs');assert.equal(process.env.NS2_TARGET,'civ7');assert.equal(process.env.NS2_TARGET_ROOT,f.run);assert.equal(process.env.NS2_RUN_DIR,f.run);assert.equal(process.env.NS2_TARGET_DIR,undefined);assert.equal(process.env.NS2_PORT_ROOT,undefined);
 });
 assert.equal(forbidden,0,'An actual network/process operation was attempted');
 console.log(JSON.stringify({passed,failed:failures.length,forbiddenIO:forbidden,syntheticOnly:true}));
 if(failures.length)process.exitCode=1;
})().catch(error=>{console.error(error.stack);process.exitCode=1;}).finally(()=>fs.rmSync(work,{recursive:true,force:true}));
