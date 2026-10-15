import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
test('physical transfer bands cover the466 canvas in all rotations without exceeding even-aligned row limits',()=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-transfer-bands-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('transfer-bands.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});
  assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
  assert.match(run.stdout,/128 rotation\/band coverage cases preserve every pixel and even-expanded physical row bounds/);
 }finally{rmSync(directory,{recursive:true,force:true});}
});
