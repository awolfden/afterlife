import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {AttentionModel} from '../host/attention.mjs';
import {encodeGhostRows} from '../host/ghost-wire.mjs';
import {encodeFrame,crc32} from '../host/wire.mjs';
const id=n=>n.toString(16).padStart(64,'0');
function corpus(){
 let seq=0,text='';
 for(let count=1;count<=16;count++){
  const model=new AttentionModel();for(let i=1;i<=count;i++)model.register(id(i));
  for(let selected=0;selected<count;selected++){
   const snapshot=model.snapshot(),rows=encodeGhostRows(snapshot,{boot:1,seq:++seq});
   const lines=rows.trim().split('\n'),indices=lines.map(line=>Number(line.split(' ')[3]));
   assert.ok(lines.length<=6);assert.equal(new Set(indices).size,indices.length);
   const neighbors=new Set([0,-1,1,-2,2].map(d=>(selected+d+count)%count));
   for(const index of neighbors)assert.ok(indices.includes(index));
   const start=Math.floor(selected/4)*4;
   assert.deepEqual(indices.filter(i=>Math.floor(i/4)===Math.floor(selected/4)).sort((a,b)=>a-b),Array.from({length:Math.min(4,count-start)},(_,i)=>start+i));
   for(const line of lines){assert.ok(line.length<192);const end=line.lastIndexOf(' ');assert.equal(crc32(line.slice(0,end+1)),Number(line.slice(end+1)));}
   text+=encodeFrame(snapshot,{boot:1,seq,alert:0,eventAck:0,reduced:false})+rows;
   model.page(1);
  }
 }
 return text;
}
test('rolling metadata stays bounded and retains every legacy page for all roster sizes and selections',()=>{assert.ok(corpus().length>0);});
test('carousel geometry, reversal, every page/wrap boundary, release authority and exact neighbor cache under sanitizers',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-carousel-'));
 try{
  const binary=join(dir,'test');
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('carousel.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:60000});assert.equal(run.status,0,run.stdout+run.stderr);
  const cross=spawnSync(binary,['--rows'],{input:corpus(),encoding:'utf8',timeout:30000});assert.equal(cross.status,0,cross.stdout+cross.stderr);assert.match(cross.stdout,/136 ring windows accepted/);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('carousel integration keeps host selection, visible ACK identity, metadata hold, reduced motion and control guards',()=>{
 const source=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8');
 const render=source.slice(source.indexOf('static void render(int64_t now)'),source.indexOf('} // namespace\nextern "C" void app_main'));
 assert.match(render,/page_mode==watcher_ghost::Mode::carousel&&identity_ready/);
 assert.match(render,/carousel.update\(ring,now,move_focus,dragging,displacement,release,paging_direction\)/);
 assert.match(render,/if\(linked&&page_mode==watcher_ghost::Mode::waiting\)return/);
 assert.match(render,/bool move_focus=!reduced/);
 assert.doesNotMatch(render,/voice\.|WATCHER_MIC_CAPTURE/);
 assert.match(render,/displayed_preview=focus_transition.previewing\(\)\|\|carousel.previewing\(\)/);
 assert.match(render,/if\(!row\)\{focus_transition.reset\(\);carousel.reset\(\);return;\}/);
 assert.doesNotMatch(render,/frame.selected\s*=|event\(|network.send|vTaskDelay/);
 assert.match(source,/if\(value&&ambiguous\)\{swipe.cancel\(\);focus_transition.reset\(\);carousel.reset\(\);\}/);
 assert.match(source,/vTaskDelay\(pdMS_TO_TICKS\(5\)\+1\)/);
});
