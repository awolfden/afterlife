import test from 'node:test';
import assert from 'node:assert/strict';
import {GhostRoster} from '../host/ghost-roster.mjs';
import {GhostContext,encodeGhostRows} from '../host/ghost-wire.mjs';
import {AttentionModel} from '../host/attention.mjs';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {crc32} from '../host/wire.mjs';
import {ghostCharacter} from '../host/session-identity.mjs';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const id=n=>n.toString(16).padStart(64,'0');
const counts=(roster,ids)=>Array.from({length:6},(_,character)=>ids.filter(id=>roster.get(id)===character).length);
test('six sprites are drawn without replacement and larger rosters remain balanced',()=>{
 for(const pick of [()=>0,n=>n-1]){
  const roster=new GhostRoster(pick),ids=[];
  for(let n=1;n<=16;n++){const previous=ids.map(id=>roster.get(id));ids.push(id(n));roster.sync(ids);assert.deepEqual(ids.slice(0,-1).map(id=>roster.get(id)),previous);const uses=counts(roster,ids);assert.ok(Math.max(...uses)-Math.min(...uses)<=1);if(n<=6)assert.equal(new Set(ids.map(id=>roster.get(id))).size,n);}
 }
});
test('removal restores uniqueness with minimal reassignment and preserves selected copy',()=>{
 const roster=new GhostRoster(()=>0),all=Array.from({length:16},(_,i)=>id(i+1));roster.sync(all);const keep=[all[0],all[6],all[12],all[1],all[7],all[13]],selected=keep[0],before=keep.map(id=>roster.get(id));roster.sync(keep,selected);
 assert.equal(new Set(keep.map(id=>roster.get(id))).size,6);assert.equal(roster.get(selected),before[0]);assert.equal(keep.filter((id,i)=>roster.get(id)!==before[i]).length,4);
 const assigned=keep.map(id=>roster.get(id));roster.sync([...keep].reverse(),selected);assert.deepEqual(keep.map(id=>roster.get(id)),assigned);
 roster.sync([]);assert.equal(roster.get(selected),undefined);
});
test('balanced removal changes no surviving assignment',()=>{
 const roster=new GhostRoster(()=>0),ids=Array.from({length:7},(_,i)=>id(i+1));roster.sync(ids);const before=new Map(ids.map(id=>[id,roster.get(id)]));ids.pop();roster.sync(ids,id(1));assert.equal(new Set(ids.map(id=>roster.get(id))).size,6);for(const id of ids)assert.equal(roster.get(id),before.get(id));
});
test('every deletion boundary preserves balance and released characters are reused',()=>{
 const roster=new GhostRoster(()=>0),ids=Array.from({length:16},(_,i)=>id(i+1));roster.sync(ids);
 while(ids.length){ids.splice(Math.floor(ids.length/2),1);roster.sync(ids);const uses=counts(roster,ids);assert.ok(Math.max(...uses)-Math.min(...uses)<=1);if(ids.length<=6)assert.equal(new Set(ids.map(id=>roster.get(id))).size,ids.length);}
});
test('malformed roster and invalid entropy fail without corrupting assignments',()=>{
 const roster=new GhostRoster(()=>0);roster.sync([id(1)]);for(const bad of [[id(1),id(1)],['bad'],Array.from({length:17},(_,i)=>id(i)),null])assert.throws(()=>roster.sync(bad));assert.equal(roster.get(id(1)),0);
 assert.throws(()=>new GhostRoster(()=>-1).sync([id(1)]));assert.throws(()=>new GhostRoster(()=>0.5).sync([id(1)]));
});
test('controller tick emits unique negotiated G2 characters across six active sessions',t=>{
 const ids=Array.from({length:6},(_,i)=>id(i+1));assert.ok(new Set(ids.map(ghostCharacter)).size<6,'fixture must distinguish the old colliding hash policy');
 const directory=mkdtempSync(join(tmpdir(),'ghost-roster-')),ledger=new DeliveryLedger(directory);t.after(()=>{ledger.close();rmSync(directory,{recursive:true,force:true});});
 const model=new AttentionModel();for(const session of ids){model.register(session);model.status(session,{state:'idle'});}
 const controller=new UsbController(model,ledger),packet=prefix=>prefix+crc32(prefix);
 controller.observe(packet('D2 7 0 0 0 0 0 0 9 '),0);controller.tick(1);controller.observe(packet('D2 7 1 1 0 0 0 0 9 '),2);assert.equal(controller.observeFamily(packet('G0 7 1 2 6 '),3),true);
 const seen=new Map();for(let i=0;i<6;i++){for(const line of controller.tick(4+i).trim().split('\n').filter(line=>line.startsWith('G2 '))){const fields=line.split(' '),index=Number(fields[3]),character=Number(fields[10]);if(seen.has(index))assert.equal(seen.get(index),character);seen.set(index,character);}model.page(1);}
 assert.equal(seen.size,6);assert.equal(new Set(seen.values()).size,6);
});
test('host context sends stable collision-free G2 rows while G1 remains unchanged',()=>{
 const model=new AttentionModel();for(let n=1;n<=6;n++){model.register(id(n));model.status(id(n),{state:'idle'});}
 const context=new GhostContext(),seen=new Map();
 for(let seq=1;seq<=12;seq++){
  const snapshot=model.snapshot(),wire=context.rows(snapshot,{boot:7,seq,family:true});
  for(const line of wire.trim().split('\n')){const parts=line.split(' '),index=Number(parts[3]),character=Number(parts[10]);if(seen.has(index))assert.equal(character,seen.get(index));seen.set(index,character);}
  assert.equal(context.rows(snapshot,{boot:7,seq,family:false}),encodeGhostRows(snapshot,{boot:7,seq,family:false}));model.page(1);
 }
 assert.equal(seen.size,6);assert.equal(new Set(seen.values()).size,6);
});
