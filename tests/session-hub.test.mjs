import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,statSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createConnection} from 'node:net';
import {once} from 'node:events';
import {AttentionModel} from '../host/attention.mjs';
import {SessionHub} from '../host/session-hub.mjs';
import {startHubServer} from '../host/hub-server.mjs';
const id=n=>n.toString(16).padStart(64,'0');
function source(hub,root=1){const connection=hub.attach();let seq=0;const send=(op,fields={},at=0)=>connection.receive({v:1,seq:++seq,op,...fields},at);assert.ok(send('hello',{id:id(root)}));return {connection,send};}
test('three roots stay scoped; unknown fields and agent control methods are rejected',()=>{
 const model=new AttentionModel(),hub=new SessionHub(model);
 const a=source(hub,1),b=source(hub,2);source(hub,3);
 assert.ok(a.send('status',{state:'working',children:2,available:true}));
 assert.ok(b.send('attention',{item:id(12),kind:'child'}));
 assert.equal(model.snapshot().sessions.length,3);assert.equal(model.snapshot().sessions[0].attentionCount,0);
 assert.equal(a.send('approve',{item:id(12)}),false);assert.equal(a.send('status',{state:'idle',children:0,available:true,prompt:'not accepted'}),false);
});
test('disconnect and orderly end retain requests as unknown, not resolved',()=>{
 const model=new AttentionModel(),hub=new SessionHub(model),a=source(hub);
 a.send('status',{state:'working',children:0,available:true});a.send('attention',{item:id(12),kind:'input'});
 a.send('end');const row=model.snapshot().sessions[0];assert.equal(row.state,'unavailable');assert.equal(row.unknownCount,1);assert.equal(row.attentionCount,1);
});
test('fresh owner stream may reconfirm retained requests; simultaneous owner rejected',()=>{
 const model=new AttentionModel(),hub=new SessionHub(model),a=source(hub);
 a.send('attention',{item:id(12),kind:'child'});
 assert.equal(hub.attach().receive({v:1,seq:1,op:'hello',id:id(1)},0),false);
 a.connection.disconnect();const b=source(hub);assert.equal(model.snapshot().sessions[0].unknownCount,1);
 b.send('attention',{item:id(12),kind:'child'});assert.equal(model.snapshot().sessions[0].pendingCount,1);
 b.send('resolve',{item:id(12)});assert.equal(model.snapshot().sessions[0].attentionCount,0);
});
test('grace begins at receive time, not at the previous controller tick',()=>{
 const model=new AttentionModel(),hub=new SessionHub(model),a=source(hub);model.connected(true);
 a.send('status',{state:'working',children:0,available:true},100);
 a.send('attention',{item:id(12),kind:'child'},250);
 model.time(10250);assert.equal(model.flush().length,0);model.time(10251);assert.equal(model.flush().length,1);
});
test('heartbeats have strict order and inactivity does not close a logical session',()=>{
 const model=new AttentionModel(),hub=new SessionHub(model),a=source(hub);
 assert.equal(a.connection.receive({v:1,seq:1,op:'heartbeat'},1),false);
 a.send('heartbeat',{},1000);a.connection.stale(6001);assert.equal(a.connection.closed,true);assert.equal(model.snapshot().sessions.length,1);
});
test('private socket handles fragmentation, one-way metadata and oversize rejection',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'wh-'));const model=new AttentionModel();const hub=await startHubServer(model,dir,()=>0);
 t.after(async()=>{await hub.close();rmSync(dir,{recursive:true,force:true});});assert.equal(statSync(hub.path).mode&0o777,0o600);
 const client=createConnection(hub.path);await once(client,'connect');
 const hello=JSON.stringify({v:1,seq:1,op:'hello',id:id(1)})+'\n';client.write(hello.slice(0,10));client.write(hello.slice(10));
 // End is processed after preceding frames, giving an explicit, deterministic completion barrier.
 client.write(JSON.stringify({v:1,seq:2,op:'attention',item:id(12),kind:'child'})+'\n');
 client.write(JSON.stringify({v:1,seq:3,op:'end'})+'\n');client.resume();await once(client,'close');
 assert.equal(model.snapshot().sessions[0].unknownCount,1);
 const bad=createConnection(hub.path);bad.on('error',()=>{});await once(bad,'connect');bad.write('x'.repeat(2049));await new Promise(resolve=>bad.once('close',resolve));
});
