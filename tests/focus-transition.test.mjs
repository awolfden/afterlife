import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
test('focus transitions are bounded, interruptible, reset-safe and baseline-identical at rest',()=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-focus-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('focus-transition.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stdout+run.stderr);
  assert.match(run.stdout,/retargets.*pass/);
 }finally{rmSync(directory,{recursive:true,force:true});}
});
test('firmware keeps animation presentation-only and retains bounded scheduling, metadata hold without voice gates',()=>{
 const source=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8');
 const render=source.slice(source.indexOf('static void render(int64_t now)'),source.indexOf('} // namespace\nextern "C" void app_main'));
 assert.match(render,/Mode::waiting\)return/);
 assert.match(render,/bool move_focus=!reduced/);
 assert.doesNotMatch(render,/voice\.|WATCHER_MIC_CAPTURE/);
 assert.match(render,/scene\.generation=link_generation/);
 assert.match(render,/scene\.rotation=rotation/);
 assert.match(render,/focus_transition\.reset\(\)/);
 assert.match(source,/unsigned render_interval=50/);
 assert.match(source.replace(/#ifdef WATCHER_TOUCH_TIMING_DIAGNOSTICS[\s\S]*?#endif/g,''),/if\(now-rendered>=render_interval\)\{\s*render\(now\);rendered=now;\s*\}/);
 assert.match(source,/if\(\(swipe.tracking\(\)&&focus_context.matches\(frame\)&&!\(frame.flags&8\)\)\|\|focus_transition.moving\(\)\|\|carousel.moving\(\)\)render_interval=20/);
 assert.match(render,/displayed_seq=linked\?frame.seq:0/);
 assert.doesNotMatch(render,/vTaskDelay|while\s*\(|network\.send|event\(|frame\.selected\s*=/);
});
