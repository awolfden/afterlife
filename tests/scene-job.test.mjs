import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
test('owned scene steps preserve legacy ghost spans and interleaved UI calls within work bounds',()=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-scene-job-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17','-DNDEBUG','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('scene-job.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});
  assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
  assert.match(run.stdout,/scene job: 27 owned ordered command\/span cases; bounded clear\/work, capacity and cancellation pass/);
 }finally{rmSync(directory,{recursive:true,force:true});}
});
