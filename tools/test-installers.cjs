#!/usr/bin/env node
'use strict';
// Host-only tests. Any accidental endpoint/HTTP connection is a hard failure.
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),sync=require('node:fs'),path=require('node:path'),os=require('node:os'),Module=require('node:module');
const net=require('node:net'),http=require('node:http');
let connections=0;
net.createConnection=http.request=()=>{connections++;throw Error('Host installer tests cannot connect to an endpoint');};
const inputs=require('./install-inputs.cjs'),{ready,guardTransports}=require('./ready-kaihen.cjs'),{verifyRoPackage,qualifyStagedRoReuse,main:kaihenMain}=require('./install-kaihen.cjs');
const root=path.resolve(__dirname,'..'),tests=[];
const originalReadFile=fs.readFile.bind(fs);
let candidateReads=0;
fs.readFile=async(file,...args)=>{
 if(typeof file==='string'&&['vendor','dist'].some(name=>path.resolve(file).startsWith(path.join(root,name)+path.sep))){
  ++candidateReads;throw Error('Installer tests must not read current candidate build outputs');
 }
 return originalReadFile(file,...args);
};
function test(name,action){tests.push({name,action});}
async function writableTree(folder){
 await fs.chmod(folder,0o700);
 for(const entry of await fs.readdir(folder,{withFileTypes:true}))if(entry.isDirectory())await writableTree(path.join(folder,entry.name));
}
async function fixture(action){
 const directory=await fs.mkdtemp(path.join(os.tmpdir(),'moader-install-test-'));
 try{return await action(directory);}finally{await writableTree(directory);await fs.rm(directory,{recursive:true,force:true});}
}
async function shipping(directory){
 const ship=path.join(directory,'ship');
 for(const name of ['ro/main','hen/main','bootstrap.bin','game/main','hbmenu.nro','programs/tool.nro']){
  await fs.mkdir(path.dirname(path.join(ship,name)),{recursive:true});await fs.writeFile(path.join(ship,name),'fixture-'+name);
 }
 const app=path.join(directory,'Moader.nro'),plugin=path.join(directory,'moader.nso');
 await fs.writeFile(app,'fixture-app-v1');await fs.writeFile(plugin,'fixture-plugin-v1');
 return{ship,programs:[{source:app,target:'/switch/Moader.nro'},{source:plugin,target:'/kaihen/plugins/moader.nso'}]};
}
function stubModule(filename,overrides,observeRequire=()=>{}){
 const file=path.join(root,filename),loaded=new Module(file,module),nativeRequire=Module.createRequire(file);
 loaded.filename=file;loaded.paths=Module._nodeModulePaths(path.dirname(file));
 loaded.require=name=>{observeRequire(name);return Object.hasOwn(overrides,name)?overrides[name]:nativeRequire(name);};
 loaded._compile(sync.readFileSync(file,'utf8'),file);return loaded.exports;
}
function fakeSession(fault){
 const events=[];
 class Session{
  constructor(){events.push('construct');}
  async initialize(){
   events.push('initialize');
   // Match the real initialization restore path: guardSession must prevent
   // this finally-write when allocation dispatch becomes uncertain.
   try{await this.call('allocate');}finally{await this.write();}
  }
  async call(name){events.push(name);if(fault===name)throw Error('Native request timed out; it was not replayed');}
  async write(){events.push('restore-scratch');}
  async openService(name){events.push(name);return 12;}
  async ipc(label,handle,id,data,options){
   assert.equal(handle,12);assert.equal(id,4);assert.deepEqual(options,{pid:true,copies:[0xffff8001]});
   await this.call('send');if(fault==='direct-uncertain')throw Object.assign(Error('unknown completion'),{nativeRequestUncertain:true});
   return{result:fault==='result'?0x202:0};
  }
  async cleanup(){events.push('cleanup');}
 }
 return{Session,events};
}
for(const fault of ['allocate','send','direct-uncertain'])test('readiness preserves allocations after '+fault+' uncertainty',async()=>{
 const f=fakeSession(fault);await assert.rejects(()=>ready({...f,argumentsWithPid:()=>Buffer.alloc(8)}));
 assert(!f.events.includes('cleanup'));
 if(fault==='allocate')assert(!f.events.includes('restore-scratch'));
});
test('readiness success and definite service error clean up owned state',async()=>{
 for(const fault of [undefined,'result']){
  const f=fakeSession(fault),operation=ready({...f,argumentsWithPid:()=>Buffer.alloc(8)});
  if(fault)await assert.rejects(()=>operation,/RO Initialize failed/);else assert.deepEqual(await operation,{ready:true});
  assert.equal(f.events.filter(item=>item==='cleanup').length,1);
 }
});
test('one bootstrap transport timeout blocks raw-session scratch restore and retained finally cleanup',async()=>{
 const events=[];
 const rpc={async call(){events.push('allocate');throw Error('Native request timed out; it was not replayed');},async write(){events.push('scratch-restore');}};
 const endpoint={async request(){events.push('close-exported-handle');}};
 const state=guardTransports({rpc,endpoint});
 await assert.rejects(async()=>{try{await rpc.call();}finally{await rpc.write();}},/timed out/);
 await assert.rejects(()=>endpoint.request(),/timed out/);
 assert.deepEqual(events,['allocate']);assert.equal(state.error.nativeRequestUncertain,true);
});
test('retained transport disconnect blocks HTTP calls and later retained requests',async()=>{
 const events=[];
 const rpc={async query(){events.push('query');}};
 const endpoint={async request(){events.push('request');throw Error('socket disconnected');},async read(){events.push('read');}};
 guardTransports({rpc,endpoint});
 await assert.rejects(()=>endpoint.request(),/disconnected/);
 await assert.rejects(()=>rpc.query(),/disconnected/);await assert.rejects(()=>endpoint.read(),/disconnected/);
 assert.deepEqual(events,['request']);
});
test('a definite native failure retains the transport for qualified cleanup',async()=>{
 const events=[],rpc={async call(){throw Error('Definite rejected operation');},async write(){events.push('restore');}};
 const state=guardTransports({rpc,endpoint:{}});
 await assert.rejects(()=>rpc.call(),/Definite/);await rpc.write();assert.equal(state.error,null);assert.deepEqual(events,['restore']);
});
test('Seedsow worker creation and join failures quarantine all helper and retained paths',async()=>{
 for(const failed of ['startWorkers','joinWorkers']){
  const events=[],failure=Error(failed==='startWorkers'?'worker create failed after one launch':'worker is still running');
  const rpc={};
  for(const name of ['startWorkers','joinWorkers','verifyFunction','say','status','ping','write'])
   rpc[name]=async()=>{events.push(name);if(name===failed)throw failure;};
  const endpoint={async request(){events.push('retire');},async read(){events.push('retained-read');}};
  const state=guardTransports({rpc,endpoint});
  await assert.rejects(()=>rpc[failed](),error=>error===failure);
  for(const operation of [...Object.keys(rpc).map(name=>()=>rpc[name]()),()=>endpoint.request(),()=>endpoint.read()])
   await assert.rejects(operation,error=>error===failure);
  assert.equal(state.error,failure);assert.equal(failure.nativeRequestUncertain,true);
  assert.deepEqual(events,[failed]);
 }
});
test('a prior retained uncertainty blocks Seedsow worker and verification helpers before dispatch',async()=>{
 const events=[],failure=Object.assign(Error('lost reply'),{code:'ECONNRESET'});
 const rpc={async startWorkers(){events.push('start');},async joinWorkers(){events.push('join');},
  async verifyFunction(){events.push('verify');},async say(){events.push('say');}};
 const endpoint={async request(){events.push('request');throw failure;}};
 guardTransports({rpc,endpoint});
 await assert.rejects(()=>endpoint.request(),error=>error===failure);
 for(const operation of Object.values(rpc))await assert.rejects(operation,error=>error===failure);
 assert.deepEqual(events,['request']);
});
test('unadmitted Seedsow runs never fall through to SmileBASIC live modules',()=>fixture(async directory=>{
 const run=path.join(directory,'switch-ABCDE');await fs.mkdir(run);
 await fs.writeFile(path.join(run,'moader-seedsow-session.json'),'{}');
 const hosts=require('./host-session.cjs');
 assert.equal(hosts.inferredId(run),'seedsow-malev0');
 assert.throws(()=>require('../runtime/modloader/install.cjs').verifiedRun(run),/not admitted yet/);
 await assert.rejects(()=>require('../runtime/modloader/lib/native/client.cjs').openClient(run),/not admitted yet/);
 await assert.rejects(()=>require('./sd-install.cjs').stage(run,[]),/not admitted yet/);
 assert.throws(()=>hosts.bind({profile:{id:'seedsow-malev0'},run}),/source-owned runtime binding is unavailable/);
 assert.throws(()=>hosts.load({profile:{id:'seedsow-malev0'},run},'shared/rpc.cjs'),/not owned by the held producer/);
 assert.equal(connections,0);
}));
test('malformed, truncated and code-only disconnected transport replies stop follow-on work',async()=>{
 for(const failure of [new SyntaxError('Unexpected end of JSON input'),Error('Memory reply exceeded its limit'),Object.assign(Error('connection failed'),{code:'ECONNRESET'}),Error('aborted')]){
  const events=[],rpc={async call(){events.push('call');throw failure;},async write(){events.push('restore');}};
  const state=guardTransports({rpc,endpoint:{}});
  await assert.rejects(()=>rpc.call(),error=>error===failure);
  await assert.rejects(()=>rpc.write(),error=>error===failure);assert.equal(state.error,failure);assert.deepEqual(events,['call']);
 }
});
test('runtime shares one uncertain-operation latch across selected-run and SD-staging transports',async()=>{
 const run='/host-test/switch2-Test',engine=path.join(root,'research/sources/ns2jb/engine'),events=[],overrides={};
 const rpc={async call(){events.push('run-call');throw new SyntaxError('Unexpected end of JSON input');}};
 const endpoint={async request(){events.push('run-retained');}};
 const stagingRpc={async write(){events.push('staging-write');}},stagingEndpoint={async request(){events.push('staging-retained');}};
 for(const [directory,r,e]of [[run,rpc,endpoint],[engine,stagingRpc,stagingEndpoint]]){
  overrides[path.join(directory,'shared/rpc.cjs')]=r;overrides[path.join(directory,'switch2/tools/endpoint.cjs')]=e;
 }
 const env=process.env.NS2_RUN_DIR;
 try{
  const state=stubModule('tools/install-runtime.cjs',overrides).guardRuntimeTransports(run);
  await assert.rejects(()=>rpc.call(),SyntaxError);
  for(const operation of [()=>endpoint.request(),()=>stagingRpc.write(),()=>stagingEndpoint.request()])await assert.rejects(operation,SyntaxError);
  assert(state.error.nativeRequestUncertain);assert.deepEqual(events,['run-call']);
 }finally{if(env===undefined)delete process.env.NS2_RUN_DIR;else process.env.NS2_RUN_DIR=env;}
});
test('HEN installer and independent RO observer share both transport latches',async()=>{
 const run='/host-test/switch2-Test',events=[],overrides={};
 const runRpc={async call(){events.push('observer-call');throw Error('Synthetic native response timed out');}};
 const runEndpoint={async request(){events.push('observer-retained');}};
 overrides[path.join(run,'shared/rpc.cjs')]=runRpc;
 overrides[path.join(run,'switch2/tools/endpoint.cjs')]=runEndpoint;
 const researchRpc={async write(){events.push('installer-write');}},researchEndpoint={async request(){events.push('installer-retained');}};
 const loader=name=>name==='shared/rpc.cjs'?researchRpc:researchEndpoint;
 const state=stubModule('tools/install-kaihen.cjs',overrides).guardKaihenTransports(run,loader);
 await assert.rejects(()=>runRpc.call(),/timed out/);
 for(const operation of [()=>runEndpoint.request(),()=>researchRpc.write(),()=>researchEndpoint.request()])await assert.rejects(operation,/timed out/);
 assert(state.error.nativeRequestUncertain);assert.deepEqual(events,['observer-call']);
});
test('unique snapshots retain qualified programs, metadata and kaiHEN bytes across source rebuilds',()=>fixture(async directory=>{
 const original=await shipping(directory),metadata=Buffer.from('first catalogue');
 const first=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,programs:original.programs,metadata:[{target:'/moader/runtime/catalogue.bin',data:metadata}]});
 const before=await inputs.validatePrepared(first);
 assert.equal(before.programs[0].data.toString(),'fixture-app-v1');
 await fs.writeFile(original.programs[0].source,'rebuilt-app-v2');
 await fs.writeFile(path.join(original.ship,'hen/main'),'rebuilt-hen');metadata.fill(0);
 const second=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,programs:original.programs,metadata:[{target:'/moader/runtime/catalogue.bin',data:Buffer.from('second catalogue')}]});
 assert.notEqual(first.directory,second.directory);
 const after=await inputs.validatePrepared(first);
 assert.equal(after.programs[0].data.toString(),'fixture-app-v1');assert.equal(after.metadata[0].data.toString(),'first catalogue');
 assert.equal((await fs.readFile(path.join(first.kaihen,'hen/main'))).toString(),'fixture-hen/main');
 assert.equal((await inputs.validatePrepared(second)).programs[0].data.toString(),'rebuilt-app-v2');
 for(const file of [first.manifestFile,first.extraFile,...first.programs.map(item=>item.source),...first.metadata.map(item=>item.source)])assert.equal((await fs.stat(file)).mode&0o222,0);
 assert.equal((await fs.stat(first.directory)).mode&0o222,0);
}));
test('snapshot hash validation rejects changed qualified inputs',()=>fixture(async directory=>{
 const original=await shipping(directory),prepared=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,programs:original.programs});
 await fs.chmod(prepared.programs[0].source,0o600);await fs.writeFile(prepared.programs[0].source,'tampered-app-v1');
 await assert.rejects(()=>inputs.validatePrepared(prepared),/Input changed|Invalid qualified/);
}));
test('invalid extra manifests fail before the actual kaiHEN main can load connection modules',()=>fixture(async directory=>{
 const original=await shipping(directory),extraFile=path.join(directory,'extras.json');
 const qualified={...original.programs[0],bytes:14,sha256:inputs.digest(Buffer.from('fixture-app-v1'))};
 for(const entries of [[{...qualified,target:'/unexpected'}],[qualified,qualified],[{...qualified,sha256:'0'.repeat(64)}],[{...qualified,bytes:13}]]){
  await fs.writeFile(extraFile,JSON.stringify(entries));
  await assert.rejects(()=>kaihenMain([path.join(directory,'switch2-Test'),original.ship,extraFile]),/Unexpected or duplicate|Input changed|Invalid qualified/);
 }
 assert.equal(connections,0);
}));
test('standalone kaiHEN preflight freezes and validates every extra before connection',()=>fixture(async directory=>{
 const original=await pairedRepo(directory),extraFile=path.join(directory,'extras.json');
 await fs.writeFile(extraFile,JSON.stringify(await Promise.all(original.programs.map(async item=>({...item,bytes:(await fs.stat(item.source)).size,sha256:inputs.digest(await fs.readFile(item.source))})))));
 const prepared=await original.qualified.prepareKaihen({ship:original.ship,extraFile,output:path.join(directory,'out')});
 await fs.writeFile(original.programs[1].source,'rebuilt-plugin-v2');
 assert.equal((await inputs.validatePrepared(prepared)).programs[1].data.toString(),'fixture-plugin-v1');
 await original.qualified.qualifyRelease(prepared);
 assert.equal((await fs.stat(prepared.manifestFile)).mode&0o222,0);
 assert.equal(connections,0);
}));
test('combined release qualification rejects a self-consistent snapshot of changed RO/bootstrap inputs',()=>fixture(async directory=>{
 const original=await pairedRepo(directory),build=await inputs.qualifyKaihenBuild(directory);
 const snapshot=()=>inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,kaihenBuild:build});
 const prepared=await snapshot();await original.qualified.qualifyRelease(prepared);
 for(const name of ['ro/main','hen/main','bootstrap.bin','game/main','hbmenu.nro']){
  const file=path.join(original.ship,name),bytes=await fs.readFile(file);
  await fs.writeFile(file,Buffer.concat([bytes,Buffer.from('fixture mutation')]));
  const changed=await snapshot();await inputs.validatePrepared(changed);
  await assert.rejects(()=>original.qualified.qualifyRelease(changed),/differs from the qualified kaiHEN release/);
  await fs.writeFile(file,bytes);
 }
 assert.equal(connections,0);
}));
async function pairedProof(directory){
 for(const [name,data] of Object.entries({
  'runtime/kaihen/libkaihen/include/kaihen/protocol.h':'#define HEN_PROTOCOL_VERSION 7\n',
  'runtime/kaihen/hen/source/main.cpp':'fixture HEN source',
  'runtime/kaihen/bootstrap/source/main.cpp':'fixture bootstrap source',
  'software-builds/kaihen-mo.patch':'fixture patch',
 })){
  const file=path.join(directory,name);await fs.mkdir(path.dirname(file),{recursive:true});await fs.writeFile(file,data);
 }
 const hen=Buffer.from('NSO0-paired-fixture-HEN'),bootstrap=Buffer.from('paired-flat-bootstrap');
 const proof={schema:2,base_commit:'e897f991524590833b82fbe8d63a3c6775fb8101',release_base:'20261114c',patch:'software-builds/kaihen-mo.patch',
  protocol_version:7,native_build_passed:true,console_retest_passed:false,source_tree_algorithm:inputs.SOURCE_TREE_ALGORITHM,
  source_tree_sha256:await inputs.kaihenSourceDigest(directory),patch_sha256:inputs.digest(Buffer.from('fixture patch')),
  hen_sha256:inputs.digest(hen),components:{'hen/main':{bytes:hen.length,sha256:inputs.digest(hen)},'bootstrap.bin':{bytes:bootstrap.length,sha256:inputs.digest(bootstrap)}}};
 const file=path.join(directory,'software-builds/kaihen-mo.json');await fs.writeFile(file,JSON.stringify(proof));
 return{proof,file,hen,bootstrap};
}
async function pairedRepo(directory){
 const original=await shipping(directory),proofFixture=await pairedProof(directory);
 const archive=await fs.readFile(path.join(root,'research/artifacts/kaiHEN-20261114c.zip'));
 assert.equal(inputs.digest(archive),'63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967');
 const localArchive=path.join(directory,'research/artifacts/kaiHEN-20261114c.zip');
 await fs.mkdir(path.dirname(localArchive),{recursive:true});await fs.writeFile(localArchive,archive);
 // These three immutable official components remain production-pinned.
 // Python only reads fixed bounded members from the already hash-qualified
 // local ZIP; no archive path is extracted or target program executed.
 const official=JSON.parse(require('node:child_process').execFileSync('python3',['-c',
  'import base64,json,sys,zipfile\nwith zipfile.ZipFile(sys.argv[1]) as z:\n names=("ro/main","game/main","hbmenu.nro")\n assert all(z.getinfo(n).file_size <= 32*1024*1024 for n in names)\n print(json.dumps({n:base64.b64encode(z.read(n)).decode() for n in names}))',
  localArchive],{encoding:'utf8',maxBuffer:64*1024*1024}));
 for(const [name,data] of Object.entries(official))await fs.writeFile(path.join(original.ship,name),Buffer.from(data,'base64'));
 await fs.writeFile(path.join(original.ship,'hen/main'),proofFixture.hen);
 await fs.writeFile(path.join(original.ship,'bootstrap.bin'),proofFixture.bootstrap);
 await fs.mkdir(path.join(directory,'tools'));
 for(const name of ['install-inputs.cjs','install-kaihen.cjs'])await fs.copyFile(path.join(root,'tools',name),path.join(directory,'tools',name));
 // Unmodified production modules resolve their real __dirname/proof/archive
 // against this isolated repository. Neither qualifier is stubbed.
 const qualified=require(path.join(directory,'tools/install-kaihen.cjs'));
 return{...original,proofFixture,qualified};
}
test('versioned pair proof binds the source, patch, protocol and bounded exact components',()=>fixture(async directory=>{
 const {proof}=await pairedProof(directory),observed={sourceSha256:proof.source_tree_sha256,patchSha256:proof.patch_sha256};
 const build=inputs.validateKaihenProof(proof,observed);assert.equal(build.protocolVersion,7);
 assert.equal(build.components['bootstrap.bin'].sha256,proof.components['bootstrap.bin'].sha256);
 for(const changed of [
  {...proof,schema:3},{...proof,schema:1},{...proof,base_commit:'0'.repeat(40)},{...proof,release_base:'other'},
  {...proof,patch:'other.patch'},{...proof,protocol_version:8},{...proof,native_build_passed:false},
  {...proof,console_retest_passed:undefined},{...proof,source_tree_algorithm:'other'},
  {...proof,source_tree_sha256:'0'.repeat(64)},{...proof,patch_sha256:'0'.repeat(64)},
  {...proof,hen_sha256:'0'.repeat(64)},
  {...proof,components:{'hen/main':proof.components['hen/main']}},
  {...proof,components:{...proof.components,'ro/main':{bytes:1,sha256:'0'.repeat(64)}}},
  {...proof,components:{...proof.components,'bootstrap.bin':{bytes:0,sha256:proof.components['bootstrap.bin'].sha256}}},
  {...proof,components:{...proof.components,'bootstrap.bin':{bytes:1,sha256:'not-a-hash'}}},
 ])assert.throws(()=>inputs.validateKaihenProof(changed,observed));
 const qualified=await inputs.qualifyKaihenBuild(directory);assert.equal(qualified.identity,inputs.digest(await fs.readFile(path.join(directory,'software-builds/kaihen-mo.json'))));
 await fs.writeFile(path.join(directory,'runtime/kaihen/hen/source/main.cpp'),'changed source');
 await assert.rejects(()=>inputs.qualifyKaihenBuild(directory),/source identity differs/);
}));
test('schema-3 proof binds the RO image to the unchanged HEN/bootstrap pair',()=>fixture(async directory=>{
 const {proof,file}=await pairedProof(directory);
 const ro=Buffer.from('NSO0-qualified-RO-fixture');
 const candidate={...proof,schema:3,components:{...proof.components,
  'ro/main':{bytes:ro.length,sha256:inputs.digest(ro)}}};
 const observed={sourceSha256:candidate.source_tree_sha256,patchSha256:candidate.patch_sha256};
 const build=inputs.validateKaihenProof(candidate,observed);
 assert.equal(build.schema,3);
 assert.equal(build.components['ro/main'].sha256,inputs.digest(ro));
 assert.throws(()=>inputs.validateKaihenProof({...candidate,components:proof.components},observed),
  /RO\/HEN\/bootstrap/);
 await fs.writeFile(file,JSON.stringify(candidate));
 assert.equal((await inputs.qualifyKaihenBuild(directory)).schema,3);
}));
test('source proof ignores generated build outputs but rejects links and changed patch/header',()=>fixture(async directory=>{
 const f=await pairedProof(directory),before=await inputs.kaihenSourceDigest(directory);
 await fs.mkdir(path.join(directory,'runtime/kaihen/hen/build'));await fs.writeFile(path.join(directory,'runtime/kaihen/hen/build/main'),'generated');
 assert.equal(await inputs.kaihenSourceDigest(directory),before);
 await fs.writeFile(path.join(directory,'software-builds/kaihen-mo.patch'),'changed patch');
 await assert.rejects(()=>inputs.qualifyKaihenBuild(directory),/patch identity differs/);
 await fs.writeFile(path.join(directory,'software-builds/kaihen-mo.patch'),'fixture patch');
 await fs.symlink(path.join(directory,'software-builds/kaihen-mo.patch'),path.join(directory,'runtime/kaihen/link'));
 await assert.rejects(()=>inputs.qualifyKaihenBuild(directory),/symlink/);await fs.unlink(path.join(directory,'runtime/kaihen/link'));
 await fs.writeFile(path.join(directory,'runtime/kaihen/libkaihen/include/kaihen/protocol.h'),'#define HEN_PROTOCOL_VERSION 8\n');
 f.proof.source_tree_sha256=await inputs.kaihenSourceDigest(directory);await fs.writeFile(f.file,JSON.stringify(f.proof));
 await assert.rejects(()=>inputs.qualifyKaihenBuild(directory),/protocol header differs/);
}));
test('prepared pair is immutable, rejects old/mixed bootstrap, and binds current proof identity',()=>fixture(async directory=>{
 const original=await pairedRepo(directory),f=original.proofFixture;
 const build=await inputs.qualifyKaihenBuild(directory);
 const prepared=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,kaihenBuild:build});
 const qualified=original.qualified;
 await inputs.validatePrepared(prepared);await qualified.qualifyRelease(prepared);
 await fs.writeFile(path.join(original.ship,'bootstrap.bin'),'later rebuild');
 await inputs.validatePrepared(prepared);await qualified.qualifyRelease(prepared);
 const binding=prepared.kaihenBuild;delete prepared.kaihenBuild;
 await assert.rejects(()=>qualified.qualifyRelease(prepared),/proof identity changed/);prepared.kaihenBuild=binding;
 const component=prepared.kaihenFiles.find(item=>item.source.endsWith('/bootstrap.bin')),old=component.sha256;
 component.sha256=inputs.OFFICIAL_COMPONENTS['bootstrap.bin'];
 await assert.rejects(()=>qualified.qualifyRelease(prepared),/differs from the qualified/);component.sha256=old;
 f.proof.reason='new proof revision';await fs.writeFile(f.file,JSON.stringify(f.proof));
 await assert.rejects(()=>qualified.qualifyRelease(prepared),/proof identity changed/);
 assert.equal(connections,0);
}));
test('RO package readback binds both image and this session’s signed policy before activation',()=>{
 const image=Buffer.from('qualified-ro'),policy=Buffer.from('fresh-signed-policy');
 const entries=[{name:'main',size:image.length,sha256:inputs.digest(image)},{name:'main.npdm',size:policy.length,sha256:inputs.digest(policy)}];
 verifyRoPackage({result:0,entries},image,policy);
 for(const invalid of [
  [entries[0]],
  [entries[0],{...entries[1],sha256:'0'.repeat(64)}],
  [{...entries[0],size:image.length+1},entries[1]],
  [entries[0],entries[0]],
 ])assert.throws(()=>verifyRoPackage({result:0,entries:invalid},image,policy),/both expected entries|readback differs/);
});
test('held Seedsow RO package is reused only with the same source and verified donor',()=>fixture(async directory=>{
 const previous=path.join(directory,'switch2/ro/seedsow-kaihen-ro-exefs.nsp');
 const candidate=path.join(directory,'candidate.nsp'),image=Buffer.from('NSO0ro'),policy=Buffer.from('signed policy');
 const bytes=Buffer.concat([Buffer.from('PFS0'),image,policy]);
 await fs.mkdir(path.dirname(previous),{recursive:true});
 await fs.writeFile(previous,bytes);await fs.writeFile(candidate,bytes);
 const packageSha=inputs.digest(bytes),entries=[
  {name:'main',size:image.length,sha256:inputs.digest(image),verified:true},
  {name:'main.npdm',size:policy.length,sha256:inputs.digest(policy),verified:true},
 ];
 const donor={ready:true,mount:'package',kind:'ro',files:entries,
  image:{sha256:inputs.digest(image)},policy:{sha256:inputs.digest(policy)},
  package:{file:previous,size:bytes.length,staged:{path:'@Sdcard:/kaihen/bin/ro/exefs.nsp',
   size:bytes.length,sha256:packageSha}}};
 const report={nativeRo:{donor}};
 assert.equal((await qualifyStagedRoReuse(directory,report,candidate,image,policy)).sha256,packageSha);
 await fs.writeFile(candidate,Buffer.concat([bytes,Buffer.from('changed')]));
 await assert.rejects(()=>qualifyStagedRoReuse(directory,report,candidate,image,policy),/differs/);
 await fs.writeFile(candidate,bytes);donor.files[0].verified=false;
 await assert.rejects(()=>qualifyStagedRoReuse(directory,report,candidate,image,policy),/source-qualified/);
 assert.equal(connections,0);
}));
test('metadata qualification rejects changed bytes before SD staging opens an endpoint',()=>fixture(async directory=>{
 const source=path.join(directory,'catalogue.bin');await fs.writeFile(source,'changed');
 await assert.rejects(()=>require('./sd-install.cjs').stage('/not-a-run',[{source,target:'/moader/runtime/catalogue.bin',bytes:7,sha256:'0'.repeat(64)}]),/Input changed/);
 assert.equal(connections,0);
}));

async function combinedFixture(directory){
 const original=await shipping(directory),app=Buffer.alloc(32);app.write('NRO0',16);
 await fs.writeFile(original.programs[0].source,app);await fs.writeFile(original.programs[1].source,'NSO0fixture-plugin');
 const native={schema:1,sources:{'source.c':'qualified-source'},files:{'templates.json':'qualified-template'}};
 const prepared=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,programs:original.programs,
  nativeRuntime:inputs.digest(Buffer.from(JSON.stringify(native))),metadata:inputs.initialRuntimeMetadata(Buffer.alloc(64))});
 const run=path.join(directory,'switch2-Test');await fs.mkdir(run);const events=[];
 const loaded=stubModule('tools/install-moader.cjs',{
  'node:fs/promises':{...fs,async readFile(file,...args){
   if(file===path.join(root,'software-builds/kaihen-mo.json'))return JSON.stringify({hen_sha256:prepared.kaihenFiles.find(item=>item.source.endsWith('/hen/main')).sha256});
   assert(!String(file).includes('/dist/'),'Explicit snapshot must not reread mutable build outputs');
   return fs.readFile(file,...args);
  }},
  'node:child_process':{spawn(command,args){events.push({child:args});const child=new (require('node:events').EventEmitter)();process.nextTick(()=>child.emit('exit',0));return child;}},
  '../runtime/modloader/lib/native/artifacts.cjs':{artifacts:()=>({manifest:native})},
  './install-inputs.cjs':{...inputs,async qualifyKaihenBuild(){return{schema:1,protocolVersion:6,identity:'fixture-build'};}},
  '../runtime/modloader/install.cjs':{verifiedRun(value){assert.equal(value,run);events.push('verified-run');}},
  './install-runtime.cjs':{async install(value,state,manifest){assert.equal(value,run);assert.deepEqual(manifest,prepared);events.push('runtime-install');return{complete:true};}},
  './install-kaihen.cjs':{async qualifyRelease(){events.push('release-check');}},
 });
 return{loaded,events,prepared,native,run};
}
test('combined install consumes the selected snapshot and never re-prepares mutable dist inputs',()=>fixture(async directory=>{
 const f=await combinedFixture(directory);
 const lines=[],log=console.log;
 try {
  console.log=message=>lines.push(message);
  await f.loaded.main(['--prepared',f.prepared.manifestFile,f.run]);
 } finally {
  console.log=log;
 }
 assert.match(lines[0],/Keep SmileBASIC and its session host open/);
 assert.match(lines.at(-2),/Moader setup complete.*close SmileBASIC/);
 assert.match(lines.at(-1),/Open Moader from hbmenu/);
 assert(f.events.includes('runtime-install'));
 const child=f.events.find(item=>item.child).child;assert.deepEqual(child.slice(1),[f.run,f.prepared.kaihen,f.prepared.extraFile]);
 const journal=JSON.parse(await fs.readFile(path.join(f.run,'moader-install.json')));
 assert.equal(journal.status,'awaiting-device-tests');assert.equal(journal.manifest.directory,f.prepared.directory);
}));
test('a different native build rejects the selected snapshot before checking a run or installing',()=>fixture(async directory=>{
 const f=await combinedFixture(directory);f.native.files['templates.json']='rebuilt-template';
 await assert.rejects(()=>f.loaded.main(['--prepared',f.prepared.manifestFile,f.run]),/Prepared native runtime differs/);
 assert(!f.events.includes('verified-run'));assert(!f.events.includes('runtime-install'));
 await assert.rejects(()=>fs.stat(path.join(f.run,'moader-install.json')),/ENOENT/);
}));
test('combined preflight rejects an old two-file metadata manifest before run qualification or native installation',()=>fixture(async directory=>{
 const f=await combinedFixture(directory),old=path.join(directory,'old-manifest.json');
 await fs.writeFile(old,JSON.stringify({...f.prepared,metadata:f.prepared.metadata.slice(0,2)}));
 await assert.rejects(()=>f.loaded.main(['--prepared',old,f.run]),/exact catalogue\/ack\/log0\/log1 metadata set/);
 assert(!f.events.includes('verified-run'));assert(!f.events.includes('runtime-install'));
 await assert.rejects(()=>fs.stat(path.join(f.run,'moader-install.json')),/ENOENT/);
}));
test('combined shared lock rejection preserves the existing owner and journals the stopped attempt',()=>fixture(async directory=>{
 const f=await combinedFixture(directory),shared=path.join(f.run,'ns2jb-modloader-install.json');
 await fs.writeFile(shared,'existing-owner-journal');
 await assert.rejects(()=>f.loaded.main(['--prepared',f.prepared.manifestFile,f.run]),/EEXIST/);
 assert.equal(await fs.readFile(shared,'utf8'),'existing-owner-journal');
 const journal=JSON.parse(await fs.readFile(path.join(f.run,'moader-install.json')));
 assert.equal(journal.status,'stopped');assert.equal(journal.inspectBeforeRetry,true);assert(!f.events.includes('runtime-install'));
 const before=await fs.readFile(path.join(f.run,'moader-install.json'),'utf8');
 await assert.rejects(()=>f.loaded.main(['--prepared',f.prepared.manifestFile,f.run]),/EEXIST/);
 assert.equal(await fs.readFile(path.join(f.run,'moader-install.json'),'utf8'),before);
}));

test('SD full readback compares content and suppresses native closes after uncertainty',async()=>{
 const {verifySdFile}=require('./sd-install.cjs'),bytes=Buffer.from('staged-bytes');
 for(const fault of [undefined,'size','content','timeout']){
  const closed=[],s={async ipc(){return{result:0,handle:10};},async close(h){closed.push(h);}};
  const filesystem={async openFile(){return{result:0,handle:11};},async fileSize(){return BigInt(bytes.length+(fault==='size'?1:0));},async readFile(){
   if(fault==='timeout')throw Error('Memory request timed out; it was not replayed');return fault==='content'?Buffer.alloc(bytes.length):bytes;
  }};
  const operation=verifySdFile({s,root:1,domain:{oneHandle:r=>r.handle},filesystem,target:'/fixture',expected:bytes});
  if(fault)await assert.rejects(()=>operation,/size differs|bytes differ|timed out/);else assert.equal((await operation).readbackVerified,true);
  assert.deepEqual(closed,fault==='timeout'?[]:[11,10]);
 }
});

test('initial metadata preserves catalogue/ack sizes and allocates two independent exact zero log slots',async()=>{
 await inputs.assertLogWireContract();const catalogue=Buffer.alloc(64,0x5a),metadata=inputs.initialRuntimeMetadata(catalogue);
 assert.deepEqual(metadata.map(entry=>[entry.target,entry.data.length]),[
  ['/moader/runtime/catalogue.bin',64],['/moader/runtime/ack.bin',64],['/moader/runtime/log0.bin',65616],['/moader/runtime/log1.bin',65616],
 ]);
 assert(metadata[0].data.equals(catalogue));assert(metadata.slice(1).every(entry=>entry.data.every(byte=>byte===0)));
 catalogue.fill(0);assert.equal(metadata[0].data[0],0x5a);metadata[2].data[0]=1;assert.equal(metadata[3].data[0],0);
 assert.throws(()=>inputs.initialRuntimeMetadata(Buffer.alloc(65)),/64-byte header ABI/);
});
test('required metadata validates exact targets/sizes/zeros rather than an array length of four',()=>{
 const valid=()=>inputs.initialRuntimeMetadata(Buffer.alloc(64));inputs.assertMoaderMetadata(valid().reverse());
 for(const mutate of [
  rows=>rows.pop(),rows=>rows.splice(2),rows=>{rows[3].target=rows[2].target;},rows=>{rows[3].target='/moader/runtime/log2.bin';},
  ...[0,1,2,3].map(index=>rows=>{rows[index].data=Buffer.alloc(rows[index].data.length+1);}),
  ...[1,2,3].map(index=>rows=>{rows[index].data[rows[index].data.length-1]=1;}),
 ]){const rows=valid();mutate(rows);assert.throws(()=>inputs.assertMoaderMetadata(rows),/metadata|receipt\/log snapshot/);}
});
test('native log header/capacity/path drift fails the local contract gate',async()=>{
 const source=await fs.readFile(path.join(root,'include/moader/runtime_log_wire.h'),'utf8');
 for(const changed of [source.replace('(80u + MOADER_LOG_PAYLOAD)','(81u + MOADER_LOG_PAYLOAD)'),
  source.replace('(64u * 1024u)','(32u * 1024u)'),source.replace('/moader/runtime/log1.bin','/moader/runtime/log2.bin')]){
  assert.notEqual(changed,source);
  const loaded=stubModule('tools/install-inputs.cjs',{'node:fs/promises':{...fs,readFile:async()=>changed}});
  await assert.rejects(()=>loaded.assertLogWireContract(),/contract changed/);
 }
});
test('both log slots are immutable hashed snapshot inputs and rehashed nonzero initial bytes still reject',()=>fixture(async directory=>{
 const original=await shipping(directory),metadata=inputs.initialRuntimeMetadata(Buffer.alloc(64));
 const prepared=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,programs:original.programs,metadata});
 metadata[2].data.fill(0xff);const frozen=await inputs.validateMoaderPrepared(prepared);
 assert.equal(frozen.metadata.length,4);
 for(const target of ['/moader/runtime/log0.bin','/moader/runtime/log1.bin']){
  const item=frozen.metadata.find(row=>row.target===target);assert.equal(item.data.length,65616);assert(item.data.every(byte=>byte===0));
  assert.equal(item.sha256,inputs.digest(Buffer.alloc(65616)));assert.equal((await fs.stat(item.source)).mode&0o222,0);
 }
 const changed=inputs.initialRuntimeMetadata(Buffer.alloc(64));changed[3].data[65536]=1;
 const invalid=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,programs:original.programs,metadata:changed});
 await assert.rejects(()=>inputs.validateMoaderPrepared(invalid),/must be all zero/);
}));
test('65616-byte log readback checks the entire 64-KiB block plus the trailing 80 bytes',async()=>{
 for(const badTail of [false,true]){
  const expected=Buffer.alloc(65616),reads=[],closed=[];
  const s={async ipc(){return{result:0,handle:10};},async close(handle){closed.push(handle);}};
  const filesystem={async openFile(){return{result:0,handle:11};},async fileSize(){return 65616n;},async readFile(s,h,offset,size){
   reads.push({offset,size});const data=Buffer.alloc(size);if(badTail&&offset===65536)data[size-1]=1;return data;
  }};
  const operation=require('./sd-install.cjs').verifySdFile({s,root:1,domain:{oneHandle:r=>r.handle},filesystem,target:'/moader/runtime/log0.bin',expected});
  if(badTail)await assert.rejects(()=>operation,/Staged bytes differ/);else assert.equal((await operation).readbackVerified,true);
  assert.deepEqual(reads,[{offset:0,size:65536},{offset:65536,size:80}]);assert.deepEqual(closed,[11,10]);
 }
});

async function runtimeMetadataFixture(directory,{captureError,readbackFault,profileFault,metadata}={}){
 const original=await shipping(directory),run=path.join(directory,'switch2-LogOffline'),output=path.join(directory,'runtime-state');
 const prepared=await inputs.snapshot({output:path.join(directory,'out'),kaihen:original.ship,programs:original.programs,
  metadata:metadata??inputs.initialRuntimeMetadata(Buffer.alloc(64))});
 const events=[],overrides={
  '../runtime/modloader/install.cjs':{verifiedRun:()=>events.push('verified-run')},
  '../runtime/modloader/lib/native-capture.cjs':{async captureNative(selected,state){
   events.push('capture');if(captureError)throw captureError;
   await fs.mkdir(path.join(state,'native-fs'),{recursive:true});
   await fs.writeFile(path.join(state,'native-fs/manifest.json'),JSON.stringify({verified:profileFault!=='unverified',session:profileFault==='session'?'switch2-Other':path.basename(selected),runtimeLoggingCompatible:true}));return'unused-capture';
  }},
  '../runtime/modloader/lib/lazy-context.cjs':{plan:()=>({payload:{filename:'payload.bin'}})},
  './sd-install.cjs':{async stage(selected,entries){
   events.push('metadata');assert.equal(selected,run);assert.equal(entries.length,4);
   const receipts=entries.map(entry=>({target:entry.target,bytes:entry.bytes,sha256:entry.sha256,readbackVerified:true}));
   if(readbackFault==='missing')receipts.pop();
   if(readbackFault==='duplicate')receipts[3]={...receipts[2]};
   if(readbackFault==='size')receipts[3].bytes--;
   if(readbackFault==='digest')receipts[3].sha256='0'.repeat(64);
   if(readbackFault==='unverified')receipts[3].readbackVerified=false;
   return receipts;
  }},
  '../runtime/modloader/lib/diagnostics.cjs':{report:()=>({buffer:Buffer.from('required report'),sdPath:'/moader/runtime/runtime.log'}),async writeReport(){events.push('report');return{};}},
  '../runtime/modloader/lib/native.cjs':{async buildAll(){events.push('build');return{runtimeBytes:1,codeBytes:1};}},
  '../runtime/modloader/lib/context.cjs':{buildContext:()=>Buffer.alloc(64)},
  '../runtime/modloader/lib/resident.cjs':{async install(){events.push('activate');return{verified:true,active:true,controllerHandlesClosed:true,diagnosticsOwned:true,diagnosticWriteErrors:'0'};}},
 };
 for(const base of [run,path.join(root,'research/sources/ns2jb/engine')]){
  overrides[path.join(base,'shared/rpc.cjs')]={};overrides[path.join(base,'switch2/tools/endpoint.cjs')]={};
 }
 const loaded=stubModule('tools/install-runtime.cjs',overrides,name=>{if(name.endsWith('/shared/rpc.cjs')||name.endsWith('/tools/endpoint.cjs'))events.push('load-transport');});
 const operation=async()=>{const env=process.env.NS2_RUN_DIR;try{return await loaded.install(run,output,prepared);}finally{if(env===undefined)delete process.env.NS2_RUN_DIR;else process.env.NS2_RUN_DIR=env;}};
 return{operation,events,prepared,output};
}
test('runtime stages/readback-verifies both slots after fresh FS qualification and before activation',()=>fixture(async directory=>{
 const f=await runtimeMetadataFixture(directory),record=await f.operation();assert.equal(record.complete,true);assert.equal(record.freshFilesystemQualified,true);
 assert.equal(record.metadata.length,4);assert(record.metadata.every(entry=>entry.readbackVerified));
 assert(f.events.indexOf('capture')<f.events.indexOf('metadata'));assert(f.events.indexOf('metadata')<f.events.indexOf('report'));assert(f.events.indexOf('metadata')<f.events.indexOf('activate'));
}));
test('old/incomplete or incorrectly sized metadata rejects before loading any native transport',()=>fixture(async directory=>{
 for(const fault of ['old-two','missing','wrong-size','nonzero']){
  const folder=path.join(directory,fault);await fs.mkdir(folder);const metadata=inputs.initialRuntimeMetadata(Buffer.alloc(64));
  if(fault==='old-two')metadata.splice(2);if(fault==='missing')metadata.pop();if(fault==='wrong-size')metadata[2].data=Buffer.alloc(65615);if(fault==='nonzero')metadata[3].data[80]=1;
  const f=await runtimeMetadataFixture(folder,{metadata});await assert.rejects(f.operation,/metadata|receipt\/log snapshot/);
  assert(!f.events.includes('load-transport'));assert(!f.events.includes('capture'));assert(!f.events.includes('metadata'));assert(!f.events.includes('activate'));
 }
}));
test('occupied live FS or mismatched capture identity stops before metadata can replace retained snapshots',()=>fixture(async directory=>{
 for(const fault of ['occupied','session','unverified']){
  const folder=path.join(directory,fault);await fs.mkdir(folder);
  const f=await runtimeMetadataFixture(folder,fault==='occupied'?{captureError:Error('FS gateway space is already occupied')}:{profileFault:fault});
  await assert.rejects(f.operation,/already occupied|same-session verified fresh/);
  assert(!f.events.includes('metadata'));assert(!f.events.includes('report'));assert(!f.events.includes('activate'));
 }
}));
test('missing/duplicate/wrong-sized/unverified metadata readback cannot reach report creation or activation',()=>fixture(async directory=>{
 for(const readbackFault of ['missing','duplicate','size','digest','unverified']){
  const folder=path.join(directory,readbackFault);await fs.mkdir(folder);const f=await runtimeMetadataFixture(folder,{readbackFault});
  await assert.rejects(f.operation,/metadata readback/);assert(!f.events.includes('report'));assert(!f.events.includes('activate'));
 }
}));

// Exercise real source qualification. The fake retains method tables/reference
// baselines, so required and optional paths meet the same ABI checks.
async function sourceFixture(directory,logPath,fail){
 const run=path.join(directory,'switch2-Test'),events=[],objects=new Map(),clones=new Map();
 await fs.mkdir(path.join(run,'switch2/ro'),{recursive:true});await fs.writeFile(path.join(run,'switch2/ro/switch2-Test-layout.json'),JSON.stringify({table:0x6100000000,count:100}));
 let next=10;
 const base=0x4000000000n;
 const domain={wire:{control:id=>({id})},oneHandle:reply=>reply.handle,async send(s,label,handle,wire){
  if(wire.id===2){const clone=++next;clones.set(clone,{kind:label.includes('SD-filesystem')?'sd':'file',id:clone});return{handle:clone};}
  return{result:0,data:Buffer.from(Uint32Array.of(clones.get(handle).id).buffer)};
 }};
 const s={async ipc(){return{handle:5};},async close(handle){events.push('close-'+handle);}};
 const a={s,domain,filesystem:{async pathMethod(){return{result:fail==='open'?0x202:0,handle:6};},async fileSize(){return BigInt(fail==='size'?99:32);}},root:1,async close(){events.push('client-close');}};
 const bridge={base,session:'switch2-Test',c:{pending:false},m:{async read(at,size){
  if(size===0x30){const id=(at-0x6100000000)/0x30+1,kind=clones.get(id).kind,object=0x6200000000+id*0x1000;objects.set(object,kind);const bytes=Buffer.alloc(size);bytes.writeBigUInt64LE(BigInt(object),32);return bytes;}
  if(size===80){const kind=objects.get(at-0x100),bytes=Buffer.alloc(size);for(const [slot,offset]of kind==='sd'?[[0,0x4b8c0],[1,0x103390]]:[[0,0x1036f0],[1,0x104e10],[4,0x104ef0],[5,fail==='method'?0:0x1050a0],[6,0x1053d0],[7,0x105410]])bytes.writeBigUInt64LE(base+BigInt(offset),slot*8);return bytes;}
  const bytes=Buffer.alloc(size);bytes.writeBigUInt64LE(BigInt(at+0x100));bytes.writeUInt32LE(2,size-4);return bytes;
 },async u32(){return 1;}}};
 const loaded=stubModule('runtime/modloader/lib/native/source.cjs',{'../game-files/mount-api.cjs':{attachGame:async()=>a}});
 return{operation:()=>loaded.openSource(run,bridge,{sdPath:logPath,size:32}),events};
}
for(const failure of ['open','size','method'])test('required Moader report '+failure+' failure aborts instead of optional downgrade',()=>fixture(async directory=>{
 const f=await sourceFixture(directory,'/moader/runtime/runtime.log',failure);
 await assert.rejects(f.operation,/Cannot retain|size changed|method differs/);assert(f.events.includes('client-close'));
}));
test('generic upstream report remains optional and valid Moader report qualifies',()=>fixture(async directory=>{
 const optional=await sourceFixture(directory,'/ns2jb-modloader/logs/20261114T010101Z-switch2-Test.log','open');
 const source=await optional.operation();assert(source.filesystem&&!source.logFile);await source.close();
 const required=await sourceFixture(directory,'/moader/runtime/runtime.log');
 const qualified=await required.operation();assert(qualified.logFile&&qualified.filesystem);await qualified.close();
}));

async function residentFixture(directory,fault){
 const run=path.join(directory,'switch2-Test'),output=path.join(directory,'state'),capture=path.join(directory,'capture.bin'),events=[];
 await fs.mkdir(path.join(output,'prepared'),{recursive:true});await fs.writeFile(path.join(output,'prepared/payload.bin'),Buffer.alloc(0));await fs.writeFile(capture,Buffer.alloc(16));
 const allocation=0x6300000000n,sd={object:0x6100001000,refOffset:40,baseline:1},log={object:0x6100002000,refOffset:32,baseline:1};let retained=false,activated=false;
 const offsets={lfs_initialize:1,lfs_activate:2};
 class Bridge{
  constructor(){this.base=0x4000000000n;this.cave=this.base+0x24d100n;this.previous=Buffer.alloc(1024);this.c={pending:false,info:async()=>({result:0,memory:{permissions:3,attributes:0,address:String(allocation),size:4096}}),close:async()=>events.push('controller-close')};
   this.m={u64:async()=>0x6100003000n,u32:async at=>at===sd.object+40||at===log.object+32?(retained?2:1):(fault==='initialize'||activated&&fault==='activate'?1:0)};
  }
  async open(){events.push('open');}
  async call(){events.push('allocate');return allocation;}
  async writeData(){}
  async mapRuntime(code,save){const value={address:'274877906944',bytes:4096,published:true};await save(value);return value;}
  async callRuntime(offset){if(offset===1)retained=true;if(offset===2)activated=true;return 0n;}
  async stage(){events.push('stage');}
  async controlCave(){events.push('hook-stage');}
  async patch(){events.push('patch');}
  async read(at,size){return Buffer.alloc(size);}
  async releaseControl(){}
  async close(){events.push('close');}
 }
 const source={filesystem:sd,...(fault==='missing'?{}:{logFile:log}),close:async()=>events.push('source-close')};
 const native={runtimeCode:Buffer.alloc(4096),code:Buffer.alloc(16),offsets,hooks:[],sdk:{}};
 const loaded=stubModule('runtime/modloader/lib/resident.cjs',{
  './native/bridge.cjs':{Bridge},'./native/source.cjs':{openSource:async()=>source,unpin:async()=>{}},
  './game-files/mount-api.cjs':{},'./content.cjs':{},
  './context.cjs':{layout:()=>({bytes:4096,lazyOffset:480,romfs:368,exefs:112}),buildContext:()=>Buffer.alloc(4096)},
  './native.cjs':{validateCapture:()=>{},buildAll:async()=>native},
 });
 const plan={schema:5,session:'switch2-Test',moader:true,lazy:true,titleIds:[],diagnostics:{sdPath:'/moader/runtime/runtime.log'},payload:{filename:'payload.bin'}};
 return{operation:()=>loaded.install(run,plan,output,capture,null),events,output};
}
for(const failure of ['missing','initialize'])test('resident blocks '+failure+' report before hooks attach',()=>fixture(async directory=>{
 const f=await residentFixture(directory,failure);await assert.rejects(f.operation,/Moader requires|could not be initialized/);
 assert(!f.events.includes('hook-stage'));assert(!f.events.includes('patch'));
 const journal=JSON.parse(await fs.readFile(path.join(f.output,'resident-install.json')));assert.equal(journal.verified,false);assert.equal(journal.hookMayBeAttached,false);
}));
test('post-activation report failure cannot produce verified acceptance',()=>fixture(async directory=>{
 const f=await residentFixture(directory,'activate');await assert.rejects(f.operation,/report write failed after activation/);
 const journal=JSON.parse(await fs.readFile(path.join(f.output,'resident-install.json')));assert.equal(journal.verified,false);assert.equal(journal.resourcesRetained,true);assert.equal(journal.requiresInspectionOrReboot,true);
}));
test('successful Moader report ownership survives controller closure',()=>fixture(async directory=>{
 const f=await residentFixture(directory);const result=await f.operation();assert.equal(result.diagnosticsOwned,true);assert.equal(result.diagnosticWriteErrors,'0');assert.equal(result.controllerHandlesClosed,true);assert.equal(result.verified,true);assert(f.events.indexOf('source-close')<f.events.indexOf('hook-stage'));
}));
(async()=>{
 for(const {name,action}of tests){await action();console.log('PASS: '+name);}
 assert.equal(connections,0);assert.equal(candidateReads,0);
 console.log('PASS: '+tests.length+' host installer checks; no endpoint/HTTP connections or candidate build reads');
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
