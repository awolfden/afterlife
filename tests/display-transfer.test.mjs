import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
test('pinned-API display adapter checks reservation and bounded completion polling; faults cannot publish',()=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-display-transfer-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17','-DNDEBUG','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('display-transfer.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});
  assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
  assert.match(run.stdout,/display adapter: four rotations, checked boot reservation, completion, geometry, ownership, clock, terminal faults pass/);
 }finally{rmSync(directory,{recursive:true,force:true});}
});
