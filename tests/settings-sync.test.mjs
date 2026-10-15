import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {join} from 'node:path';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {DeskSettings,SETTINGS_DEFAULTS} from '../host/desk-settings.mjs';
import {SettingsSync,settingsLine,settingsPacket,settingsFlags} from '../host/settings-sync.mjs';
const token='1'.repeat(32),second='2'.repeat(32);
const context=(now=0,seq=10)=>({boot:7,seq,now,ready:true});
const line=(kind,fields)=>settingsLine(kind,fields).trimEnd();
function fixture(t){
 const directory=mkdtempSync(join(tmpdir(),'settings-sync-')),owner=new DeliveryLedger(directory);t.after(()=>{owner.close();rmSync(directory,{recursive:true,force:true});});
 const store=new DeskSettings(directory,owner);let generation=0;
 const sync=new SettingsSync(store,{tokenFactory:()=>++generation===1?token:second});sync.reset(7);
 return {sync,store,owner};
}
const advertise=(sync,now=0,seq=10)=>sync.receive(line('S0',[7,seq,1]),context(now,seq));
const edit=(id=1,base=1,flags=7,t=token,seq=10)=>line('S2',[7,seq,t,id,base,flags,160]);
test('canonical settings wire rejects corrupted, oversized and noncanonical records',()=>{
 const good=line('S2',[7,10,token,1,1,15,240]);assert.equal(settingsPacket(good).values.brightness,240);
 for(const bad of [good+' ',good+'\n',good.replace(' 7 ',' 07 '),good.replace(token,token.toUpperCase().replace('1','A')),good.slice(0,-1),good.replace('S2','S1'),'x'.repeat(192)])assert.equal(settingsPacket(bad),null);
 for(const fields of [[0,10,1],[7,0,1],[7,10,2],[7,10,0x100000000]])assert.throws(()=>settingsLine('S0',fields));
 for(const fields of [[7,10,token,0,1,5,160],[7,10,token,1,1,16,160],[7,10,token,1,1,5,161]])assert.throws(()=>settingsLine('S2',fields));
 assert.equal(settingsFlags(SETTINGS_DEFAULTS),5);
});
test('no advertisement means no extensions; settings never establish readiness',t=>{
 const {sync}=fixture(t);assert.equal(sync.frame(context()),'');
 assert.equal(sync.receive(line('S0',[7,10,1]),{...context(),ready:false}),false);
 assert.equal(sync.receive(line('S0',[7,9,1]),context()),false);
 assert.equal(sync.receive(line('S0',[8,10,1]),context()),false);
 assert.equal(sync.state(0).supported,false);assert.equal(advertise(sync),true);
 const restore=settingsPacket(sync.frame(context()).trimEnd());assert.equal(restore.ack,0);assert.equal(restore.token,token);assert.equal(restore.status,0);
 assert.equal(sync.state(0).held,true);assert.equal(sync.frame(context(249)),'');assert.ok(sync.frame(context(250,11)));
});
test('connected save and retries are idempotent; conflicting/stale/gapped requests do not persist',t=>{
 const {sync,store}=fixture(t);advertise(sync);assert.equal(sync.receive(edit(),context(1)),true);assert.equal(store.snapshot().revision,2);assert.equal(store.snapshot().sounds,true);
 assert.equal(sync.receive(edit(),context(2)),true);assert.equal(store.snapshot().revision,2);
 for(const invalid of [edit(1,1,5),edit(3,2),edit(2,1),edit(2,2,5,second)])assert.equal(sync.receive(invalid,context(3)),false);
 const echo=settingsPacket(sync.frame(context(4),{forced:true}).trimEnd());assert.equal(echo.ack,1);assert.equal(echo.revision,2);assert.equal(echo.forced,true);
 assert.equal(sync.receive(edit(2,2,5),context(5)),true);assert.equal(store.snapshot().revision,3);
});
test('boot/link retirement fences old edits and restores only Mac preferences',t=>{
 const {sync,store}=fixture(t);advertise(sync);sync.receive(edit(),context(1));sync.reset();assert.equal(sync.receive(edit(),context(2)),false);
 sync.reset(7);advertise(sync,3);assert.equal(sync.receive(edit(),context(4)),false);
 const restore=settingsPacket(sync.frame(context(5)).trimEnd());assert.equal(restore.token,second);assert.equal(restore.ack,0);assert.equal(restore.values.sounds,true);assert.equal(store.snapshot().revision,2);
 sync.reset(8);assert.equal(sync.receive(line('S0',[7,10,1]),context(6)),false);
});
test('interaction requires explicit fresh state and rejects replay/conflicts without extending freshness',t=>{
 const {sync}=fixture(t);advertise(sync);const interaction=(id,activity)=>line('S3',[7,10,token,id,activity]);
 assert.equal(sync.state(0).interactionFresh,false);
 assert.equal(sync.receive(interaction(1,1),context(1)),true);assert.equal(sync.state(1).held,true);
 assert.equal(sync.receive(interaction(1,0),context(2)),false);assert.equal(sync.receive(interaction(2,0),context(3)),true);assert.equal(sync.state(3).held,false);
 assert.equal(sync.receive(interaction(1,1),context(4)),false);
 advertise(sync,1504);assert.equal(sync.state(1504).interactionFresh,false);assert.equal(sync.state(1504).held,true);
 assert.equal(sync.receive(interaction(2,0),context(1505)),true);assert.equal(sync.state(1505).held,false);
 assert.equal(sync.receive(interaction(3,1),context(1500)),false);
});
test('actual watch state machine and host/watch wire round trip under sanitizers',()=>{
 const work=mkdtempSync(join(tmpdir(),'watch-settings-'));
 try{
  const binary=join(work,'sync');execFileSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('settings-sync.cpp',import.meta.url)),'-o',binary],{timeout:30000});
  assert.match(execFileSync(binary,[],{encoding:'utf8',timeout:10000}),/watch settings epochs/);
  const restore=settingsLine('S1',[7,10,token,1,0,0,5,160,0]);
  const [request,advertisement,interaction]=execFileSync(binary,['--cross'],{input:restore,encoding:'utf8',timeout:10000}).trimEnd().split('\n').map(settingsPacket);
  assert.deepEqual(advertisement,{kind:'S0',boot:7,seq:10,version:1});
  assert.deepEqual(interaction,{kind:'S3',boot:7,seq:10,token,stateId:1,activity:3});
  assert.deepEqual(request,{kind:'S2',boot:7,seq:10,token,edit:1,baseRevision:1,values:{...SETTINGS_DEFAULTS,sounds:true,brightness:240}});
 }finally{rmSync(work,{recursive:true,force:true});}
});
test('same-boot reset never emits a different token on an already used frame',t=>{
 const {sync}=fixture(t);advertise(sync);assert.ok(sync.frame(context()));sync.reset(7);advertise(sync,250);
 assert.equal(sync.frame(context(250)),'');assert.ok(sync.frame(context(251,11)));
});
test('unavailable owner is advertised as failure, and invalid input never changes interaction state',t=>{
 const {sync,owner}=fixture(t);advertise(sync);const before=sync.state(0);
 assert.equal(sync.receive(line('S3',[7,10,second,1,0]),context()),false);assert.deepEqual(sync.state(0),before);
 assert.equal(sync.receive(line('S1',[7,10,token,1,0,0,5,160,0]),context()),false);assert.deepEqual(sync.state(0),before);
 owner.close();assert.equal(settingsPacket(sync.frame(context()).trimEnd()).status,1);
});
test('save failure echoes failure and old persisted state, never retries writes',()=>{
 let writes=0;const record={version:1,revision:1,...SETTINGS_DEFAULTS};
 const sync=new SettingsSync({failed:false,snapshot:()=>record,update:()=>{writes++;throw Error('private detail');}},{tokenFactory:()=>token});sync.reset(7);advertise(sync);
 assert.equal(sync.receive(edit(),context(1)),true);assert.equal(sync.receive(edit(),context(2)),true);assert.equal(sync.receive(edit(2,1),context(3)),false);
 const echo=settingsPacket(sync.frame(context(4)).trimEnd());assert.equal(echo.status,1);assert.equal(echo.values.sounds,false);assert.equal(echo.ack,1);assert.equal(writes,1);
});
