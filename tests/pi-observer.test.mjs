import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {performance} from 'node:perf_hooks';
import {AttentionModel} from '../host/attention.mjs';
import {SourceClient} from '../host/source-client.mjs';
import {startHubServer} from '../host/hub-server.mjs';
import {registerWatcher} from '../host/pi-observer.mjs';
import {bindActivity} from '../host/activity-binding.mjs';
class ObservedModel extends AttentionModel {
 events=new EventEmitter();
 register(...a){const r=super.register(...a);this.events.emit('change');return r;}
 status(...a){const r=super.status(...a);this.events.emit('change');return r;}
 attention(...a){const r=super.attention(...a);this.events.emit('change');return r;}
 uncertain(...a){const r=super.uncertain(...a);this.events.emit('change');return r;}
 resolve(...a){const r=super.resolve(...a);this.events.emit('change');return r;}
 supervisorSync(...a){const r=super.supervisorSync(...a);this.events.emit('change');return r;}
}
function waitFor(model,predicate){return new Promise((resolve,reject)=>{
 let timer;const check=()=>{if(predicate()){clearTimeout(timer);model.events.off('change',check);resolve();}};
 timer=setTimeout(()=>{model.events.off('change',check);reject(new Error('Metadata integration deadline'));},3000);
 model.events.on('change',check);check();
});}
function fakePi(owner){
 const emitter=new EventEmitter(),hooks=new Map(),requests=[];
 const events={on(name,fn){emitter.on(name,fn);return()=>emitter.off(name,fn);},emit(name,value){emitter.emit(name,value);}};
 events.on('pi-subagents:supervisor-observe:request',q=>events.emit('pi-subagents:supervisor-observe:response',{requestId:q.requestId,apiVersion:1,ownerSessionId:owner,generation:'generation-one',revision:0,historyComplete:true,requests:[]}));
 events.on('subagents:rpc:v1:request',q=>{requests.push(q.method);events.emit(`subagents:rpc:v1:reply:${q.requestId}`,{version:1,requestId:q.requestId,method:q.method,success:true,data:q.method==='ping'?{session:{sessionId:owner},capabilities:{fleetStatus:{version:1}}}:{fleet:{version:1,totalActive:0,omitted:0,entries:[]}}});});
 return {events,requests,on(name,fn){hooks.set(name,fn);return()=>hooks.delete(name);},registerCommand(){},fire(name,event={}){hooks.get(name)?.(event,{sessionManager:{getSessionId:()=>owner}});}};
}
test('three opt-in roots reach private hub; explicit UI lifecycle resolves, completion stays silent',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'wh-')),model=new ObservedModel(),epoch=performance.now(),calls=[];
 const hub=await startHubServer(model,dir,()=>Math.floor(performance.now()-epoch));const disposers=[];
 t.after(async()=>{for(const d of disposers)d.dispose();await hub.close();rmSync(dir,{recursive:true,force:true});});
 const roots=[1,2,3].map(n=>fakePi(`synthetic-owner-${n}`));
 for(const pi of roots){disposers.push(registerWatcher(pi,{socketPath:hub.path,isChild:false,clientFactory:(id,path)=>{
  const client=new SourceClient(id,path);return new Proxy(client,{get(target,key){const value=Reflect.get(target,key);return typeof value==='function'?(...args)=>{calls.push([key,...args]);return value.apply(target,args);}:value;}});
 }}));pi.fire('session_start');}
 await waitFor(model,()=>model.snapshot().sessions.length===3&&model.snapshot().sessions.every(row=>row.supervisorSynced));
 model.connected(true);roots[0].fire('ui_prompt_start',{kind:'confirm',title:'SECRET_SYNTHETIC_TITLE'});
 await waitFor(model,()=>model.snapshot().sessions.some(row=>row.state==='needs-input'));assert.equal(model.flush().length,1);
 roots[0].fire('ui_prompt_end',{kind:'confirm',title:'SECRET_SYNTHETIC_TITLE'});
 await waitFor(model,()=>model.snapshot().sessions.every(row=>row.attentionCount===0));
 roots[0].fire('message_end',{message:{role:'assistant',stopReason:'stop',content:'SECRET_SYNTHETIC_BODY'}});roots[0].fire('agent_settled');
 await waitFor(model,()=>model.snapshot().sessions.some(row=>row.state==='completed'));assert.equal(model.flush().length,0);
 assert.equal(JSON.stringify(calls).includes('SECRET_SYNTHETIC'),false);
 for(const pi of roots)assert.ok(pi.requests.every(method=>['ping','status'].includes(method)));
});
test('unknown cached input is never temporarily replayed as confirmed',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'wh-')),model=new ObservedModel(),id='1'.repeat(64),item='2'.repeat(64);
 const hub=await startHubServer(model,dir,()=>0),client=new SourceClient(id,hub.path);
 t.after(async()=>{client.close();await hub.close();rmSync(dir,{recursive:true,force:true});});
 client.attention(id,{itemId:item,kind:'input'});client.uncertain(id,item);
 let confirmations=0;model.events.on('change',()=>{if(model.snapshot().sessions[0]?.selected?.confirmed)confirmations++;});
 client.start();await waitFor(model,()=>model.snapshot().sessions[0]?.unknownCount===1);
 assert.equal(confirmations,0);assert.equal(model.snapshot().sessions[0].selected.kind,'input');
});
test('child process guard adds no root observer or commands',()=>{
 let registered=0;const result=registerWatcher({on(){registered++;},registerCommand(){registered++;}},{isChild:true});result.dispose();assert.equal(registered,0);
});
test('activity response for a different owner stays unknown and never requests status',async()=>{
 const pi=fakePi('wrong-owner'),values=[];const binding=bindActivity(pi.events,'expected-owner',value=>values.push(value));
 await binding.refresh();binding.dispose();assert.deepEqual(values,[{known:false}]);assert.deepEqual(pi.requests,['ping']);
});
