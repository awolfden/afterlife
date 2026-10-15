import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
for(const enabled of [false,true])test(`measured normalization opt-in=${enabled}: exhaustive envelopes, parity, rotation and sampler under sanitizers`,()=>{
 const dir=mkdtempSync(join(tmpdir(),'touch-normalization-'));
 try{
  const bin=join(dir,'native'),args=['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined'];if(enabled)args.push('-DWATCHER_TOUCH_MEASURED_SCALE=1');
  const built=spawnSync('clang++',[...args,fileURLToPath(new URL('touch-normalization.cpp',import.meta.url)),'-o',bin],{encoding:'utf8',timeout:60000});assert.equal(built.status,0,built.stderr);
  const run=spawnSync(bin,[],{encoding:'utf8',timeout:60000});assert.equal(run.status,0,run.stderr);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('normalization is a separate decoder, not calibration, clamping or gesture changes',()=>{
 const normal=readFileSync(new URL('../firmware/pet/main/touch-normalization.h',import.meta.url),'utf8');assert.match(normal,/measured_raw_x_max=460,measured_raw_y_max=461/);assert.match(normal,/Kind::contact,x\/2,y\/2/);assert.match(normal,/if\(rejection\)return \{\{\},rejection\}/);assert.doesNotMatch(normal.replace(/\/\/[^\n]*/g,''),/\b(?:orient|saturate|event|reg_write|reg_read|clamp)\(/);
 const sampler=readFileSync(new URL('../firmware/pet/main/touch.h',import.meta.url),'utf8');assert.match(sampler,/#ifdef WATCHER_TOUCH_MEASURED_SCALE\n   const auto decoded=decode_measured_packet\(bytes,sizeof bytes\);\n#else\n   const auto decoded=decode_packet\(bytes,sizeof bytes\);/);
 const legacy=readFileSync(new URL('../firmware/pet/main/swipe.h',import.meta.url),'utf8');assert.doesNotMatch(legacy,/WATCHER_TOUCH_MEASURED_SCALE|decode_measured_packet/);
});
