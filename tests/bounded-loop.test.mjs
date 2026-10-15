import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
for(const diagnostics of [false,true])test(`actual bounded scene/loop preserves frozen legacy scenes and yields due-sample navigation (overlay=${diagnostics})`,t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-bounded-loop-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17',...(diagnostics?['-DWATCHER_TOUCH_DIAGNOSTICS=1']:[]),'-DNDEBUG','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('bounded-loop.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:60000});
  assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
  assert.match(run.stdout,/bounded loop: 5480 frozen legacy scene comparisons; snapshot heartbeat, rotation, brightness and due-sample navigation pass/);
  t.diagnostic(run.stdout.trim());
  const invalid=spawnSync(binary,['--invalid-scene'],{encoding:'utf8',timeout:10000});
  assert.equal(invalid.status,0,invalid.error?.message??invalid.stdout+invalid.stderr);
  assert.match(invalid.stdout,/RAM-only producer rejection remains terminal/);
  t.diagnostic(invalid.stdout.trim());
 }finally{rmSync(directory,{recursive:true,force:true});}
});
