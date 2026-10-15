import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,writeFileSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {AttentionModel} from '../host/attention.mjs';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {DeskSettings} from '../host/desk-settings.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {crc32,deviceStatus} from '../host/wire.mjs';
import {settingsLine,settingsPacket} from '../host/settings-sync.mjs';
import {runHost} from '../host/run.mjs';
const id=n=>n.toString(16).padStart(64,'0');
const status=seq=>{const p=`D2 7 ${seq} ${seq} 0 0 0 0 13 `;return p+crc32(p);};
const fields=wire=>wire.split('\n')[0].split(' ').slice(1).map(Number);
function fixture(t){const directory=mkdtempSync(join(tmpdir(),'settings-controller-')),ledger=new DeliveryLedger(directory),settings=new DeskSettings(directory,ledger),model=new AttentionModel({autoFocus:true});
 t.after(()=>{ledger.close();rmSync(directory,{recursive:true,force:true});});for(let n=1;n<=2;n++){model.register(id(n));model.status(id(n),{state:'working'});model.supervisorSync(id(n),true);}
 const controller=new UsbController(model,ledger,{settings});controller.observe(status(0),0);controller.tick(1);controller.observe(status(1),2);return {directory,ledger,settings,model,controller};}
function handshake(controller){
 assert.equal(controller.observeSettings(settingsLine('S0',[7,1,1]).trimEnd(),3),true);
 const wire=controller.tick(4),echo=settingsPacket(wire.split('\n').find(l=>l.startsWith('S1 ')));assert.ok(echo);assert.equal(echo.seq,2);controller.observe(status(2),5);return echo;
}
test('new host suppresses output before settings negotiation without blocking metadata',t=>{
 const {controller,model,ledger}=fixture(t);model.attention(id(2),{itemId:id(50),kind:'input'});const packet=fields(controller.tick(3));assert.equal(packet[8],0);assert.equal(packet[2],0);
 assert.equal(ledger.snapshot().notifications[0].status,'suppressed');assert.equal(controller.status().ready,true);
});
test('interaction gate holds attention until current epoch explicitly reports idle',t=>{
 const {controller,model}=fixture(t),echo=handshake(controller);model.attention(id(2),{itemId:id(50),kind:'input'});assert.equal(model.selection().id,id(1));
 assert.equal(controller.observeSettings(settingsLine('S3',[7,2,echo.token,1,1]).trimEnd(),6),true);assert.equal(model.selection().id,id(1));
 assert.equal(controller.observeSettings(settingsLine('S3',[7,2,'f'.repeat(32),2,0]).trimEnd(),7),false);assert.equal(model.selection().id,id(1));
 assert.equal(controller.observeSettings(settingsLine('S3',[7,2,echo.token,2,0]).trimEnd(),8),true);assert.equal(model.selection().id,id(2));
 assert.equal(controller.observeSettings(settingsLine('S3',[7,2,echo.token,3,1]).trimEnd(),2000),false);assert.equal(controller.readyAt(2000),false);
});
test('disconnect closes focus before releasing interaction and restoration baselines pending events',t=>{
 const {controller,model}=fixture(t),echo=handshake(controller);
 controller.observeSettings(settingsLine('S3',[7,2,echo.token,1,1]).trimEnd(),6);
 model.attention(id(2),{itemId:id(70),kind:'input'});assert.equal(model.selection().id,id(1));
 controller.disconnected();assert.equal(model.selection().id,id(1));
 const wire=controller.tick(7),seq=fields(wire)[1];controller.observe(status(seq),8);assert.equal(model.selection().id,id(1));
 controller.observeSettings(settingsLine('S0',[7,seq,1]).trimEnd(),9);
 const restored=controller.tick(10);const token=settingsPacket(restored.split('\n').find(l=>l.startsWith('S1 '))).token;const next=fields(restored)[1];
 controller.observe(status(next),11);controller.observeSettings(settingsLine('S3',[7,next,token,1,0]).trimEnd(),12);
 assert.equal(model.selection().id,id(1));model.attention(id(2),{itemId:id(71),kind:'input'});assert.equal(model.selection().id,id(2));
});
test('settings renegotiation suppresses ineligible disconnected backlog without focus or later replay',t=>{
 const {controller,model,ledger}=fixture(t),echo=handshake(controller);
 controller.observeSettings(settingsLine('S3',[7,2,echo.token,1,0]).trimEnd(),6);
 controller.disconnected();model.attention(id(2),{itemId:id(73),kind:'input'});
 const discovery=fields(controller.tick(7));assert.equal(discovery[8],0);assert.equal(ledger.snapshot().notifications.length,0);
 controller.observe(status(discovery[1]),8);assert.equal(model.selection().id,id(1));
 controller.observeSettings(settingsLine('S0',[7,discovery[1],1]).trimEnd(),9);
 const restored=controller.tick(10),frame=fields(restored),settings=settingsPacket(restored.split('\n').find(l=>l.startsWith('S1 ')));
 assert.equal(frame[8],0);assert.equal(ledger.snapshot().notifications.length,1);assert.equal(ledger.snapshot().notifications[0].status,'suppressed');assert.equal(model.selection().id,id(1));
 controller.observe(status(frame[1]),11);controller.observeSettings(settingsLine('S3',[7,frame[1],settings.token,1,0]).trimEnd(),12);
 assert.equal(fields(controller.tick(13))[8],0);assert.equal(model.selection().id,id(1));assert.equal(ledger.snapshot().notifications.length,1);
 model.attention(id(2),{itemId:id(74),kind:'input'});assert.equal(model.selection().id,id(2));assert.equal(fields(controller.tick(14))[8],1);assert.equal(ledger.snapshot().notifications[1].status,'claimed');
});
test('post-readiness attention waits for the first valid interaction witness',t=>{
 const {controller,model}=fixture(t);model.attention(id(2),{itemId:id(72),kind:'input'});assert.equal(model.selection().id,id(1));
 const echo=handshake(controller);assert.equal(model.selection().id,id(1));
 controller.observeSettings(settingsLine('S3',[7,2,echo.token,1,0]).trimEnd(),6);assert.equal(model.selection().id,id(2));
});
test('global mute from local Settings is acknowledged with rendered seq zero',t=>{
 const {controller,ledger,model}=fixture(t),echo=handshake(controller);
 controller.observeSettings(settingsLine('S3',[7,2,echo.token,1,1]).trimEnd(),6);
 for(const kind of [1,2,3]){const p=`D2 7 2 0 0 1 ${kind} 2 13 `;assert.equal(deviceStatus(p+crc32(p)),null);}
 const future='D2 7 2 0 0 1 4 3 13 ';assert.equal(deviceStatus(future+crc32(future)),null);
 const prefix='D2 7 2 0 0 1 4 2 13 ',packet=prefix+crc32(prefix);
 assert.equal(controller.observe(packet,7),true);assert.equal(ledger.muted,true);assert.equal(model.snapshot().muted,true);
 const next=fields(controller.tick(8));assert.equal(next[9],1);assert.ok(next[7]&1);
 controller.observe(packet,9);assert.equal(ledger.muted,true); // Retransmission must not toggle twice.
});
test('disabled outputs consume attention; re-enabling and restore cannot replay it',t=>{
 const {controller,model,settings,ledger}=fixture(t),echo=handshake(controller);
 controller.observeSettings(settingsLine('S3',[7,2,echo.token,1,0]).trimEnd(),6);
 assert.equal(controller.observeSettings(settingsLine('S2',[7,2,echo.token,1,1,4,160]).trimEnd(),7),true);assert.equal(settings.snapshot().vibration,false);
 model.attention(id(2),{itemId:id(51),kind:'input'});assert.equal(fields(controller.tick(8))[8],0);assert.equal(ledger.snapshot().notifications[0].status,'suppressed');
 controller.observe(status(3),9);assert.equal(controller.observeSettings(settingsLine('S2',[7,3,echo.token,2,2,7,160]).trimEnd(),10),true);assert.equal(fields(controller.tick(11))[8],0);
});
test('sound-only preferences retain notification delivery without enabling a motor preference',t=>{
 const {controller,model,settings}=fixture(t),echo=handshake(controller);controller.observeSettings(settingsLine('S3',[7,2,echo.token,1,0]).trimEnd(),6);
 controller.observeSettings(settingsLine('S2',[7,2,echo.token,1,1,6,160]).trimEnd(),7);assert.equal(settings.snapshot().vibration,false);assert.equal(settings.snapshot().sounds,true);
 model.attention(id(2),{itemId:id(52),kind:'input'});assert.equal(fields(controller.tick(8))[8],1);
});
test('retired completion option rejects programmatic callers even when false',async()=>{
 for(const completionChime of [false,true]){
  assert.throws(()=>new UsbController(new AttentionModel(),{}, {completionChime}),/completionChime is retired/);
  await assert.rejects(runHost({completionChime}),/completionChime is retired/);
 }
});
test('invalid persisted preferences fail startup and release only the acquired ledger lock',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'settings-startup-'));t.after(()=>rmSync(directory,{recursive:true,force:true}));writeFileSync(join(directory,'settings.json'),'{}',{mode:0o600});
 await assert.rejects(runHost({directory,ipcOnly:true}),/Invalid settings/);assert.equal(existsSync(join(directory,'delivery.lock')),false);assert.equal(existsSync(join(directory,'settings.json')),true);
});
