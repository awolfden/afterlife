import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {ghostCharacter} from '../host/session-identity.mjs';
import {sessionIdentity} from '../host/supervisor-adapter.mjs';
import {encodeGhostRows,ghostFamilyStatus} from '../host/ghost-wire.mjs';
import {crc32} from '../host/wire.mjs';
import {AttentionModel} from '../host/attention.mjs';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {characters,states,expressionSvg} from '../preview/ghost-family/expressions.mjs';
const id=n=>sessionIdentity('synthetic-session-'+n),sha=v=>createHash('sha256').update(v).digest('hex');
const packet=prefix=>prefix+crc32(prefix);
const status=(boot=7,seq=0,caps=8)=>packet(`D2 ${boot} ${seq} ${seq} 0 0 0 0 ${caps} `);
const advert=(boot=7,seq=0)=>packet(`G0 ${boot} ${seq} 2 6 `);
test('character identity survives re-registration, host recreation, focus and workspace changes',()=>{
 const picks=Array.from({length:256},(_,n)=>ghostCharacter(id(n)));assert.equal(new Set(picks).size,6);
 for(const invalid of ['',null,'x','f'.repeat(63),'G'.repeat(64)])assert.throws(()=>ghostCharacter(invalid));
 const take=(m,n)=>{const rows=encodeGhostRows(m.snapshot(),{boot:7,seq:1,family:true}).trim().split('\n');const index=m.snapshot().sessions.findIndex(s=>s.id===id(n));return Number(rows.find(r=>Number(r.split(' ')[3])===index).split(' ')[10]);};
 for(const order of [[0,1,2],[2,0,1],[1,2,0]]){
  const m=new AttentionModel();for(const n of order){m.register(id(n));m.status(id(n),{state:'idle'});}
  for(const n of order){assert.equal(take(m,n),picks[n]);m.page(1);m.identity(id(n),{spaceId:id(99),spaceTitle:'Renamed workspace'});assert.equal(take(m,n),picks[n]);}
  m.remove(id(0));m.register(id(0));m.status(id(0),{state:'thinking'});assert.equal(take(m,0),picks[0]);
 }
});
test('family format advertisement validates all fields and is not a D2 authority packet',()=>{
 assert.deepEqual(ghostFamilyStatus(advert()),{boot:7,seq:0});
 for(const prefix of ['G0 0 0 2 6 ','G0 7 0 3 6 ','G0 7 0 2 7 ','G0 07 0 2 6 ','G0 7 4294967296 2 6 ','G0 7 0 2 ','D2 7 0 2 6 '])assert.equal(ghostFamilyStatus(packet(prefix)),null);
 assert.equal(ghostFamilyStatus(advert()+'x'),null);
});
test('old/new negotiation is connection/boot scoped and cannot establish readiness or clear attention',t=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-family-ledger-')),ledger=new DeliveryLedger(dir);t.after(()=>{ledger.close();rmSync(dir,{recursive:true,force:true});});
 const model=new AttentionModel();model.register(id(1));model.status(id(1),{state:'idle'});model.attention(id(1),{itemId:id(91),kind:'input'});
 const c=new UsbController(model,ledger);
 assert.equal(c.observeFamily(advert()),false);assert.equal(c.observe(status(),0),true);
 assert.match(c.tick(1),/\nG1 /);const before=c.status(),pending=model.snapshot().sessions[0].attentionCount;
 assert.equal(c.observeFamily(advert(8)),false);assert.equal(c.observeFamily(advert(7,1)),false);assert.equal(c.observeFamily(advert()),true);
 assert.deepEqual(c.status(),before);assert.equal(c.status().ready,false);assert.equal(model.snapshot().sessions[0].attentionCount,pending);
 assert.match(c.tick(2),/\nG2 /);assert.equal(ledger.snapshot().acks.length,0);
 assert.equal(c.observeFamily(advert(),1501),false);assert.equal(c.observeFamily(advert(),-1),false);
 c.disconnected();assert.equal(c.observeFamily(advert()),false);assert.match(c.tick(3),/\nG1 /);
 c.observe(status(8),4);assert.equal(c.observeFamily(advert()),false);assert.match(c.tick(5),/\nG1 /);
 c.observe(status(9,0,0),6);assert.equal(c.observeFamily(advert(9)),false);assert.doesNotMatch(c.tick(7),/\nG[12] /);
});
test('canonical idle SVGs remain untouched; expressions and generated art hashes are bounded',()=>{
 const manifest=JSON.parse(readFileSync(new URL('../firmware/art/ghost-family.manifest.json',import.meta.url)));
 assert.deepEqual(manifest.characters,characters);assert.equal(manifest.frames.length,90);assert.ok(manifest.binaryBytes<1600000);
 assert.equal(sha(readFileSync(new URL('../firmware/art/ghost-family.generated.h',import.meta.url))),manifest.headerSha256);
 const original=JSON.parse(readFileSync(new URL('../firmware/art/ghost.manifest.json',import.meta.url)));assert.equal(manifest.flagshipHeaderSha256,original.headerSha256);assert.equal(sha(readFileSync(new URL('../firmware/art/ghost.generated.h',import.meta.url))),original.headerSha256);
 for(const [file,pin] of Object.entries(manifest.sourceHashes)){const path=file==='export-ghost-family.mjs'?'../preview/'+file:'../preview/ghost-family/'+file;assert.equal(sha(readFileSync(new URL(path,import.meta.url))),pin,file);}
 for(const name of characters.slice(1)){const source=readFileSync(new URL(`../preview/ghost-family/${name}.svg`,import.meta.url),'utf8');assert.equal(expressionSvg(source,name,'idle',false),source);for(const s of states)for(const blink of [false,true]){const svg=expressionSvg(source,name,s,blink);assert.match(svg,/<svg /);assert.equal((svg.match(/id="expression"/g)||[]).length,1);assert.doesNotMatch(svg,/<script|https?:\/\/(?!www.w3.org)/);}}
});
test('actual retained family renderer decodes all 108 frames exactly and preserves dirty composition',t=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-family-native-'));t.after(()=>rmSync(dir,{recursive:true,force:true}));const binary=join(dir,'test');
 const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('ghost-family.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
 const run=spawnSync(binary,[dir],{encoding:'utf8',timeout:90000});assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
 const original=JSON.parse(readFileSync(new URL('../firmware/art/ghost.manifest.json',import.meta.url))),family=JSON.parse(readFileSync(new URL('../firmware/art/ghost-family.manifest.json',import.meta.url)));
 for(let c=0;c<6;c++)for(let s=0;s<9;s++)for(let b=0;b<2;b++){
  const expected=c===0?original.frames[s*2+b]:family.frames[(c-1)*18+s*2+b];assert.equal(sha(readFileSync(join(dir,`${c}-${s}-${b}.rgb`))),expected.decodedSha256,`${c}/${s}/${b}`);
 }
 t.diagnostic(run.stdout.trim());
});
