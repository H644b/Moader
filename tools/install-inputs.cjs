'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const check=(value,message)=>{if(!value)throw Error(message);};
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const PROGRAM_TARGETS=['/switch/Moader.nro','/kaihen/plugins/moader.nso'];
const LOG_SNAPSHOT_CAPACITY=65616;
const METADATA_SIZES=Object.freeze({'/moader/runtime/catalogue.bin':64,'/moader/runtime/ack.bin':64,
 '/moader/runtime/log0.bin':LOG_SNAPSHOT_CAPACITY,'/moader/runtime/log1.bin':LOG_SNAPSHOT_CAPACITY});
const METADATA_TARGETS=Object.freeze(Object.keys(METADATA_SIZES));
const COMPONENTS=['ro/main','hen/main','bootstrap.bin','game/main','hbmenu.nro'];
const MAX=32*1024*1024;

async function assertLogWireContract(){
 const header=await fs.readFile(path.resolve(__dirname,'../include/moader/runtime_log_wire.h'),'utf8');
 check(/#define MOADER_LOG_VERSION\s+1u\b/.test(header)&&/#define MOADER_LOG_PAYLOAD\s+\(64u\s*\*\s*1024u\)/.test(header)&&
  /#define MOADER_LOG_CAPACITY\s+\(80u\s*\+\s*MOADER_LOG_PAYLOAD\)/.test(header)&&
  /static_assert\(sizeof\(MoaderLogHeader\)\s*==\s*80\)/.test(header)&&
  /#define MOADER_LOG_PATH0\s+"\/moader\/runtime\/log0\.bin"/.test(header)&&
  /#define MOADER_LOG_PATH1\s+"\/moader\/runtime\/log1\.bin"/.test(header),
  'Native log snapshot contract changed; review installer capacity/header/paths before preparation');
}
function initialRuntimeMetadata(catalogue){
 check(Buffer.isBuffer(catalogue)&&catalogue.length===64,'Initial catalogue must keep its 64-byte header ABI');
 return METADATA_TARGETS.map(target=>({target,data:target==='/moader/runtime/catalogue.bin'?Buffer.from(catalogue):Buffer.alloc(METADATA_SIZES[target])}));
}
function assertMoaderMetadata(entries){
 check(Array.isArray(entries)&&entries.length===METADATA_TARGETS.length,'Moader requires the exact catalogue/ack/log0/log1 metadata set');
 const seen=new Set();
 for(const entry of entries){
  check(entry&&Object.hasOwn(METADATA_SIZES,entry.target)&&!seen.has(entry.target),'Unexpected or duplicate required Moader metadata target');seen.add(entry.target);
  check(Buffer.isBuffer(entry.data)&&entry.data.length===METADATA_SIZES[entry.target],'Required Moader metadata size differs: '+entry.target);
  if(entry.target!=='/moader/runtime/catalogue.bin')check(entry.data.every(byte=>byte===0),'Initial receipt/log snapshot must be all zero: '+entry.target);
 }
 check(METADATA_TARGETS.every(target=>seen.has(target)),'Missing required Moader metadata target');
}
async function validateMoaderPrepared(manifest){
 const frozen=await validatePrepared(manifest);await assertLogWireContract();
 check(frozen.programs.length===PROGRAM_TARGETS.length&&PROGRAM_TARGETS.every(target=>frozen.programs.some(entry=>entry.target===target)),
  'Moader requires both fixed application/resident program targets');
 assertMoaderMetadata(frozen.metadata);return frozen;
}
function validateMetadataReadback(expected,actual){
 check(Array.isArray(expected)&&expected.length===METADATA_TARGETS.length&&Array.isArray(actual)&&actual.length===METADATA_TARGETS.length,
  'Missing required metadata readback receipts');
 const required=new Map(expected.map(entry=>[entry.target,entry])),seen=new Set();
 check(required.size===METADATA_TARGETS.length&&METADATA_TARGETS.every(target=>required.has(target)),'Invalid expected metadata readback target set');
 for(const receipt of actual){
  const entry=required.get(receipt?.target);
  check(entry&&!seen.has(receipt.target),'Unexpected or duplicate metadata readback target');seen.add(receipt.target);
  check(receipt.readbackVerified===true&&receipt.bytes===METADATA_SIZES[receipt.target]&&receipt.bytes===entry.bytes&&
   typeof entry.sha256==='string'&&/^[a-f0-9]{64}$/.test(entry.sha256)&&receipt.sha256===entry.sha256,
   'Required metadata readback differs: '+receipt.target);
 }
}

async function readInput(item,qualified=false){
 check(item&&typeof item.source==='string'&&path.isAbsolute(item.source),'Input needs an absolute source path');
 const stat=await fs.stat(item.source);
 check(stat.isFile()&&stat.size>0&&stat.size<=MAX,'Input must be a nonempty regular file at most 32 MiB: '+item.source);
 const data=await fs.readFile(item.source);
 check(data.length>0&&data.length<=MAX,'Input size changed: '+item.source);
 if(qualified){
  check(Number.isSafeInteger(item.bytes)&&item.bytes===data.length&&typeof item.sha256==='string'&&/^[a-f0-9]{64}$/.test(item.sha256),
   'Invalid qualified input size or digest: '+item.source);
  check(digest(data)===item.sha256,'Input changed after preparation: '+item.source);
 }
 return{...item,data};
}
async function validateEntries(entries,targets){
 check(Array.isArray(entries)&&entries.length<=8,'Invalid input manifest');
 const seen=new Set(),out=[];
 for(const item of entries){
  check(item&&targets.includes(item.target)&&!seen.has(item.target),'Unexpected or duplicate input target');seen.add(item.target);
  out.push(await readInput(item,true));
 }
 return out;
}
async function readExtra(file){
 if(!file)return[];
 return validateEntries(JSON.parse(await fs.readFile(file,'utf8')),PROGRAM_TARGETS);
}

// Read mutable originals once, then publish a unique read-only tree. Build
// jobs can replace dist files without changing this installation's inputs.
async function snapshot({output,kaihen,programs=[],metadata=[],nativeRuntime,hostProfile}){
 const components=[];
 for(const name of COMPONENTS)components.push({...await readInput({source:path.resolve(kaihen,name)}),name});
 const bundled=[];
 for(const entry of await fs.readdir(path.join(kaihen,'programs'),{withFileTypes:true})){
  if(entry.isFile()&&!entry.name.startsWith('.')){
   check(!/[\\/\0]/.test(entry.name),'Invalid bundled program name');
   bundled.push(await readInput({source:path.resolve(kaihen,'programs',entry.name),target:'/switch/'+entry.name}));
  }
 }
 const targets=new Set();
 const extras=[];
 for(const item of programs){
  check(PROGRAM_TARGETS.includes(item.target)&&!targets.has(item.target),'Unexpected or duplicate extra-program target');targets.add(item.target);
  extras.push(await readInput(item,item.sha256!==undefined||item.bytes!==undefined));
 }
 for(const item of bundled){check(!targets.has(item.target),'Duplicate bundled/extra-program target');targets.add(item.target);}
 check(extras.length<=8,'Too many extra programs');
 const meta=[];
 for(const item of metadata){
  check(METADATA_TARGETS.includes(item.target)&&!meta.some(row=>row.target===item.target),'Unexpected or duplicate runtime metadata');
  const value=Buffer.isBuffer(item.data)?{...item,data:Buffer.from(item.data)}:await readInput(item,true);
  check(value.data.length>0&&value.data.length<=MAX,'Invalid metadata size');meta.push(value);
 }
 await fs.mkdir(output,{recursive:true});
 const directory=await fs.mkdtemp(path.join(output,'prepared-')),directories=new Set([directory]);
 async function store(name,data,target){
  const source=path.join(directory,name),parent=path.dirname(source);
  await fs.mkdir(parent,{recursive:true,mode:0o700});
  for(let d=parent;d.startsWith(directory);d=path.dirname(d)){directories.add(d);if(d===directory)break;}
  await fs.writeFile(source,data,{flag:'wx',mode:0o400});
  return{source,...(target?{target}:{}),bytes:data.length,sha256:digest(data)};
 }
 const kaihenFiles=[];
 for(const item of components)kaihenFiles.push(await store('kaihen/'+item.name,item.data));
 const bundledPrograms=[];
 await fs.mkdir(path.join(directory,'kaihen/programs'),{recursive:true,mode:0o700});directories.add(path.join(directory,'kaihen/programs'));
 for(const item of bundled)bundledPrograms.push(await store('kaihen/programs/'+path.basename(item.source),item.data,item.target));
 const frozenPrograms=[];
 for(const [i,item]of extras.entries())frozenPrograms.push(await store('programs/'+i+'-'+path.basename(item.target),item.data,item.target));
 const frozenMetadata=[];
 for(const item of meta)frozenMetadata.push(await store('metadata/'+path.basename(item.target),item.data,item.target));
 const extraFile=path.join(directory,'programs.json'),manifestFile=path.join(directory,'manifest.json');
 await fs.writeFile(extraFile,JSON.stringify(frozenPrograms,null,2)+'\n',{flag:'wx',mode:0o400});
 const manifest={schema:1,preparedAt:new Date().toISOString(),directory,manifestFile,kaihen:path.join(directory,'kaihen'),kaihenFiles,...(nativeRuntime?{nativeRuntime}:{}),...(hostProfile?{hostProfile}:{}),
  bundledPrograms,programs:frozenPrograms,metadata:frozenMetadata,extraFile,consoleTested:false};
 await fs.writeFile(manifestFile,JSON.stringify(manifest,null,2)+'\n',{flag:'wx',mode:0o400});
 for(const name of [...directories].sort((a,b)=>b.length-a.length))await fs.chmod(name,0o500);
 return manifest;
}
async function validatePrepared(manifest){
 check(manifest?.schema===1&&Array.isArray(manifest.kaihenFiles)&&manifest.kaihenFiles.length===COMPONENTS.length,'Invalid prepared manifest');
 const components=new Set(COMPONENTS.map(name=>path.join(manifest.kaihen,name)));
 for(const item of manifest.kaihenFiles){check(components.delete(item.source),'Unexpected or duplicate kaiHEN component');await readInput(item,true);}
 const programs=await validateEntries(manifest.programs,PROGRAM_TARGETS),metadata=await validateEntries(manifest.metadata,METADATA_TARGETS);
 const extra=await readExtra(manifest.extraFile);
 check(JSON.stringify(extra.map(({data,...item})=>item))===JSON.stringify(programs.map(({data,...item})=>item)),'Extra manifest differs from prepared programs');
 for(const item of manifest.bundledPrograms||[]){check(item.target==='/switch/'+path.basename(item.source),'Invalid bundled program target');await readInput(item,true);}
 return{programs,metadata};
}
module.exports={snapshot,validatePrepared,validateMoaderPrepared,assertLogWireContract,initialRuntimeMetadata,assertMoaderMetadata,validateMetadataReadback,
 validateEntries,readInput,readExtra,digest,PROGRAM_TARGETS,METADATA_TARGETS,METADATA_SIZES,LOG_SNAPSHOT_CAPACITY};
