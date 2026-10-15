import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {motionMetrics} from '../host/motion-metrics.mjs';
for(const [source,family,afterlife] of [['retained-paging',false,false],['retained-loop',false,false],['retained-loop',true,false],['retained-loop',true,true]])test(`${source}${family?' with family':''}${afterlife?' and Afterlife':''}: original motion and retained framebuffer rewrite`,t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-retained-'));
 try{
  const binary=join(directory,'test');
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',...(family?['-DWATCHER_GHOST_FAMILY=1']:[]),...(afterlife?['-DWATCHER_AFTERLIFE=1']:[]),fileURLToPath(new URL(`${source}.cpp`,import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:90000});
  assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
  if(source==='retained-loop'){
   const lines=run.stdout.split('\n').filter(line=>line.startsWith('R3 '));assert.equal(lines.length,1);
   const snapshot=motionMetrics(lines[0]);assert.ok(snapshot);assert.equal(snapshot.boot,7);assert.equal(snapshot.sequence,1);assert.ok(snapshot.frames>100);assert.ok(snapshot.reused>0);assert.ok(snapshot.discarded>0);assert.equal(snapshot.fault,0);
  }
  t.diagnostic(run.stdout.trim());
 }finally{rmSync(directory,{recursive:true,force:true});}
});
