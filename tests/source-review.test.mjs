import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {SourceClient} from '../host/source-client.mjs';
import {startHubServer} from '../host/hub-server.mjs';
import {SessionHub} from '../host/session-hub.mjs';
import {AttentionModel} from '../host/attention.mjs';
import {registerWatcher} from '../host/pi-observer.mjs';
const id='a'.repeat(64),item='b'.repeat(64);
function observerFixture(){
 const hooks=new Map(),calls=[];
 const client={register:()=>true,start(){},status(){},supervisorSync(){},attention(...a){calls.push(['attention',...a]);return true;},uncertain(...a){calls.push(['unknown',...a]);return true;},resolve(...a){calls.push(['resolve',...a]);return true;},close(){},reconnect(){}};
 const pi={on(name,fn){hooks.set(name,fn);return()=>hooks.delete(name);},events:{on(){return()=>{};},emit(){}},registerCommand(){}};
 const observer=registerWatcher(pi,{isChild:false,clientFactory:()=>client});
 const fire=(event,value={})=>hooks.get(event)?.(value,{sessionManager:{getSessionId:()=> 'synthetic-root'}});
 fire('session_start');return {fire,calls,observer};
}
test('new explicit prompt after a source gap gets a fresh identity, not an old ACK',()=>{
 const {fire,calls,observer}=observerFixture();
 try{
  fire('ui_prompt_start',{kind:'input'});fire('ui_prompt_end',{kind:'confirm'});fire('ui_prompt_start',{kind:'input'});
  const starts=calls.filter(c=>c[0]==='attention');assert.equal(starts.length,2);assert.notEqual(starts[0][2].itemId,starts[1][2].itemId);
  assert.ok(calls.some(c=>c[0]==='unknown'&&c[2]===starts[0][2].itemId));
  assert.equal(calls.some(c=>c[0]==='resolve'&&c[2]===starts[0][2].itemId),false);
 }finally{observer.dispose();}
});
test('explicit failure emits once; next run and orderly root shutdown retire owned episodes',()=>{
 const {fire,calls,observer}=observerFixture();
 fire('message_end',{message:{role:'assistant',stopReason:'error'}});fire('agent_settled');fire('agent_settled');
 const failures=calls.filter(c=>c[0]==='attention');assert.equal(failures.length,1);assert.equal(failures[0][2].kind,'failure');
 fire('agent_start');assert.ok(calls.some(c=>c[0]==='resolve'&&c[2]===failures[0][2].itemId));
 fire('ui_prompt_start',{kind:'input'});const input=calls.filter(c=>c[0]==='attention').at(-1)[2].itemId;
 fire('session_shutdown');assert.ok(calls.some(c=>c[0]==='resolve'&&c[2]===input));observer.dispose();
});
test('retired-item semantic rejection does not tear down ingress or conceal incomplete coverage',()=>{
 const model=new AttentionModel(),hub=new SessionHub(model),source=hub.attach();let seq=0;
 const send=(op,fields={})=>source.receive({v:1,seq:++seq,op,...fields},0);
 assert.ok(send('hello',{id}));assert.ok(send('resolve',{item}));
 assert.ok(send('attention',{item,kind:'child'}));assert.ok(send('heartbeat'));
 assert.ok(send('sync',{synced:true}));assert.equal(model.snapshot().sessions[0].supervisorSynced,false);
 assert.equal(model.snapshot().sessions[0].attentionCount,0);assert.equal(source.closed,false);
 assert.equal(send('attention',{item,kind:'invalid'}),false);
});
test('automatic source reconnect survives more than four daemon generations',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'wh-'));let hub;
 const client=new SourceClient(id,join(dir,'watcher.sock'),{retryDelays:[0,1,2,5]});
 t.after(async()=>{client.close();if(hub)await hub.close();rmSync(dir,{recursive:true,force:true});});
 for(let generation=0;generation<6;generation++){
  let resolve,reject;const seen=new Promise((yes,no)=>{resolve=yes;reject=no;});
  const timeout=setTimeout(()=>reject(new Error('Reconnect deadline')),2000);
  class Model extends AttentionModel {register(...args){const result=super.register(...args);if(result){clearTimeout(timeout);resolve();}return result;}}
  hub=await startHubServer(new Model(),dir,()=>0);if(generation===0)client.start();
  await seen;await hub.close();hub=undefined;
 }
});
