import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {AttentionModel} from '../host/attention.mjs';
import {encodeGhostRows,displayCaption} from '../host/ghost-wire.mjs';
import {crc32} from '../host/wire.mjs';
const id=n=>n.toString(16).padStart(64,'0');
test('badge captions transliterate accents and visibly substitute unsupported glyphs',()=>{
 assert.equal(displayCaption('café 🐺'),'cafe ?');assert.match(displayCaption('🐺'.repeat(12)),/^\?+$/);
 const model=new AttentionModel();model.register(id(1));model.identity(id(1),{spaceId:id(99),spaceTitle:'\u0301'});
 assert.match(encodeGhostRows(model.snapshot(),{boot:1,seq:1}),/ 3f \d+\n$/);
});
test('host ghost rows fit legacy line capacity and carry exact generation checksums',()=>{
 const model=new AttentionModel();for(let i=1;i<=16;i++){model.register(id(i));model.identity(id(i),{spaceId:id(99),spaceTitle:'🐺'.repeat(12)});}
 const lines=encodeGhostRows(model.snapshot(),{boot:0xffffffff,seq:0xffffffff}).trim().split('\n');assert.equal(lines.length,6);
 for(const line of lines){assert.ok(line.length<192);const end=line.lastIndexOf(' ');assert.equal(crc32(line.slice(0,end+1)),Number(line.slice(end+1)));assert.match(line,/^G1 4294967295 4294967295 /);}
 assert.throws(()=>encodeGhostRows(model.snapshot(),{boot:0,seq:1}));
});
test('solo/group layout uses total session count and companions animate by their own state',()=>{
 const source=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8');
 assert.match(source,/if\(focused&&watcher_render::multi_session_layout\(frame.count\)\)/);
 assert.match(source,/personality_pose\(static_cast<watcher_render::State>\(row->state\),reduced,now,row->rhythm,true\)/);
 assert.match(source,/draw_ghost\(static_cast<watcher_render::State>\(art_state\),animated/);
});
test('portable ghost page parser and rendering stay bounded under sanitizers',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-ghost-pages-'));
 try{
  const binary=join(dir,'test');
  const build=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('ghost-pages.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stdout+run.stderr);
  const model=new AttentionModel();for(let i=1;i<=4;i++){model.register(id(i));model.identity(id(i),{spaceId:id(99),spaceTitle:'café 🐺'});}
  const encoded=encodeGhostRows(model.snapshot(),{boot:0xffffffff,seq:0xffffffff});
  const cross=spawnSync(binary,['--parse'],{input:encoded,encoding:'utf8',timeout:30000});assert.equal(cross.status,0,cross.stdout+cross.stderr);
  assert.deepEqual(cross.stdout.trim().split('\n'),[0,3,1,2].map(index=>`4294967295 4294967295 ${index} cafe ?`));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
