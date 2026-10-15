import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
test('render publication preserves old-frame authority, sticky identity invalidation and real Swipe/K1 and pre-loss hook contracts',()=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-render-authority-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('render-authority.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});
  assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
  assert.match(run.stdout,/render authority: heartbeat, preview,9 identity seams, ABA, DMA retirement, paging, real Swipe\/K1 and mandatory pre-loss hook contracts pass/);
 }finally{rmSync(directory,{recursive:true,force:true});}
});
