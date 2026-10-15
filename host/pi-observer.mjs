// Explicit opt-in Pi observer. Metadata excludes prompts/results/paths.
// No reply or agent-control capabilities.
import {createHash,randomUUID} from 'node:crypto';
import {homedir} from 'node:os';
import {join} from 'node:path';
import {performance} from 'node:perf_hooks';
import {SourceClient} from './source-client.mjs';
import {describeHostStatus} from './host-status.mjs';
import {SupervisorAdapter,sessionIdentity} from './supervisor-adapter.mjs';
import {bindSupervisorObservation} from './supervisor-binding.mjs';
import {SnapshotRecovery} from './snapshot-recovery.mjs';
import {bindActivity} from './activity-binding.mjs';
import {herdrIdentity,spaceIdentityEnabled} from './session-identity.mjs';
const notify=(ctx,message,level='info')=>{try{ctx.ui?.notify?.(message,level);}catch{/* A replaced runner cannot be notified. */}};
const kinds=new Set(['select','confirm','input','editor','custom']);
const hash=(...parts)=>createHash('sha256').update(JSON.stringify(['watcher-root-v1',...parts])).digest('hex');
export function registerWatcher(pi,{socketPath=join(homedir(),'.local/share/pi-watcher-pet/run/watcher.sock'),clientFactory=(id,path)=>new SourceClient(id,path),isChild=process.env.PI_SUBAGENT_CHILD==='1',autoStart=true,spaceIdentity=false,identityProvider=herdrIdentity,serverCommand=null}={}) {
 if(isChild)return {dispose(){}};
 let runtime;const off=[];
 const stop=(unwatch=true)=>{if(!runtime)return;const r=runtime;runtime=undefined;r.disposed=true;clearInterval(r.timer);r.activity?.dispose();if(r.ui)r.client.resolve(r.id,r.ui);if(r.failure)r.client.resolve(r.id,r.failure);r.binding?.dispose();r.client.close({unwatch});};
 function start(ctx){
  stop();const owner=ctx.sessionManager.getSessionId(),id=sessionIdentity(owner),client=clientFactory(id,socketPath);
  const r={owner,id,client,instance:randomUUID(),counter:0,state:ctx.isIdle?.()===false?'thinking':'idle',children:0,activityKnown:false,supervisorKnown:false,prompts:new Map(),ui:null,uiUncertain:false,failure:null,lastError:false,lastAborted:false,disposed:false};runtime=r;
  const publish=()=>{if(runtime!==r||r.disposed)return;client.status(id,{state:r.state,activeChildren:r.children,available:true});client.supervisorSync(id,r.activityKnown&&r.supervisorKnown);};
  r.publish=publish;
  const sink={register:ownerId=>client.register(ownerId),supervisorSync:(_id,value)=>{r.supervisorKnown=value;publish();return true;},attention:(...args)=>client.attention(...args),uncertain:(...args)=>client.uncertain(...args),resolve:(...args)=>client.resolve(...args)};
  r.adapter=new SupervisorAdapter(owner,sink);
  r.binding=bindSupervisorObservation(pi.events,r.adapter);
  r.recovery=new SnapshotRecovery(r.adapter,r.binding);
  r.activity=bindActivity(pi.events,owner,value=>{r.activityKnown=value.known;if(value.count!==undefined)r.children=value.count;publish();});
  let identityBusy=false,lastIdentityAt=-Infinity,identityMisses=0;
  const publishIdentity=value=>{
   if(runtime!==r||r.disposed)return;
   if(value){identityMisses=0;client.identity(id,value);}
   else if(++identityMisses>=2)client.identity(id,null);
  };
  const refreshIdentity=async()=>{
   if(!spaceIdentity||typeof client.identity!=='function'||identityBusy||r.disposed||performance.now()-lastIdentityAt<5000)return;
   identityBusy=true;lastIdentityAt=performance.now();
   try{publishIdentity(await identityProvider());}
   catch{publishIdentity(null);}
   finally{identityBusy=false;}
  };
  client.start();publish();void r.activity.refresh();void refreshIdentity();
  r.timer=setInterval(()=>{if(r.disposed)return;try{r.recovery.tick(Math.floor(performance.now()));void r.activity.refresh();void refreshIdentity();}catch{r.adapter.unavailable();}},1000);r.timer.unref?.();
 }
 const on=(name,handler)=>off.push(pi.on(name,(event,ctx)=>{try{handler(event,ctx);}catch{if(runtime){runtime.activityKnown=false;runtime.supervisorKnown=false;runtime.publish();}}}));
 on('session_start',(_event,ctx)=>{stop();if(autoStart)start(ctx);});
 on('session_shutdown',()=>stop());
 on('session_tree',()=>{if(runtime){runtime.adapter.unavailable();runtime.recovery.reset();runtime.binding.refresh();runtime.activityKnown=false;runtime.publish();void runtime.activity.refresh();}});
 on('agent_start',()=>{const r=runtime;if(!r)return;if(r.failure){r.client.resolve(r.id,r.failure);r.failure=null;}r.lastError=false;r.lastAborted=false;r.state='thinking';r.recovery.reset();r.binding.refresh();r.publish();});
 on('turn_start',()=>{if(runtime){runtime.state='thinking';runtime.publish();}});
 on('tool_execution_start',()=>{if(runtime){runtime.state='working';runtime.publish();}});
 on('message_end',event=>{if(runtime&&event.message?.role==='assistant'){runtime.lastError=event.message.stopReason==='error';runtime.lastAborted=event.message.stopReason==='aborted';}});
 on('agent_settled',()=>{const r=runtime;if(!r)return;r.state=r.lastError?'failed':r.lastAborted?'idle':'completed';if(r.lastError&&!r.failure){r.failure=hash(r.owner,r.instance,'failure',++r.counter);r.client.attention(r.id,{itemId:r.failure,kind:'failure'});}r.publish();void r.activity.refresh();});
 on('ui_prompt_start',event=>{
  const r=runtime;if(!r||!kinds.has(event.kind))return;
  // Fresh explicit input gets its own identity after a gap; never reuse an old ACK.
  // The ambiguous earlier item remains retained in the source cache/hub.
  if(r.uiUncertain){r.ui=null;r.prompts.clear();r.uiUncertain=false;}
  const count=r.prompts.get(event.kind)??0;if(count>=64){r.uiUncertain=true;if(r.ui)r.client.uncertain(r.id,r.ui);return;}
  r.prompts.set(event.kind,count+1);
  if(!r.ui){r.ui=hash(r.owner,r.instance,'ui',++r.counter);r.client.attention(r.id,{itemId:r.ui,kind:'input'});}
 });
 on('ui_prompt_end',event=>{
  const r=runtime;if(!r||!kinds.has(event.kind))return;const count=r.prompts.get(event.kind)??0;
  if(!count){if(r.ui){r.uiUncertain=true;r.client.uncertain(r.id,r.ui);}return;}
  if(count===1)r.prompts.delete(event.kind);else r.prompts.set(event.kind,count-1);
  if(!r.prompts.size&&r.ui&&!r.uiUncertain){r.client.resolve(r.id,r.ui);r.ui=null;}
 });
 pi.registerCommand('watch',{
  description:'Watch this session: /watch [on|off|status], /watch server [start|stop|status]',
  getArgumentCompletions:prefix=>{
   const items=['on','off','status',...(serverCommand?['server','server start','server status','server stop']:[])].filter(value=>value.startsWith(prefix)).map(value=>({value,label:value}));
   return items.length?items:null;
  },
  handler:async(args,ctx)=>{
   const action=args.trim()||'on';
   if(action.split(/\s+/)[0]==='server'){
    if(typeof serverCommand==='function')await serverCommand(action.slice(6).trim(),ctx);
    else notify(ctx,'Server controls are unavailable in this watcher configuration.','warning');
    return;
   }
   if(!['on','off','status'].includes(action)){ctx.ui?.notify?.('Usage: /watch [on|off|status]','warning');return;}
   if(action==='off'){stop(true);ctx.ui?.notify?.('Watching stopped for this session.','info');return;}
   if(action==='on'&&!runtime){
    try{start(ctx);}catch{stop();ctx.ui?.notify?.('Watcher could not start. Try /watch again.','error');return;}
   }
   const r=runtime;
   let status=null;
   try{if(r?.client.connected&&typeof r.client.inspect==='function')status=await r.client.inspect();}catch{/* Diagnostics never disrupt watching. */}
   if(r!==runtime)return;
   const message=!r?'Watching is off. Run /watch to opt in.':
    `Watching enabled; ${r.client.connected?`local socket connected (${describeHostStatus(status)})`:'local socket not connected; retrying'}. Child telemetry ${r.activityKnown&&r.supervisorKnown?'synced':'unknown (separate from device connectivity)'}.`;
   ctx.ui?.notify?.(message,'info');
  }
 });
 pi.registerCommand('watcher-reconnect',{description:'Reconnect watcher metadata transport',handler:async(_args,ctx)=>{
  if(!runtime)start(ctx);else{runtime.client.reconnect();runtime.recovery.reset();runtime.binding.refresh();void runtime.activity.refresh();}
  ctx.ui?.notify?.('Watcher reconnect requested. Missing child evidence stays unknown.','info');
 }});
 return Object.freeze({dispose(){stop();for(const unsubscribe of off)unsubscribe?.();}});
}
export default function watcher(pi){registerWatcher(pi,{spaceIdentity:spaceIdentityEnabled()});}
