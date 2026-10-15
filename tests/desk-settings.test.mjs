import test from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {DeskSettings,SETTINGS_DEFAULTS,validSettings,validSettingsRecord} from '../host/desk-settings.mjs';
function fixture(t){const directory=fs.mkdtempSync(join(tmpdir(),'desk-settings-'));const owner=new DeliveryLedger(directory);t.after(()=>{owner.close();fs.rmSync(directory,{recursive:true,force:true});});return {directory,owner,path:join(directory,'settings.json')};}
test('settings schema is exact and defaults are independent',()=>{
 assert.equal(validSettings(SETTINGS_DEFAULTS),true);assert.equal(Object.isFrozen(SETTINGS_DEFAULTS),true);
 for(const value of [null,[],{}, {...SETTINGS_DEFAULTS,brightness:0},{...SETTINGS_DEFAULTS,sounds:1},{...SETTINGS_DEFAULTS,extra:false}])assert.equal(validSettings(value),false);
 const record={version:1,revision:1,...SETTINGS_DEFAULTS};assert.equal(validSettingsRecord(record),true);
 for(const value of [{...record,version:2},{...record,revision:0},{...record,revision:0x100000000},{...record,revision:1.5},{...record,extra:1}])assert.equal(validSettingsRecord(value),false);
});
test('settings require the live owner of the exact directory',t=>{
 const f=fixture(t),g=fixture(t);assert.throws(()=>new DeskSettings(f.directory,{}));assert.throws(()=>new DeskSettings(f.directory,g.owner));
 f.owner.close();assert.throws(()=>new DeskSettings(f.directory,f.owner));assert.equal(fs.existsSync(f.path),false);
});
test('defaults persist privately and updates preserve v1 mute and receipts',t=>{
 const f=fixture(t);f.owner.mute(true);f.owner.claim([{sessionId:'a'.repeat(64),itemId:'b'.repeat(64)}]);
 const before=fs.readFileSync(join(f.directory,'delivery.json'));
 const settings=new DeskSettings(f.directory,f.owner);assert.deepEqual(settings.snapshot(),{version:1,revision:1,...SETTINGS_DEFAULTS});
 assert.equal(fs.statSync(f.path).mode&0o777,0o600);
 const values={...SETTINGS_DEFAULTS,sounds:true,vibration:false,brightness:240};assert.equal(settings.update(values).revision,2);
 assert.equal(settings.update(values).revision,2);assert.deepEqual(fs.readFileSync(join(f.directory,'delivery.json')),before);
 assert.deepEqual(new DeskSettings(f.directory,f.owner).snapshot(),{version:1,revision:2,...values});
 f.owner.close();assert.equal(settings.failed,true);assert.throws(()=>settings.update(SETTINGS_DEFAULTS),/unavailable/);
});
for(const mode of ['invalid-json','extra','version','oversized','permission','symlink','dangling','hardlink','directory'])test(`unsafe existing settings rejected without repair: ${mode}`,t=>{
 const f=fixture(t),outside=join(f.directory,'original');
 fs.writeFileSync(outside,JSON.stringify({version:1,revision:1,...SETTINGS_DEFAULTS}),{mode:0o600});
 if(mode==='symlink')fs.symlinkSync(outside,f.path);
 else if(mode==='dangling')fs.symlinkSync(join(f.directory,'missing'),f.path);
 else if(mode==='hardlink')fs.linkSync(outside,f.path);
 else if(mode==='directory')fs.mkdirSync(f.path);
 else fs.writeFileSync(f.path,mode==='invalid-json'?'{':mode==='oversized'?'x'.repeat(1025):JSON.stringify({version:mode==='version'?2:1,revision:1,...SETTINGS_DEFAULTS,...(mode==='extra'?{extra:1}:{})}),{mode:mode==='permission'?0o644:0o600});
 const before=fs.lstatSync(f.path);assert.throws(()=>new DeskSettings(f.directory,f.owner));assert.equal(fs.lstatSync(f.path).ino,before.ino);assert.equal(f.owner.failed,false);
});
for(const failure of ['write','file-sync','rename','directory-sync'])test(`failed ${failure} never advertises saved state or retries`,t=>{
 const f=fixture(t);new DeskSettings(f.directory,f.owner);let syncs=0,writes=0;
 const io={...fs,writeFileSync:(...args)=>{writes++;if(failure==='write')throw Error('private data');return fs.writeFileSync(...args);},fsyncSync:fd=>{syncs++;if((failure==='file-sync'&&syncs===1)||(failure==='directory-sync'&&syncs===2))throw Error('private data');return fs.fsyncSync(fd);},renameSync:(...args)=>{if(failure==='rename')throw Error('private data');return fs.renameSync(...args);}};
 const settings=new DeskSettings(f.directory,f.owner,{io});const original=settings.snapshot();assert.throws(()=>settings.update({...SETTINGS_DEFAULTS,sounds:true}),/Settings save failed/);
 assert.deepEqual(settings.snapshot(),original);assert.equal(settings.failed,true);assert.equal(f.owner.failed,false);
 assert.throws(()=>settings.update({...SETTINGS_DEFAULTS,sounds:true}),/unavailable/);assert.equal(writes,1);
 assert.equal(fs.readdirSync(f.directory).some(n=>n.startsWith('.settings-')),false);
});
test('revision exhaustion refuses wrapping without replacing saved state',t=>{
 const f=fixture(t);fs.writeFileSync(f.path,JSON.stringify({version:1,revision:0xffffffff,...SETTINGS_DEFAULTS}),{mode:0o600});const settings=new DeskSettings(f.directory,f.owner);
 assert.throws(()=>settings.update({...SETTINGS_DEFAULTS,sounds:true}),/exhausted/);assert.equal(settings.snapshot().revision,0xffffffff);
});
