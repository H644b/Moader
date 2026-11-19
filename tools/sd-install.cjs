// SD staging through an already verified held session. No acquisition here.
'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const project=path.resolve(__dirname,'..');
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const check=(condition,message)=>{if(!condition)throw Error(message);};
async function verifySdFile({s,root,domain,filesystem,target,expected}){
 const {isUncertain}=require('../runtime/modloader/lib/game-files/uncertain.cjs');let uncertain=false;
 const opened=await s.ipc('Open SD for Moader readback',root,18);check(!opened.result,'Cannot open staged SD file for readback');
 const sd=domain.oneHandle(opened);let file;
 try{
  const result=await filesystem.openFile(s,sd,target);check(!result.result,'Cannot reopen '+target);file=result.handle;
  check(await filesystem.fileSize(s,file)===BigInt(expected.length),'Staged size differs: '+target);
  const hash=crypto.createHash('sha256');
  for(let offset=0;offset<expected.length;offset+=0x10000){
   const count=Math.min(0x10000,expected.length-offset),part=await filesystem.readFile(s,file,offset,count);
   check(part.length===count,'Short SD readback: '+target);hash.update(part);
  }
  const actual=hash.digest('hex');check(actual===digest(expected),'Staged bytes differ: '+target);
  return{target,bytes:expected.length,sha256:actual,readbackVerified:true};
 }catch(error){uncertain=isUncertain(error)||!!s.uncertainError;throw error;}
 finally{if(!uncertain){if(file)await s.close(file);await s.close(sd);}}
}
async function stage(run,files){
 // Qualify all inputs before opening an endpoint or staging the first file.
 const inputs=require('./install-inputs.cjs');
 const frozen=await inputs.validateEntries(files,inputs.METADATA_TARGETS);
 process.env.NS2_RUN_DIR=path.resolve(run);
 const hosts=require('./host-session.cjs');
 const profileId=hosts.inferredId(run);
 const context=profileId===hosts.DEFAULT?null:hosts.qualifyRun(run,profileId);
 if(context?.target==='seedsow')check(context.guard?.error===null&&context.loader?.run===path.resolve(run),
  'Seedsow SD staging lost its live source-owned transport');
 else if(context)require('./host-transport.cjs').install(context);
 const engine=context?.runtimeRoot||path.join(project,'research/sources/ns2jb/engine'),load=rel=>context?hosts.load(context,rel):require(path.join(engine,rel));
 const {Session,live}=load('switch2/app/session.cjs'),domain=load('switch2/app/domain.cjs'),filesystem=load('switch2/app/filesystem.cjs');
 const {guardSession,isUncertain}=require('../runtime/modloader/lib/game-files/uncertain.cjs');
 const memory=await load('switch2/tools/fs-memory.cjs').connect();
 const execution=JSON.parse(await fs.readFile(path.join(run,'switch2/ro/current-execution.json')));
 check(execution.verified&&execution.session===memory.info.session&&execution.report===memory.reportFile,'No matching live diagnostic RO for SD staging');
 const s=guardSession(new Session('moader-sd-stage',0x30000));s.record=async()=>{};let uncertain=false;
 try{
  await s.initialize();
  const root=domain.oneHandle(await domain.send(s,'Clone initialized Moader FS root',memory.report.ownedRoot.clones[0],domain.wire.control(2)));
  const context={s,live,save:async()=>{}},state={},out=[];
  for(const item of frozen){
   const bytes=item.data;
   await load('switch2/ro/package.cjs').dispatch(context,{command:'root-native-file-stage',root,target:item.target,source:item.source},state,domain);
   out.push(await verifySdFile({s,root,domain,filesystem,target:item.target,expected:bytes}));
  }
  return out;
 }catch(error){uncertain=isUncertain(error);throw error;}
 finally{if(!uncertain)await s.cleanup();}
}
module.exports={verifySdFile,stage};
