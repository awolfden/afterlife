import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {AttentionModel} from '../host/attention.mjs';
import {GhostContext} from '../host/ghost-wire.mjs';
import {crc32} from '../host/wire.mjs';
const id=n=>n.toString(16).padStart(64,'0');
function records(){
 const model=new AttentionModel();let context=new GhostContext();model.register(id(1));model.register(id(2));
 let seq=0;const lines=[];
 const take=()=>{const line=context.encode(model.snapshot(),model.selection().epoch,{boot:99,seq:++seq},model.rosterEpoch());lines.push(line);return line;};
 take();model.status(id(1),{state:'working'});take(); // Ordinary state changes preserve identity.
 model.page(1);model.page(-1);take(); // Selection ABA advances even between transmissions.
 model.remove(id(2));model.register(id(2));take(); // Same opaque ID, new lifetime.
 const identity={spaceId:id(50),spaceTitle:'Same title'};model.identity(id(1),identity);take();model.identity(id(1),identity);take();
 context=new GhostContext();take(); // Fast host restart in the same device boot.
 return lines;
}
test('focus metadata uses bounded counters, CRC, stable heartbeats and ABA-safe identity',()=>{
 const lines=records();const fields=lines.map(line=>line.trim().split(' ').slice(1,5).map(Number));
 assert.deepEqual(fields,[[99,1,1,1],[99,2,1,1],[99,3,1,3],[99,4,4,3],[99,5,5,3],[99,6,5,3],[99,7,7,7]]);
 for(const line of lines){const at=line.trim().lastIndexOf(' ');assert.equal(crc32(line.slice(0,at+1)),Number(line.slice(at+1)));assert.ok(line.length<192);assert.equal(line.includes(id(1)),false);}
 assert.throws(()=>new GhostContext().encode({sessions:[]},null,{boot:1,seq:1}));
});
test('focus logical clocks reject replay and restart safely at uint32 boundaries',()=>{
 const model=new AttentionModel();model.register(id(1));const context=new GhostContext();
 const encode=(boot,seq)=>context.encode(model.snapshot(),model.selection().epoch,{boot,seq},model.rosterEpoch());
 assert.match(encode(1,0xfffffffe),/^K1 1 4294967294 4294967294 4294967294 /);
 assert.match(encode(1,0xffffffff),/^K1 1 4294967295 4294967294 4294967294 /);
 assert.throws(()=>encode(1,0xffffffff));assert.throws(()=>encode(1,0));
 assert.match(encode(2,1),/^K1 2 1 1 1 /);
});
test('portable finger paging and host-to-firmware identity records under sanitizers',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-finger-paging-'));
 try{
  const binary=join(dir,'test');
  const build=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('finger-paging.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stdout+run.stderr);
  const cross=spawnSync(binary,['--parse'],{input:records().join(''),encoding:'utf8',timeout:30000});assert.equal(cross.status,0,cross.stdout+cross.stderr);
  assert.deepEqual(cross.stdout.trim().split('\n'),['1 1','1 1','1 3','4 3','5 3','5 3','7 7']);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('integration preserves authority separation, loop yield, capture absence and optional legacy display',()=>{
 const source=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8');
 assert.match(source,/dragging=identity_ready&&swipe.tracking\(\)/);
 assert.doesNotMatch(source,/voice\.|WATCHER_MIC_CAPTURE/);
 assert.match(source,/block_preview_controls&&input.action==Action::ack/);
 assert.match(source,/frame.event_ack>=paging_event\?2u:1u/);
 assert.match(source,/if\(\(swipe.tracking\(\)&&focus_context.matches\(frame\)&&!\(frame.flags&8\)\)\|\|focus_transition.moving\(\)\|\|carousel.moving\(\)\)render_interval=20/);
 assert.match(source,/displayed_preview=focus_transition.previewing\(\)\|\|carousel.previewing\(\)/);
 assert.match(source,/vTaskDelay\(pdMS_TO_TICKS\(5\)\+1\)/);
 assert.doesNotMatch(source,/voice_usb_budget|usb_capture/);
 assert.match(source,/if\(linked&&page_mode==watcher_ghost::Mode::waiting\)return/);
 const metadata=source.slice(source.indexOf('if(length>=3&&!memcmp(line,"K1 "'),source.indexOf('if(length>=3&&(!memcmp(line,"G1 "'));
 assert.doesNotMatch(metadata,/last_frame_ms=|event\(|send_status\(|voice\./);
});
