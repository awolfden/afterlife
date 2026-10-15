import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {AttentionModel} from '../host/attention.mjs';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {crc32} from '../host/wire.mjs';
const id=n=>n.toString(16).padStart(64,'0');
const status=(seq=0,buttonId=0,buttonKind=0,buttonFrame=0)=>{const p=`D2 99 ${seq} ${seq} 0 ${buttonId} ${buttonKind} ${buttonFrame} 1 `;return p+crc32(p);};
function fixture(t,enabled=true){const dir=mkdtempSync(join(tmpdir(),'paging-diagnostic-')),ledger=new DeliveryLedger(dir),model=new AttentionModel();t.after(()=>{ledger.close();rmSync(dir,{recursive:true,force:true});});for(let i=1;i<=3;i++){model.register(id(i));model.status(id(i),{state:'idle'});model.supervisorSync(id(i),true);}const controller=new UsbController(model,ledger,{timingDiagnostics:enabled});controller.observe(status(),0);controller.tick(1);controller.observe(status(1),2);controller.tick(3);return {controller,model,ledger};}
for(const [scenario,key] of [['accepted','applied'],['selected','selectedMismatch'],['aba','selectionEpoch'],['roster','rosterEpoch'],['roster-aba','rosterEpoch'],['identity','rosterIdentity']])test(`anonymous consumed paging classification: ${scenario}`,t=>{
 const {controller:c,model:m,ledger}=fixture(t);
 if(scenario==='selected')m.page(1);
 if(scenario==='aba'){m.page(1);m.page(-1);}
 if(scenario==='identity'){const old=m.rosterEpoch();m.rosterEpoch=()=>old;}
 if(scenario==='roster'||scenario==='identity'){m.remove(id(3));m.register(id(4));}
 if(scenario==='roster-aba'){m.remove(id(3));m.register(id(3));}
 const before=m.snapshot().selectedSessionId;c.observe(status(2,1,2,2),4);c.observe(status(2,1,2,2),5);
 const d=c.pagingDiagnostics();assert.equal(d.observed,1);assert.equal(d[key],1);assert.equal(c.status().buttonAck,1);assert.equal(d.applied,scenario==='accepted'?1:0);
 assert.equal(m.snapshot().selectedSessionId,scenario==='accepted'?id(2):before);assert.equal(ledger.snapshot().acks.length,0);assert.ok(!JSON.stringify(d).includes(id(1)));
 assert.equal(d.observed,d.applied+d.noChange+d.unknownFrame+d.selectedMismatch+d.selectionEpoch+d.rosterEpoch+d.rosterIdentity);
});
test('evicted shown frame is ACKed/rejected, not accepted navigation',t=>{
 const {controller:c,model:m}=fixture(t);let seq=2;
 for(let n=0;n<130;n++){c.tick(5+n*2);c.observe(status(++seq),6+n*2);}
 c.observe(status(seq,1,2,2),266);const d=c.pagingDiagnostics();assert.equal(d.observed,1);assert.equal(d.unknownFrame,1);assert.equal(d.applied,0);assert.equal(c.status().buttonAck,1);assert.equal(m.snapshot().selectedSessionId,id(1));
});
test('anonymous roster generations detect same-count replacement and same-ID lifetime ABA without exposing authority symbols',t=>{
 const {controller:c,model:m}=fixture(t);const first=c.pagingDiagnostics();assert.ok(Number.isInteger(first.rosterGeneration));assert.equal(c.pagingDiagnostics().rosterGeneration,first.rosterGeneration);
 m.remove(id(3));m.register(id(3));const second=c.pagingDiagnostics();assert.equal(second.rosterGeneration,first.rosterGeneration+1);assert.equal(m.snapshot().sessions.length,3);
 m.remove(id(3));m.register(id(4));assert.equal(c.pagingDiagnostics().rosterGeneration,second.rosterGeneration+1);assert.equal(typeof m.rosterEpoch(),'symbol');
});
test('single-session admitted paging is a no-op; diagnostics never establish freshness',t=>{
 const {controller:c,model:m}=fixture(t);m.remove(id(2));m.remove(id(3));c.tick(4);c.observe(status(3,1,2,3),5);assert.equal(c.pagingDiagnostics().noChange,1);assert.equal(c.pagingDiagnostics().applied,0);
 const before=c.pagingDiagnostics().linkGeneration;c.disconnected();assert.equal(c.pagingDiagnostics().linkGeneration,before+1);assert.equal(c.readyAt(6),false);
});
test('diagnostics off/on have identical host authority traces, emitted frames and ledger receipts',t=>{
 const a=fixture(t,false),b=fixture(t,true);assert.equal(a.controller.pagingDiagnostics(),null);
 for(const f of [a,b]){f.model.page(1);f.model.page(-1);f.controller.observe(status(2,1,2,2),4);f.controller.tick(5);f.controller.observe(status(3,2,2,3),6);}
 assert.equal(a.controller.tick(7),b.controller.tick(7));assert.deepEqual(a.model.snapshot(),b.model.snapshot());assert.deepEqual(a.ledger.snapshot(),b.ledger.snapshot());assert.deepEqual(a.controller.status(),b.controller.status());
});
