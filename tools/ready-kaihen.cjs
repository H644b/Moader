'use strict';
const {guardSession,isUncertain}=require('../runtime/modloader/lib/game-files/uncertain.cjs');

// Upstream bootstrap helpers also create their own sessions and issue retained
// requests in finally blocks. One child-process-wide latch covers both native
// transports, including those helpers, without editing the pinned sources.
function guardTransports({rpc,endpoint},state={error:null}){
 for(const [object,names]of [[rpc,['request','call','callAddress','callAddress6','pair','read','write','query','log']],[endpoint,['request','read']]]){
  for(const name of names){
   if(typeof object[name]!=='function')continue;
   const operation=object[name].bind(object);
   object[name]=async(...args)=>{
    if(state.error)throw state.error;
    try{return await operation(...args);}catch(error){
     // A malformed/truncated transport reply also leaves dispatch completion
     // unknown. Error codes may carry disconnect identity absent its message.
     if(isUncertain(error)||error instanceof SyntaxError||
       ['ECONNRESET','ECONNABORTED','EPIPE','ETIMEDOUT','ERR_STREAM_PREMATURE_CLOSE'].includes(error?.code)||
       /reply exceeded its limit|^aborted$|socket hang up/i.test(String(error?.message||error))){
      error.nativeRequestUncertain=true;state.error=error;
     }
     throw error;
    }
   };
  }
 }
 return state;
}

// Dependencies are supplied by the installer after its same-session checks.
// Importing this helper never loads connection.json or opens a native endpoint.
async function ready({Session,argumentsWithPid}){
 const session=guardSession(new Session('moader-ro-ready'));
 let uncertain=false;
 try{
  await session.initialize();
  const ro=await session.openService('ldr:ro');
  const reply=await session.ipc('RO Initialize',ro,4,argumentsWithPid(),{pid:true,copies:[0xffff8001]});
  if(reply.result)throw Error('RO Initialize failed: 0x'+reply.result.toString(16));
  return{ready:true};
 }catch(error){uncertain=isUncertain(error)||!!session.uncertainError;throw error;}
 finally{if(!uncertain)await session.cleanup();}
}
module.exports={ready,guardTransports};
