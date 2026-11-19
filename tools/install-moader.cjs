#!/usr/bin/env node
'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{spawn}=require('node:child_process');
const project=path.resolve(__dirname,'..'),sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const check=(value,text)=>{if(!value)throw Error(text);};
const inputs=require('./install-inputs.cjs');
const hosts=require('./host-session.cjs');
const usage='Usage: node tools/install-moader.cjs [--host-profile PROFILE] (--check | RUN_DIRECTORY | --prepared MANIFEST RUN_DIRECTORY)';
async function qualifyPrepared(prepared){
 hosts.qualifySources(hosts.preparedProfile(prepared.hostProfile));
 const native=require('../runtime/modloader/lib/native/artifacts.cjs').artifacts();
 check(prepared.nativeRuntime===sha(Buffer.from(JSON.stringify(native.manifest))),'Prepared native runtime differs from the current qualified build; prepare again after integration');
 const frozen=await inputs.validateMoaderPrepared(prepared);
 const build=await inputs.qualifyKaihenBuild(project);inputs.assertKaihenBinding(prepared,build);
 check(frozen.programs.find(item=>item.target==='/switch/Moader.nro').data.subarray(16,20).toString()==='NRO0','Moader application is not an NRO');
 check(frozen.programs.find(item=>item.target==='/kaihen/plugins/moader.nso').data.subarray(0,4).toString()==='NSO0','Moader resident is not an NSO');
 await require('./install-kaihen.cjs').qualifyRelease(prepared);
 return prepared;
}
async function prepare(profileId=hosts.DEFAULT){
 const profile=hosts.qualifySources(hosts.resolveProfile(profileId));
 await inputs.assertLogWireContract();
 const native=require('../runtime/modloader/lib/native/artifacts.cjs').artifacts();
 const kaihen=path.join(project,'vendor/kaihen-mo'),output=path.join(project,'build/install');await fs.mkdir(output,{recursive:true});
 const build=await inputs.qualifyKaihenBuild(project);
 const header=Buffer.alloc(64);header.write('MOADER01');header.writeUInt32LE(1,8);crypto.randomFillSync(header,24,16);
 if(!header.readBigUInt64LE(24)&&!header.readBigUInt64LE(32))header[24]=1;
 header.writeUInt32LE(64,40);header.writeUInt32LE(64,44);
 const manifest=await inputs.snapshot({output,kaihen,kaihenBuild:build,hostProfile:hosts.binding(profile),nativeRuntime:sha(Buffer.from(JSON.stringify(native.manifest))),programs:[
  {source:path.join(project,'dist/Moader.nro'),target:'/switch/Moader.nro'},
  {source:path.join(project,'dist/moader.nso'),target:'/kaihen/plugins/moader.nso'}],metadata:inputs.initialRuntimeMetadata(header)});
 await qualifyPrepared(manifest);
 // This is only a convenience pointer. Native work uses the unique snapshot.
 const latest=path.join(output,'manifest-'+process.pid+'.pending');
 await fs.writeFile(latest,JSON.stringify(manifest,null,2)+'\n',{flag:'wx',mode:0o600});
 await fs.rename(latest,path.join(output,'manifest.json'));return manifest;
}
async function main(args=process.argv.slice(2)){
 if(args.length===1&&args[0]==='--help'){
  console.log(usage);
  console.log('--check prepares installer files on this computer.');
  console.log('For setup instructions, open docs/INSTALLATION.md.');
  return;
 }
 args=[...args];let selected;
 const at=args.indexOf('--host-profile');
 if(at>=0){check(at+1<args.length&&args.indexOf('--host-profile',at+1)<0,'Provide one host profile');selected=args[at+1];hosts.resolveProfile(selected);args.splice(at,2);}
 const fromSnapshot=args[0]==='--prepared';
 check(fromSnapshot?args.length===3:args.length===1,usage);
 const prepared=fromSnapshot?await qualifyPrepared(JSON.parse(await fs.readFile(path.resolve(args[1]),'utf8'))):await prepare(selected);
 const profile=hosts.preparedProfile(prepared.hostProfile);check(!selected||selected===profile.id,'Prepared snapshot host profile differs from --host-profile');
 // install-from-rpc.py reads this line to locate the prepared manifest.
 if(args[0]==='--check'){console.log('Prepared read-only local Moader snapshot: '+prepared.manifestFile+'. No console connection opened.');return;}
 const run=path.resolve(fromSnapshot?args[2]:args[0]),session=path.basename(run);
 check((profile.id==='seedsow-malev0'?/^switch-[A-Za-z0-9]+$/:/^switch2-[A-Za-z0-9]+$/).test(session),'Invalid run directory');
 check(hosts.inferredId(run)===profile.id,'Prepared host profile and selected run differ');
 require('../runtime/modloader/install.cjs').verifiedRun(run);
 const host=profile.id===hosts.DEFAULT?{profile,run,target:'smilebasic',runtimeRoot:profile.sourceDirectory}:
  hosts.qualifyRun(run,profile.id);hosts.bind(host);
 const record={session,started:new Date().toISOString(),status:'preparing',complete:false,manifest:prepared};
 const file=path.join(run,'moader-install.json'),shared=path.join(run,'ns2jb-modloader-install.json');
 await fs.writeFile(file,JSON.stringify(record,null,2)+'\n',{flag:'wx',mode:0o600});
 const save=()=>fs.writeFile(file,JSON.stringify(record,null,2)+'\n',{mode:0o600});
 try{
  await fs.writeFile(shared,JSON.stringify({session,owner:'Moader',journal:file,status:'preparing'})+'\n',{flag:'wx',mode:0o600});
  await inputs.validatePrepared(prepared);
  const entryApp={civ7:'Civilization VII',seedsow:'Seedsow Lullaby Demo',smilebasic:'SmileBASIC'}[profile.target];
  console.log('Keep '+entryApp+' and its session host open while Moader sets up the backend.');
  record.status='installing-filesystem';await save();
  record.runtime=await require('./install-runtime.cjs').install(run,path.join(project,'state',session),prepared,host);
  record.status='starting-kaihen';await save();
  if(profile.id==='seedsow-malev0'){
   // The held producer and installer must keep one process-wide socket and
   // uncertainty latch. A child process would lose both ownership guarantees.
   await require('./install-kaihen.cjs').main([run,prepared.kaihen,prepared.extraFile]);
  }else await new Promise((resolve,reject)=>{
   const child=spawn(process.execPath,[path.join(__dirname,'install-kaihen.cjs'),run,prepared.kaihen,prepared.extraFile],{stdio:'inherit'});
   child.once('error',reject);child.once('exit',(code,signal)=>code===0?resolve():reject(Error('kaiHEN installation stopped: '+(signal||code))));
  });
  record.status='awaiting-device-tests';record.complete=true;record.finished=new Date().toISOString();await save();
  if(profile.target==='seedsow'){
   console.log('Backend installed. Keep Seedsow Lullaby Demo open while the installer checks the running service.');
  }else if(profile.target==='civ7'){
   console.log('Moader setup complete. The CivSwitch handoff to hbmenu still needs a console test.');
  }else{
   console.log('Moader setup complete. When the terminal prompt returns, press HOME and close SmileBASIC.');
   console.log('Open Moader from hbmenu and check the runtime connection in Settings > Advanced.');
  }
 }catch(error){record.status='stopped';record.error=String(error.stack||error);record.inspectBeforeRetry=true;await save();throw error;}
}
module.exports={prepare,qualifyPrepared,main};
if(require.main===module)main().catch(error=>{
 console.error(error.stack);
 console.error('Setup stopped. Keep the error and any moader-install.json report. Follow the recovery guide before trying again.');
 process.exitCode=1;
});
