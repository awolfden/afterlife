import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
test('portable touch decoder, release-only swipe authority and sampler failure handling under sanitizers',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-swipe-'));
 try{
  const binary=join(dir,'swipe'),source=fileURLToPath(new URL('touch-swipe.cpp',import.meta.url));
  const build=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',source,'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);
  const result=spawnSync(binary,[],{encoding:'utf8',timeout:30000});assert.equal(result.status,0,result.stdout+result.stderr);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('touch stays optional, capture-free and routes only existing paging events',()=>{
 const source=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8'),cmake=readFileSync(new URL('../firmware/pet/main/CMakeLists.txt',import.meta.url),'utf8');
 assert.match(cmake,/option\(WATCHER_TOUCH_SWIPE[^\n]+OFF\)/);
 assert.doesNotMatch(source,/voice\.|WATCHER_MIC_CAPTURE/);
 assert.match(source,/const bool allowed=swipe.guard\(context,now\)/);
 assert.match(source,/if\(!allowed\)return/);
 assert.match(source,/if\(gesture.kind\)event\(gesture.kind,gesture.shown,gesture.boot\)/);
 assert.ok(source.indexOf('touch(now);')>source.indexOf('buttons(now);'));
 assert.match(cmake,/option\(WATCHER_TOUCH_DIAGNOSTICS[^\n]+OFF\)/);
 assert.match(source,/bool show_touch=true/);
 const diagnostic=readFileSync(new URL('../firmware/pet/main/touch-diagnostic.h',import.meta.url),'utf8');
 assert.doesNotMatch(diagnostic, /\b(?:output|usb_serial_jtag_write_bytes|reg_write|fopen|printf)\(/);
 assert.match(diagnostic,/char \(&top\)\[48\],char \(&bottom\)\[48\]/);
});
