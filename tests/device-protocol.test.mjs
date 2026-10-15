import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {AttentionModel} from '../host/attention.mjs';
import {encodeFrame} from '../host/wire.mjs';
test('native parser and normal/opt-in button contracts pass sanitizers and actual host frame corpus',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-device-protocol-'));
 try{
  const binary=join(dir,'test'),corpus=join(dir,'frames.txt');
  const build=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('device-protocol.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);
  const model=new AttentionModel();let frames=encodeFrame(model.snapshot(),{boot:1,seq:1,alert:0,eventAck:0});
  for(let i=1;i<=16;i++){model.register(i.toString(16).padStart(64,'0'));frames+=encodeFrame(model.snapshot(),{boot:0xffffffff,seq:i+1,alert:0,eventAck:0});}
  writeFileSync(corpus,frames);const run=spawnSync(binary,[corpus],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stdout+run.stderr);assert.match(run.stdout,/17 JavaScript-produced frames accepted/);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
