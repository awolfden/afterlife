import {randomUUID} from 'node:crypto';
// Read-only public RPC: ping + untargeted status only. Never infer terminal outcomes.
export function bindActivity(bus,owner,observe,{timeoutMs=1000}={}) {
 let disposed=false,busy=false,cancel;
 function query(method){return new Promise((resolve,reject)=>{
  const requestId=randomUUID();let off,timer,ended=false;
  const finish=(error,data)=>{if(ended)return;ended=true;clearTimeout(timer);off?.();cancel=undefined;error?reject(error):resolve(data);};
  cancel=()=>finish(new Error('Activity query cancelled'));
  off=bus.on(`subagents:rpc:v1:reply:${requestId}`,reply=>{
   if(reply?.version!==1||reply.requestId!==requestId||reply.method!==method)return;
   finish(reply.success===true?null:new Error('Activity unavailable'),reply.data);
  });
  timer=setTimeout(()=>finish(new Error('Activity timeout')),timeoutMs);timer.unref?.();
  try{bus.emit('subagents:rpc:v1:request',{version:1,requestId,method});}catch(error){finish(error);}
 });}
 async function refresh(){
  if(disposed||busy)return false;busy=true;
  try{
   const ping=await query('ping');if(disposed)return false;
   if(ping?.session?.sessionId!==owner || ping?.capabilities?.fleetStatus?.version!==1)throw new Error('Wrong activity owner/version');
   const data=await query('status');if(disposed)return false;
   const fleet=data?.fleet;
   if(fleet?.version!==1||!Number.isInteger(fleet.totalActive)||fleet.totalActive<0||fleet.totalActive>256||!Number.isInteger(fleet.omitted)||fleet.omitted<0||!Array.isArray(fleet.entries)||fleet.entries.length>16)throw new Error('Invalid activity summary');
   observe({known:fleet.omitted===0,count:fleet.totalActive});return true;
  }catch{if(!disposed)observe({known:false});return false;}
  finally{busy=false;}
 }
 return Object.freeze({refresh,dispose(){if(disposed)return;disposed=true;cancel?.();}});
}
