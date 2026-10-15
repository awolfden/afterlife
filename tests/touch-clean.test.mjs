import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
for(const measured of [false,true])test(`diagnostics-free sampler/swipe matches instrumented behavior, measured=${measured}`,()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-touch-clean-'));
 try{
  const results=[];
  for(const diagnostics of [false,true]){
   const bin=join(dir,diagnostics?'diagnostic':'clean');
   const args=['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined'];
   if(measured)args.push('-DWATCHER_TOUCH_MEASURED_SCALE=1');if(diagnostics)args.push('-DWATCHER_TOUCH_DIAGNOSTICS=1');
   const build=spawnSync('clang++',[...args,fileURLToPath(new URL('touch-clean.cpp',import.meta.url)),'-o',bin],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);
   const run=spawnSync(bin,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stdout+run.stderr);
   const result=JSON.parse(run.stdout);assert.equal(result.diagnosticApi,diagnostics);assert.equal(result.events,8);assert.ok(result.observations>1000);results.push(result);
  }
  assert.equal(results[0].trace,results[1].trace);assert.equal(results[0].observations,results[1].observations);assert.ok(results[0].samplerBytes<results[1].samplerBytes);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
