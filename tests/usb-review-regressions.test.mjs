import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {AttentionModel} from '../host/attention.mjs';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {crc32,deviceStatus} from '../host/wire.mjs';
const id=n=>n.toString(16).padStart(64,'0');
const status=(seq,alert=0,caps=1,rendered=seq)=>{const p=`D2 99 ${seq} ${rendered} ${alert} 0 0 0 ${caps} `;return p+crc32(p);};
const packet=line=>{const p=line.split(' ').map(Number);return {seq:p[2],alert:p[9]};};
function fixture(t,caps=1){const dir=mkdtempSync(join(tmpdir(),'watcher-review-test-'));const ledger=new DeliveryLedger(dir);t.after(()=>{ledger.close();rmSync(dir,{recursive:true,force:true});});const model=new AttentionModel();for(let n=1;n<3;n++){model.register(id(n));model.status(id(n),{state:'idle'});model.supervisorSync(id(n),true);}const controller=new UsbController(model,ledger);controller.observe(status(0,0,caps),0);controller.tick(1);controller.observe(status(1,0,caps),2);return {ledger,model,controller};}
test('durable claim failure leaves model intents waiting and faults transport',t=>{
 const {ledger,model,controller}=fixture(t);
 for(let batch=0;batch<4;batch++)ledger.claim(Array.from({length:1024},(_,i)=>({sessionId:id(1),itemId:id(10000+batch*1024+i)})));
 model.attention(id(1),{itemId:id(50),kind:'input'});
 assert.throws(()=>controller.tick(3),/ledger full/);assert.equal(controller.status().ready,false);assert.match(controller.status().fault,/ledger full/);
 assert.deepEqual(model.notificationReceipts(),[]);assert.throws(()=>controller.tick(4),/ledger full/);
 model.connected(true);assert.equal(model.flush().length,1);
});
test('unsent claimed intent waits through uncertainty and resumes only on confirmation',t=>{
 const {model,controller}=fixture(t);model.attention(id(1),{itemId:id(50),kind:'input'});
 const first=packet(controller.tick(3));controller.observe(status(first.seq,first.alert),4);
 model.attention(id(2),{itemId:id(51),kind:'input'});controller.tick(5);
 model.uncertain(id(2),id(51));assert.equal(packet(controller.tick(300)).alert,1);assert.equal(controller.status().queued,1);
 model.attention(id(2),{itemId:id(51),kind:'input'});assert.equal(packet(controller.tick(301)).alert,2);
});
test('unavailable source pauses unsent intent without a duplicate durable claim',t=>{
 const {model,ledger,controller}=fixture(t);model.attention(id(1),{itemId:id(50),kind:'input'});
 const first=packet(controller.tick(3));controller.observe(status(first.seq,first.alert),4);
 model.attention(id(2),{itemId:id(51),kind:'input'});controller.tick(5);model.status(id(2),{state:'working',available:false});
 assert.equal(packet(controller.tick(300)).alert,1);model.status(id(2),{state:'working',available:true});assert.equal(packet(controller.tick(301)).alert,2);
 assert.equal(ledger.snapshot().notifications.length,2);
});
test('motor-disabled capability consumes intents as suppressed, never claims a vibration',t=>{
 const {model,ledger,controller}=fixture(t,0);model.attention(id(1),{itemId:id(50),kind:'input'});
 const first=packet(controller.tick(3));assert.equal(first.alert,0);assert.equal(ledger.snapshot().notifications[0].status,'suppressed');
 controller.observe(status(first.seq,0,1),4);assert.equal(packet(controller.tick(5)).alert,0);
});
test('acknowledged packets with no alert consumption produce an explicit fault',t=>{
 const {model,controller}=fixture(t);model.attention(id(1),{itemId:id(50),kind:'input'});controller.tick(3);
 const p=packet(controller.tick(1000));controller.observe(status(p.seq,0),1001);
 assert.throws(()=>controller.tick(1504),/acceptance timed out/);assert.equal(controller.status().ready,false);
});
test('rendered sequence is separate from accepted sequence and range checked',()=>{
 const value=deviceStatus(status(5,0,0,3));assert.equal(value.seq,5);assert.equal(value.rendered,3);
 assert.equal(deviceStatus(status(5,0,0,6)),null);assert.equal(deviceStatus(status(5,0,256,3)),null);
 assert.equal(deviceStatus(status(5,0,255,3)).capabilities,255); // Includes network fault/IP diagnostics.
 assert.equal(deviceStatus(status(5,0,48,3)).capabilities,48); // LAN support plus RAM pairing status.
 assert.equal(deviceStatus(status(5,0,8,3)).capabilities,8); // Display-only ghost pages.
 assert.equal(deviceStatus(status(5,0,4,3)).capabilities,4); // Completion-audio capability.
});
