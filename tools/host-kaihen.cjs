'use strict';
// Moader-owned host compatibility, never acquisition. All modules are qualified
// by host-session before evaluation; target bootstrap bytes remain untouched.
const fs=require('node:fs/promises'),path=require('node:path');
const host=require('./host-session.cjs'),check=(ok,message)=>{if(!ok)throw Error(message);};
function packageBytes(image,policy){
 check(Buffer.isBuffer(image)&&image.length>=4&&image.subarray(0,4).toString()==='NSO0'&&Buffer.isBuffer(policy)&&policy.length>0,'Invalid RO image or signed policy');
 check(image.length<=32*1024*1024&&policy.length<=1024*1024,'RO package input exceeds bound');
 const names=Buffer.from('main\0main.npdm\0'),start=16+48+names.length,policyOffset=Math.ceil(image.length/32)*32;
 const bytes=Buffer.alloc(start+policyOffset+policy.length);bytes.write('PFS0');bytes.writeUInt32LE(2,4);bytes.writeUInt32LE(names.length,8);names.copy(bytes,64);
 for(const [index,offset,data,nameOffset]of [[0,0,image,0],[1,policyOffset,policy,5]]){
  const row=16+index*24;bytes.writeBigUInt64LE(BigInt(offset),row);bytes.writeBigUInt64LE(BigInt(data.length),row+8);bytes.writeUInt32LE(nameOffset,row+16);data.copy(bytes,start+offset);
 }return bytes;
}
async function buildPackage(imageFile,policyFile,output){
 const data=packageBytes(await fs.readFile(imageFile),await fs.readFile(policyFile));await fs.writeFile(output,data,{flag:'wx',mode:0o600});return{file:output,size:data.length};
}
async function launch(context,memory){
 check(context.profile.id==='civ7-s8n','Civ7 launch adapter requires its qualified profile');host.bind(context);
 const relative='switch2/ro/launch.cjs',filename=host.safeFile(context.run,relative),source=await fs.readFile(filename,'utf8');
 check(host.sha(Buffer.from(source))===context.profile.runFiles[relative],'Civ7 launch source changed');
 const dir=path.dirname(filename),prefix=path.basename(context.run)+'-launch-',before=new Set(await fs.readdir(dir));
 // Expose the existing checked operation without changing its body or source
 // file. Upstream main logs its proof but neither exports nor returns it.
 const loaded=host.compile(context,relative,'\nmodule.exports.moaderLaunchMain = main;\n');
 await loaded.moaderLaunchMain();
 const created=(await fs.readdir(dir)).filter(name=>!before.has(name)&&name.startsWith(prefix)&&name.endsWith('.json'));
 check(created.length===1,'Civ7 launch did not create one unique proof');
 const file=host.safeFile(context.run,'switch2/ro/'+created[0]),proof=host.runJson(context.run,'switch2/ro/'+created[0]);
 check(proof.session===path.basename(context.run)&&proof.report===memory.reportFile&&proof.imageAccepted&&proof.restored&&proof.flags===0x10&&/^\d+$/.test(proof.pid),'Civ7 launch proof is incomplete or from another session');
 return{...proof,file};
}
async function activate(context,launchFile,diagnosticPid,memory){
 check(context.profile.id==='civ7-s8n','Civ7 activation adapter requires its qualified profile');
 const launch=host.linkedJson(context.run,launchFile),execution=host.runJson(context.run,'switch2/ro/current-execution.json');
 check(execution.verified&&execution.pid===String(diagnosticPid)&&execution.session===path.basename(context.run)&&execution.report===memory.reportFile,'The diagnostic RO identity differs from the selected run');
 check(launch.session===execution.session&&launch.report===execution.report&&launch.imageAccepted&&launch.restored&&launch.flags===0x10&&/^\d+$/.test(launch.pid)&&BigInt(launch.pid)>1n&&BigInt(diagnosticPid)>1n&&launch.pid!==String(diagnosticPid),'The accepted HEN RO and diagnostic RO must have distinct verified identities');
 const state=await memory.request({command:'root-native-info'});check(state.worker?.joined,'A native worker remains active');
 const shell=state.services.find(row=>row.name==='pm:shell')?.handle,monitor=state.services.find(row=>row.name==='pm:dmnt')?.handle;
 check(shell&&monitor,'Missing owned process-manager sessions');
 const file=path.join(context.run,'switch2/ro','moader-activation-'+Date.now()+'-'+process.pid+'.json');
 const record={session:execution.session,report:execution.report,launch:launchFile,pid:launch.pid,originalPid:String(diagnosticPid),role:'diagnostic-ro',steps:[]};
 await fs.writeFile(file,JSON.stringify(record,null,2)+'\n',{flag:'wx',mode:0o600});
 const save=()=>fs.writeFile(file,JSON.stringify(record,null,2)+'\n',{mode:0o600});
 const ipc=async(label,handle,id,pid)=>{
  const row={label,pending:true};record.steps.push(row);await save();
  const input=Buffer.alloc(8);input.writeBigUInt64LE(BigInt(pid));
  try{
   const result=await memory.request({command:'root-native-ipc',handle,id,hex:input.toString('hex')});
   row.result=result;row.pending=false;await save();check(!result.result&&!result.transport,label+' failed');return result;
  }catch(error){row.error=String(error.stack||error);record.error=row.error;record.nativeOperationUncertain=!!error.nativeRequestUncertain;record.inspectBeforeRetry=true;await save();throw error;}
 };
 for(const [role,pid]of [['new-ro',launch.pid],['diagnostic-ro',diagnosticPid]]){
  const reply=await ipc('Verify '+role,monitor,7,pid),bytes=Buffer.from(reply.hex||'','hex');
  check(bytes.length>=8&&bytes.readBigUInt64LE()===0x0400000000000037n,'PM did not identify '+role+' as the qualified RO program');
 }
 await ipc('Start accepted HEN RO',monitor,1,launch.pid);
 await ipc('Retire identified diagnostic RO',shell,1,diagnosticPid);
 record.complete=true;await save();return{file,pid:launch.pid,originalPid:String(diagnosticPid)};
}
function tryWaitAdapter(address,mask=7){
 check(mask===7&&BigInt(address)>0n,'Unqualified Civ7 pthread tag or try-wait target');
 const bytes=Buffer.alloc(24);[0x927df000,0x58000070,0xd61f0200,0xd503201f].forEach((word,index)=>bytes.writeUInt32LE(word,index*4));bytes.writeBigUInt64LE(BigInt(address),16);return bytes;
}
function validateBootstrapImage(image,text,address){
 const offset=Math.ceil(text.length/16)*16,adapter=tryWaitAdapter(address);
 check(offset+24<=0xff80&&image.length===0x12000&&image.toString('ascii',0x10,0x14)==='NRO0','Invalid Civ7 bootstrap NRO bounds');
 for(const [at,expected]of [[0x18,0x12000],[0x20,0],[0x24,0x10000],[0x28,0x10000],[0x2c,0x1000],[0x30,0x11000],[0x34,0x1000],[0x38,0x1000]])check(image.readUInt32LE(at)===expected,'Civ7 bootstrap NRO segment geometry differs');
 check(image.subarray(0x80,0x80+text.length).equals(text)&&image.subarray(0x80+text.length,0x80+offset).every(byte=>byte===0)&&image.subarray(0x80+offset,0x80+offset+24).equals(adapter),'Civ7 bootstrap prefix, alignment or tagged try-wait adapter differs');return offset;
}
async function startHen(context,image,bootstrap,game,programs){
 const table=host.runJson(context.run,'switch2/app/thread-code.json');
 check(table.pthreadHandle?.form==='tagged'&&table.pthreadHandle.tagMask===7&&table.pthreadHandle.join==='tagged'&&table.pthreadHandle.tryWait==='untagged','Civ7 pthread lifecycle profile differs');
 const {live}=host.load(context,'switch2/app/session.cjs'),frontend=host.load(context,'switch2/ro/nro.cjs'),original=frontend.main,text=await fs.readFile(bootstrap);
 frontend.main=async options=>{
  check(options.label==='hen-startup','Unexpected Civ7 frontend bootstrap operation');validateBootstrapImage(options.image,text,live.sdk+table.functions.tryWait.offset);
  return original(options);
 };
 try{return await host.load(context,'switch2/ro/start-hen.cjs').main(image,bootstrap,game,programs);}
 finally{frontend.main=original;}
}
module.exports={packageBytes,buildPackage,launch,activate,tryWaitAdapter,validateBootstrapImage,startHen};
