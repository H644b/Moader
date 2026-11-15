'use strict';
// Offline provider/run qualification. Importing this module opens no transport.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),Module=require('node:module');
const ROOT=path.resolve(__dirname,'..'),DEFAULT='smilebasic-kurumuz';
const check=(ok,message)=>{if(!ok)throw Error(message);};
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const CIV_TREE='d83cadc568b65a724f32b848f1681540487a798b354ac3286f71e41e94a9ac75';
let bound=null;const privateModules=new Map();
function safeFile(root,relative){
 check(typeof relative==='string'&&relative&&!path.isAbsolute(relative)&&!relative.includes('\\')&&!relative.split('/').some(p=>!p||p==='.'||p==='..'),'Unsafe provider-relative path');
 const file=path.join(root,relative);
 for(let part=file;;part=path.dirname(part)){
  check(!fs.lstatSync(part).isSymbolicLink(),'Provider/run path contains a symlink');
  if(path.dirname(part)===part)break;
 }
 check(fs.statSync(file).isFile(),'Provider/run input is not a file');return file;
}
function readJson(file){const bytes=fs.readFileSync(file);check(bytes.length<=4*1024*1024,'Provider JSON exceeds bound');return JSON.parse(bytes.toString('utf8'));}
function materializedFiles(files){
 const result={};for(const [name,hash]of Object.entries(files))if(/^engine\/(lib|shared|switch2)\//.test(name))result[name.slice(7)]=hash;
 const mapping={'shared/target.json':'target.json','shared/sdk-functions.json':'sdk-functions.json','shared/sdk-layout.json':'sdk-layout.json'};
 for(const name of ['thread-code.json','event-code.json','timing-code.json','hash-code.json','sdk-proxy-code.json','thread-scheduling-reference.json','compat-affinity-reference.json'])mapping['switch2/app/'+name]='app/'+name;
 for(const [dest,source]of Object.entries(mapping)){check(files['targets/civ7/'+source],'Missing Civ7 target artifact');result[dest]=files['targets/civ7/'+source];}return result;
}
function resolveProfile(id=DEFAULT,root=ROOT){
 check([DEFAULT,'civ7-s8n'].includes(id),'Unsupported host profile: '+id);
 if(id===DEFAULT)return{id,target:'smilebasic',revision:'28f03b5895e5569d8fbc35cbed84906b8f0a65b2',sourceTreeSha256:'7003335a2a426a82156c19653be5e61506a05dd02b5d0490c8bcb4516c5c354a',sourceDirectory:path.join(root,'research/sources/ns2jb/engine'),...require('./host-source-pins.cjs')[DEFAULT]};
 const descriptor=readJson(safeFile(root,'config/civ7-provider.json'));
 check(descriptor.schemaVersion===1&&descriptor.id===id&&descriptor.target==='civ7'&&descriptor.repository==='s8n/civ7-fs-kaihen'&&descriptor.revision==='51a804cbce09216e1a42e65929644fd436b4c8bf'&&descriptor.sourceTreeSha256===CIV_TREE&&descriptor.sourceSha256==='d4bcfcaa1e4debf5dd205a8f99effe38ca272de0d7d9d93d0ef45f7d6c75fe6d'&&descriptor.sourceDirectory==='research/sources/civ7-fs-kaihen','Civ7 provider descriptor differs from the qualified profile');
 check(descriptor.files&&Object.keys(descriptor.files).length===120&&descriptor.runFiles&&Object.keys(descriptor.runFiles).length===110,'Incomplete Civ7 source/materialization manifest');
 const derived=materializedFiles(descriptor.files),keys=Object.keys(derived);
 check(keys.length===Object.keys(descriptor.runFiles).length&&keys.every(key=>descriptor.runFiles[key]===derived[key]),'Civ7 run manifest is not derived from the pinned source closure');
 return{...descriptor,sourceDirectory:path.join(root,descriptor.sourceDirectory)};
}
function binding(profile){return{schema:1,id:profile.id,revision:profile.revision,sourceTreeSha256:profile.sourceTreeSha256};}
function preparedProfile(value,root=ROOT){
 const profile=resolveProfile(value?.id||DEFAULT,root);
 if(value){const expected=binding(profile);check(Object.keys(value).length===4&&Object.keys(expected).every(key=>value[key]===expected[key]),'Prepared host-profile identity differs');}
 return profile;
}
function qualifySources(profile){
 const entries=[];
 for(const [name,expected]of Object.entries(profile.files).sort(([a],[b])=>a.localeCompare(b,'en'))){
  const bytes=fs.readFileSync(safeFile(profile.sourceDirectory,name));
  check(bytes.length<=32*1024*1024&&sha(bytes)===expected,'Qualified host source changed: '+name);entries.push([name,expected]);
 }
 // Use ordinal filename ordering, matching the Python provisioning tree hash.
 const tree=sha(entries.sort(([a],[b])=>a<b?-1:a>b?1:0).map(([name,hash])=>name+'\0'+hash+'\n').join(''));
 check(tree===profile.sourceTreeSha256,'Host source manifest/tree differs from the pinned profile');
 if(profile.id==='civ7-s8n'){
  const walk=directory=>{for(const row of fs.readdirSync(directory,{withFileTypes:true})){check(!row.isSymbolicLink(),'Symlink in selected Civ7 sources');const file=path.join(directory,row.name);if(row.isDirectory())walk(file);else check(Object.hasOwn(profile.files,path.relative(profile.sourceDirectory,file).split(path.sep).join('/')),'Unexpected file in selected Civ7 source closure');}};
  walk(path.join(profile.sourceDirectory,'engine'));walk(path.join(profile.sourceDirectory,'targets/civ7'));
 }return profile;
}
function runJson(run,relative){return readJson(safeFile(run,relative));}
function linkedJson(run,file){
 check(typeof file==='string','Missing run proof path');const absolute=path.resolve(run,file),relative=path.relative(run,absolute);
 return runJson(run,relative.split(path.sep).join('/'));
}
function qualifyRun(run,expected,root=ROOT,{retired=false}={}){
 run=path.resolve(run);check(/^switch2-[A-Za-z0-9]+$/.test(path.basename(run)),'Invalid held run directory');
 const result=runJson(run,'result.json');
 const inferred=result.target==='civ7'?'civ7-s8n':DEFAULT,profile=resolveProfile(expected||inferred,root);
 check(profile.target==='civ7'?result.target==='civ7':result.target===undefined||result.target==='smilebasic','Run target and prepared host profile differ');
 check(result.verified&&result.rpc?.state==='listening'&&!result.completed,'Selected run is not a verified listening held session');
 const current=runJson(run,'switch2/fs-execution-current.json'),proof=linkedJson(run,current.file);
 check(current.verified&&proof.verified&&proof.session===path.basename(run),'Missing same-session verified FS instruction proof');
 if(profile.id===DEFAULT)return{profile,run,root,runtimeRoot:profile.sourceDirectory,target:profile.target,current,proof};
 qualifySources(profile);
 check(result.platform===2&&result.sourceSha256===profile.sourceSha256&&result.fs?.verified&&result.fs.restored&&current.restored&&current.cleanup?.deferred&&current.retainedEndpoint?.ready&&current.retainedEndpoint.session===proof.session&&proof.identity?.programId==='0400000000000000'&&proof.notifier&&proof.mapping&&proof.trials?.length===2&&proof.trials.every(t=>t.verified&&t.restored&&!t.pending),'Civ7 run is not the qualified FS-only restored held profile');
 for(const [name,expectedHash]of Object.entries(profile.runFiles))check(sha(fs.readFileSync(safeFile(run,name)))===expectedHash,'Civ7 materialized source/profile changed: '+name);
 for(const folder of ['lib','shared','switch2']){
  const walk=dir=>{for(const entry of fs.readdirSync(dir,{withFileTypes:true})){check(!entry.isSymbolicLink(),'Symlink in materialized runtime');const f=path.join(dir,entry.name);if(entry.isDirectory())walk(f);else if(/\.(?:cjs|mjs|js|node)$/i.test(entry.name))check(Object.hasOwn(profile.runFiles,path.relative(run,f).split(path.sep).join('/')),'Unqualified executable in Civ7 run');}};walk(path.join(run,folder));
 }
 const connection=runJson(run,'connection.json'),addresses=connection.addresses;
 check(Number.isInteger(connection.port)&&connection.port>0&&connection.port<=65535&&/^[a-f0-9]{64}$/.test(connection.token)&&addresses?.target==='civ7'&&addresses.platform===2&&addresses.session===proof.session,'Civ7 authenticated transport/session identity differs');
 const acquisition=runJson(run,'switch2/current-acquisition.json');
 check(acquisition.report===proof.report,'Civ7 acquisition and FS proof reports differ');
 const report=linkedJson(run,acquisition.report),execution=runJson(run,'switch2/ro/current-execution.json');
 check(report.session===proof.session&&report.retainedMemory?.ready&&!report.retainedMemory.pending&&report.retainedMemory.zeroCountVerified,'Civ7 retained FS endpoint is not qualified');
 check(execution.session===proof.session&&execution.report===proof.report&&/^\d+$/.test(execution.pid)&&BigInt(execution.pid)>1n&&(execution.verified||retired&&execution.retiredForKaihen),'Civ7 diagnostic RO identity is stale or retired');
 check(!result.ro&&!result.roImage&&!result.app&&!result.error,'Civ7 frontend/app-only/failed run is not admitted');
 return{profile,run,root,runtimeRoot:run,target:profile.target,current,proof,execution,retired,connectionSha256:sha(fs.readFileSync(path.join(run,'connection.json')))};
}
function forRun(run,expected,root=ROOT,options={}){
 return qualifyRun(run,expected,root,options);
}
function bind(context){
 check(!bound||bound.id===context.profile.id&&(context.profile.id===DEFAULT||bound.run===context.run),'A different host profile/run was imported; use a fresh installer process');
 bound={id:context.profile.id,run:context.run};
 if(context.profile.id===DEFAULT){process.env.NS2_RUN_DIR=context.run;delete process.env.NS2_TARGET;delete process.env.NS2_TARGET_ROOT;delete process.env.NS2_TARGET_DIR;delete process.env.NS2_PORT_ROOT;return context;}
 process.env.NS2_RUN_DIR=context.run;process.env.NS2_TARGET='civ7';process.env.NS2_TARGET_ROOT=context.run;
 delete process.env.NS2_TARGET_DIR;delete process.env.NS2_PORT_ROOT;return context;
}
function load(context,relative){
 if(context.profile.id==='civ7-s8n'){
  const current=qualifyRun(context.run,context.profile.id,context.root||ROOT,{retired:!!context.retired});
  check(!context.connectionSha256||context.connectionSha256===current.connectionSha256,'Civ7 connection changed after qualification');
  check(Object.hasOwn(context.profile.runFiles,relative),'Module is not in the qualified Civ7 manifest');
  check(sha(fs.readFileSync(safeFile(context.run,relative)))===context.profile.runFiles[relative],'Civ7 module changed before import');
  bind(context);return privateLoad(context,relative);
 }
 bind(context);return require(path.join(context.runtimeRoot,relative));
}
function privateLoad(context,relative,append=''){
 const filename=safeFile(context.run,relative);
 check(!require.cache[filename],'Foreign cached module at the Civ7 runtime path');
 check(relative==='connection.json'||Object.hasOwn(context.profile.runFiles,relative),'Unqualified Civ7 dependency');
 const bytes=fs.readFileSync(filename);if(relative!=='connection.json')check(sha(bytes)===context.profile.runFiles[relative],'Civ7 dependency changed before evaluation');
 const key=filename;
 if(privateModules.has(key)&&!append)return privateModules.get(key).exports;
 if(relative.endsWith('.json')){const value=JSON.parse(bytes.toString('utf8'));privateModules.set(key,{exports:value});return value;}
 check(relative.endsWith('.cjs'),'Only qualified CJS dependencies can execute');
 const loaded=new Module(filename,module);loaded.filename=filename;loaded.paths=Module._nodeModulePaths(path.dirname(filename));privateModules.set(key,loaded);
 loaded.require=name=>{
  if(Module.builtinModules.includes(name)||name.startsWith('node:'))return require(name);
  check(name.startsWith('.')||path.isAbsolute(name),'External package is outside the Civ7 runtime closure');
  let file=path.resolve(path.dirname(filename),name);if(!path.extname(file))file+='.cjs';
  const rel=path.relative(context.run,file).split(path.sep).join('/');return privateLoad(context,rel);
 };
 try{loaded._compile(bytes.toString('utf8')+append,filename);loaded.loaded=true;return loaded.exports;}
 catch(error){privateModules.delete(key);throw error;}
}
function compile(context,relative,append){load(context,relative);return privateLoad(context,relative,append);}
function inferredId(run){
 run=path.resolve(run);const values=[];
 for(const name of ['result.json','connection.json','shared/target.json'])if(fs.existsSync(path.join(run,name)))values.push(runJson(run,name));
 return values.some(v=>v.target==='civ7'||v.name==='civ7'||v.addresses?.target==='civ7'||typeof v.token==='string')?'civ7-s8n':DEFAULT;
}
module.exports={ROOT,DEFAULT,resolveProfile,qualifySources,binding,preparedProfile,qualifyRun,forRun,load,compile,bind,inferredId,safeFile,runJson,linkedJson,sha,materializedFiles};
