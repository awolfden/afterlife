import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync,rmSync,writeFileSync,symlinkSync,readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { AttentionModel } from '../host/attention.mjs';
import { DeliveryLedger } from '../host/delivery-ledger.mjs';
import { UsbController } from '../host/usb-controller.mjs';
import { crc32,deviceStatus,encodeFrame } from '../host/wire.mjs';
const id=n=>n.toString(16).padStart(64,'0');
function fixture(t,options){const dir=mkdtempSync(join(tmpdir(),'watcher-ledger-test-'));const ledger=new DeliveryLedger(dir);t.after(()=>{ledger.close();rmSync(dir,{recursive:true,force:true});});const model=new AttentionModel(options);for(let n=1;n<=3;n++){model.register(id(n));model.status(id(n),{state:'idle'});model.supervisorSync(id(n),true);}return {dir,ledger,model,controller:new UsbController(model,ledger)};}
function status({boot=99,seq=0,rendered=seq,alert=0,buttonId=0,buttonKind=0,buttonFrame=0,capabilities=1}={}){const p=`D2 ${boot} ${seq} ${rendered} ${alert} ${buttonId} ${buttonKind} ${buttonFrame} ${capabilities} `;return p+crc32(p);}
function packet(line){const v=line.trim().split(' ').slice(1).map(Number);return {boot:v[0],seq:v[1],selected:v[2],count:v[3],state:v[4],pending:v[5],other:v[6],flags:v[7],alert:v[8],eventAck:v[9]};}
function connect(controller,at=0,boot=99){assert.equal(controller.observe(status({boot}),at),true);const frame=packet(controller.tick(at+1));controller.observe(status({boot,seq:frame.seq}),at+2);assert.equal(controller.status().ready,true);}

test('controller exposes no retired voice authority',t=>{
 const {controller}=fixture(t);assert.equal(controller.voiceFrame,undefined);connect(controller);assert.equal(controller.voiceFrame,undefined);
});
test('paging rejects selection away/back ABA and accepts a fresh origin without acknowledging',t=>{
 const {model,ledger,controller}=fixture(t);connect(controller);
 const old=packet(controller.tick(3));model.page(1);model.page(-1);
 controller.observe(status({seq:old.seq,buttonId:1,buttonKind:2,buttonFrame:old.seq}),4);
 assert.equal(model.snapshot().selectedSessionId,id(1));assert.equal(controller.status().buttonAck,1);
 const fresh=packet(controller.tick(5));controller.observe(status({seq:fresh.seq,buttonId:2,buttonKind:2,buttonFrame:fresh.seq}),6);
 assert.equal(model.snapshot().selectedSessionId,id(2));assert.equal(ledger.snapshot().acks.length,0);
});
test('paging rejects roster replacement and same-ID lifetime ABA, consumes once, and accepts a fresh release',t=>{
 const {model,ledger,controller}=fixture(t);connect(controller);
 const old=packet(controller.tick(3));model.remove(id(3));model.register(id(3));
 const stale=status({seq:old.seq,buttonId:1,buttonKind:2,buttonFrame:old.seq});
 controller.observe(stale,4);assert.equal(model.snapshot().selectedSessionId,id(1));assert.equal(controller.status().buttonAck,1);
 const fresh=packet(controller.tick(5));const release=status({seq:fresh.seq,buttonId:2,buttonKind:2,buttonFrame:fresh.seq});
 controller.observe(release,6);controller.observe(release,7);assert.equal(model.snapshot().selectedSessionId,id(2));assert.equal(controller.status().buttonAck,2);
 assert.equal(ledger.snapshot().acks.length,0);
});
test('display identity supplements W1 only for ghost-capable peers, without changing authority',t=>{
 const {controller}=fixture(t);controller.observe(status({capabilities:8}),0);
 const lines=controller.tick(1).trim().split('\n');assert.match(lines[0],/^W1 /);assert.match(lines[1],/^K1 99 1 1 1 /);assert.equal(lines.filter(line=>line.startsWith('G1 ')).length,3);
 assert.equal(controller.status().ready,false);
});
test('wire CRC, status strictness and selected-session validation',()=>{
 assert.equal(crc32('123456789'),0xcbf43926);assert.equal(deviceStatus(status()).boot,99);
 assert.equal(deviceStatus(status()+'x'),null);assert.equal(deviceStatus(status({seq:1,buttonKind:3,buttonFrame:1})),null);
 assert.equal(deviceStatus(status({seq:1,buttonKind:3,buttonId:1,buttonFrame:2})),null);
 assert.equal(deviceStatus(status({boot:0})),null);
 assert.throws(()=>encodeFrame({sessions:[{id:id(1)}],selectedSessionId:id(2)},{boot:1,seq:1}));
});
test('no intent claim until two-way transport proof; other-root alert survives an idle page',t=>{
 const {model,ledger,controller}=fixture(t);model.attention(id(2),{itemId:id(21),kind:'input'});
 assert.equal(controller.tick(0),'Q1\n');assert.equal(ledger.snapshot().notifications.length,0);
 controller.observe(status(),1);assert.equal(packet(controller.tick(2)).alert,0);assert.equal(ledger.snapshot().notifications.length,0);
 controller.observe(status({seq:1}),3);const p=packet(controller.tick(4));assert.equal(p.state,0);assert.equal(p.other,1);assert.equal(p.alert,1);assert.equal(ledger.snapshot().notifications[0].status,'claimed');
 controller.observe(status({seq:p.seq,alert:1}),5);assert.equal(ledger.snapshot().notifications[0].status,'accepted');
});
test('disconnected arrival is claimed after restoration without stealing focus when outputs are eligible',t=>{
 const {model,ledger,controller}=fixture(t,{autoFocus:true});connect(controller);
 controller.disconnected();model.attention(id(2),{itemId:id(210),kind:'input'});
 const discovery=packet(controller.tick(3));assert.equal(discovery.alert,0);assert.equal(ledger.snapshot().notifications.length,0);
 controller.observe(status({seq:discovery.seq}),4);assert.equal(model.selection().id,id(1));
 const emitted=packet(controller.tick(5));assert.equal(emitted.alert,1);assert.equal(model.selection().id,id(1));assert.equal(ledger.snapshot().notifications.length,1);assert.equal(ledger.snapshot().notifications[0].status,'claimed');
 controller.observe(status({seq:emitted.seq,alert:emitted.alert}),6);assert.equal(ledger.snapshot().notifications[0].status,'accepted');
 controller.tick(7);assert.equal(controller.status().pending,false);assert.equal(controller.status().queued,0);assert.equal(ledger.snapshot().notifications.length,1);
 model.attention(id(3),{itemId:id(211),kind:'input'});assert.equal(model.selection().id,id(3));
});
test('duplicate device receipts cannot repeat a mute toggle',t=>{
 const {model,ledger,controller}=fixture(t);connect(controller);const p=packet(controller.tick(3));
 const receipt=status({seq:p.seq,buttonId:1,buttonKind:4,buttonFrame:p.seq});controller.observe(receipt,4);controller.observe(receipt,5);
 assert.equal(model.snapshot().muted,true);assert.equal(ledger.muted,true);assert.equal(packet(controller.tick(6)).eventAck,1);
});
test('ACK exact rendered identity persists without resolving the request',t=>{
 const {dir,model,ledger,controller}=fixture(t);connect(controller);model.attention(id(1),{itemId:id(31),kind:'input'});
 const p=packet(controller.tick(3));controller.observe(status({seq:p.seq,buttonId:1,buttonKind:3,buttonFrame:p.seq}),4);
 assert.equal(model.snapshot().sessions[0].pendingCount,1);assert.equal(model.snapshot().sessions[0].selected.acknowledged,true);
 assert.equal(ledger.acknowledged(id(1),id(31)),true);ledger.close();const reopened=new DeliveryLedger(dir);
 try{assert.equal(reopened.acknowledged(id(1),id(31)),true);}finally{reopened.close();}
});
test('stale ACK cannot silence a replacement item or another selected root',t=>{
 const {model,ledger,controller}=fixture(t);connect(controller);model.attention(id(1),{itemId:id(31),kind:'input'});
 const p=packet(controller.tick(3));model.resolve(id(1),id(31));model.attention(id(1),{itemId:id(32),kind:'input'});
 controller.observe(status({seq:p.seq,buttonId:1,buttonKind:3,buttonFrame:p.seq}),4);
 assert.equal(model.snapshot().sessions[0].selected.acknowledged,false);assert.equal(ledger.snapshot().acks.length,0);
 model.page(1);controller.observe(status({seq:p.seq,buttonId:2,buttonKind:1,buttonFrame:p.seq}),5);assert.equal(model.snapshot().selectedSessionId,id(2));
});
test('claimed alerts are not replayed after a device generation change',t=>{
 const {model,ledger,controller}=fixture(t);connect(controller);model.attention(id(1),{itemId:id(31),kind:'input'});
 assert.equal(packet(controller.tick(3)).alert,1);controller.observe(status({boot:100}),4);
 assert.equal(controller.observe(status({boot:99,seq:2,alert:1}),5),false);
 const first=packet(controller.tick(6));controller.observe(status({boot:100,seq:first.seq}),7);
 assert.equal(packet(controller.tick(8)).alert,0);assert.equal(ledger.snapshot().notifications[0].status,'claimed');
});
test('quiet suppression while offline survives restart and later unmute',t=>{
 const {dir,model,ledger,controller}=fixture(t);ledger.mute(true);model.mute(true);
 model.attention(id(1),{itemId:id(41),kind:'child'});controller.tick(10000);assert.equal(ledger.snapshot().notifications.length,0);
 controller.tick(10001);assert.equal(ledger.snapshot().notifications[0].status,'suppressed');
 ledger.close();const reopened=new DeliveryLedger(dir);
 try{reopened.mute(false);assert.deepEqual(reopened.claim([{sessionId:id(1),itemId:id(41)}]),[]);}finally{reopened.close();}
});
test('child grace remains strict and boundary resolution wins',t=>{
 const {model,controller}=fixture(t);connect(controller);model.attention(id(1),{itemId:id(51),kind:'child'});
 for(let now=1000;now<=10000;now+=1000){const p=packet(controller.tick(now));assert.equal(p.alert,0);controller.observe(status({seq:p.seq}),now);}
 model.resolve(id(1),id(51));assert.equal(packet(controller.tick(10002)).alert,0);
});
test('repeated old status does not keep a failed write path alive',t=>{
 const {controller}=fixture(t);connect(controller);controller.tick(1000);controller.observe(status({seq:1}),1500);
 controller.tick(1503);assert.equal(controller.status().ready,false);
});
test('ledger ownership: second writer, symlink, malformed content rejected',t=>{
 const {dir,ledger}=fixture(t);assert.throws(()=>new DeliveryLedger(dir));ledger.close();
 writeFileSync(join(dir,'delivery.json'),'{}',{mode:0o600});assert.throws(()=>new DeliveryLedger(dir));
 rmSync(join(dir,'delivery.json'));symlinkSync('/etc/passwd',join(dir,'delivery.json'));assert.throws(()=>new DeliveryLedger(dir));
});
test('ledger metadata does not contain raw identities and receipts are immutable',t=>{
 const {model,ledger}=fixture(t);model.connected(true);model.attention(id(1),{itemId:id(61),kind:'input'});
 ledger.claim(model.flush());const receipt=model.notificationReceipts();assert.ok(Object.isFrozen(receipt)&&Object.isFrozen(receipt[0]));
 assert.equal(JSON.stringify(ledger.snapshot()).includes(id(61)),false);
});
test('resolution cancels a waiting coalesced alert before it is sent',t=>{
 const {model,controller}=fixture(t);connect(controller);model.attention(id(1),{itemId:id(71),kind:'input'});
 const p=packet(controller.tick(3));controller.observe(status({seq:p.seq,alert:p.alert}),4);
 model.attention(id(2),{itemId:id(72),kind:'input'});assert.equal(packet(controller.tick(5)).alert,1);
 model.resolve(id(2),id(72));assert.equal(packet(controller.tick(300)).alert,1);
});
