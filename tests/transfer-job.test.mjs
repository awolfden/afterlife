import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
test('transfer sequencing never equates push return with bus completion or publishes aborted/failed jobs',()=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-transfer-job-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('transfer-job.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});
  assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
  assert.match(run.stdout,/12 complete transfers; pending DMA, dirty\/reservation failure, clip restore and abort publication guards pass/);
 }finally{rmSync(directory,{recursive:true,force:true});}
});
