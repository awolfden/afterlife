import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {AttentionModel} from '../host/attention.mjs';
import {SessionHub} from '../host/session-hub.mjs';
import {displayTitle,validIdentity} from '../host/session-identity.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {crc32} from '../host/wire.mjs';
import {runHost} from '../host/run.mjs';
const id=n=>n.toString(16).padStart(64,'0');
test('production oldest-first selection permits ACK of every unseen item without resolving pending work',()=>{
 const model=new AttentionModel({autoFocus:true});model.register(id(1));model.time(1);
 model.attention(id(1),{itemId:id(10),kind:'child'});assert.ok(model.acknowledge(id(1),id(10)));
 model.time(2);model.attention(id(1),{itemId:id(11),kind:'input'});
 assert.equal(model.snapshot().sessions[0].selected.id,id(11));assert.equal(model.snapshot().sessions[0].state,'needs-input');
 assert.equal(model.acknowledge(id(1),id(10)),false);assert.ok(model.acknowledge(id(1),id(11)));
 assert.equal(model.snapshot().sessions[0].pendingCount,2);assert.equal(model.snapshot().sessions[0].unacknowledgedCount,0);
});
test('malformed optional identity is discarded without severing subsequent alert ingress',()=>{
 const model=new AttentionModel(),stream=new SessionHub(model).attach();
 assert.ok(stream.receive({v:1,seq:1,op:'hello',id:id(1)},0));
 assert.ok(stream.receive({v:1,seq:2,op:'identity',identity:{spaceId:'bad',spaceTitle:'bad\n'}},0));
 assert.ok(stream.receive({v:1,seq:3,op:'attention',item:id(10),kind:'input'},0));
 assert.equal(stream.closed,false);assert.equal(model.snapshot().sessions[0].pendingCount,1);assert.equal(model.snapshot().sessions[0].label,'Session 1');
});
test('surrogates rejected and fallback labels stable across late grouping metadata',()=>{
 assert.equal(displayTitle('a\ud800b'),'a b');assert.equal(validIdentity({spaceId:id(10),spaceTitle:'a\ud800b'}),false);
 const model=new AttentionModel();for(let i=1;i<=3;i++)model.register(id(i));
 model.identity(id(1),{spaceId:id(10),spaceTitle:'Space'});model.identity(id(3),{spaceId:id(10),spaceTitle:'Space'});
 assert.deepEqual(model.snapshot().sessions.map(s=>s.id),[id(1),id(3),id(2)]);
 assert.equal(model.snapshot().sessions.find(s=>s.id===id(2)).label,'Session 2');
});
test('G1 capability gates rows and preserves exact frame-bound ACKs',t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-review-')),ledger=new DeliveryLedger(directory);
 t.after(()=>{ledger.close();rmSync(directory,{recursive:true,force:true});});
 const model=new AttentionModel({autoFocus:true});model.register(id(1));model.status(id(1),{state:'idle'});
 const controller=new UsbController(model,ledger);
 const report=(seq,cap,button=0)=>{const prefix=`D2 99 ${seq} ${seq} 0 ${button?1:0} ${button} ${button?seq:0} ${cap} `;return prefix+crc32(prefix);};
 controller.observe(report(0,0),0);assert.doesNotMatch(controller.tick(1),/G1/);
 controller.observe(report(1,8),2);model.attention(id(1),{itemId:id(10),kind:'input'});
 const lines=controller.tick(3).trim().split('\n');assert.equal(lines.length,3);assert.match(lines[0],/^W1 /);assert.match(lines[1],/^K1 99 2 2 2 /);assert.match(lines[2],/^G1 99 2 /);
 controller.observe(report(2,8,3),4);assert.equal(ledger.acknowledged(id(1),id(10)),true);assert.equal(model.snapshot().sessions[0].pendingCount,1);
});
test('host identity options reject ambiguous, malformed and IPC-only device selections before touching files',async()=>{
 const deviceSerial=Array(6).fill('ab').join(':'); // Synthetic, not an owner's hardware identity.
 for(const options of [{port:'/dev/cu.usbmodem1',deviceSerial,python:'/unused'},{deviceSerial:'*',python:'/unused'},{deviceSerial,ipcOnly:true}])await assert.rejects(runHost({directory:'/unused',...options}));
});
