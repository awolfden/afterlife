import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
const project=fileURLToPath(new URL('../',import.meta.url));
const component=join(project,'firmware/pet/components/watcher_codec');
const upstream=join(component,'upstream');
test('all 21 vendored codec files retain their pinned source hashes',()=>{
 const provenance=JSON.parse(readFileSync(join(component,'provenance.json'),'utf8'));
 assert.equal(provenance.version,'1.5.4');assert.equal(Object.keys(provenance.files).length,21);
 for(const [name,hash] of Object.entries(provenance.files))assert.equal(createHash('sha256').update(readFileSync(join(upstream,name))).digest('hex'),hash,name);
});
test('real codec and factory engine run under address/undefined sanitizers against hardware mocks',{timeout:120000},()=>{
 const work=mkdtempSync(join(tmpdir(),'watcher-factory-test-'));
 try {
  const include=['tests/fixtures/factory','firmware/pet/components/watcher_codec/upstream/include','firmware/pet/components/watcher_codec/upstream/interface','firmware/pet/components/watcher_codec/upstream/device/include','firmware/pet/components/watcher_codec/upstream/device/priv_include','firmware/pet/components/watcher_codec/upstream'].flatMap(p=>['-I',resolve(project,p)]);
  const options=['-g','-DWATCHER_FEEDBACK_HOST_TEST=1','-fsanitize=address,undefined','-fno-omit-frame-pointer',...include];
  const sources=['esp_codec_dev.c','esp_codec_dev_if.c','esp_codec_dev_vol.c','audio_codec_sw_vol.c','device/es8311/es8311.c'];
  const objects=sources.map((source,i)=>{
   const target=join(work,`${i}.o`);execFileSync('clang',['-std=c11',...options,'-c',join(upstream,source),'-o',target],{timeout:30000});return target;
  });
  for(const [mode,defines] of [['production',['-DWATCHER_HAPTICS=1']],['acceptance',['-DWATCHER_FACTORY_ACCEPTANCE=1','-DWATCHER_FACTORY_HAPTIC_ACCEPTANCE=1']]]){
   const binary=join(work,`factory-${mode}`);
   execFileSync('clang++',['-std=c++17',...options,...defines,join(project,'tests/factory-feedback.cpp'),...objects,'-o',binary],{timeout:30000});
   const output=execFileSync(binary,[],{timeout:20000,encoding:'utf8'});assert.match(output,/scenarios passed/);console.log(mode,output.trim());
  }
 }finally{rmSync(work,{recursive:true,force:true});}
});
test('production and explicit acceptance share the factory service; legacy backend cannot build',()=>{
 const main=readFileSync(join(project,'firmware/pet/main/main.cpp'),'utf8');
 const cmake=readFileSync(join(project,'firmware/pet/main/CMakeLists.txt'),'utf8');
 assert.ok(!main.includes('#include "audio.h"'));assert.match(main,/watcher_feedback::Service audio/);
 assert.match(main,/prefix="B1 "/);assert.match(main,/prefix="C1 "/);assert.match(main,/factory_requested=true/);
 assert.match(cmake,/Legacy audio.h is retired/);assert.match(cmake,/if\(NOT WATCHER_HAPTICS_SOFTWARE_CUTOFF\)/);
 assert.match(cmake,/Automatic haptics requires normal factory feedback and excludes acceptance\/probe modes/);
 assert.ok(!cmake.includes('WATCHER_FEEDBACK_HOST_TEST'));assert.ok(!main.includes('process_for_test'));
 const service=readFileSync(join(project,'firmware/pet/main/factory-feedback-service.h'),'utf8');
 assert.match(service,/return request\(mask&1\)/);assert.match(service,/WATCHER_FACTORY_HAPTIC_ACCEPTANCE/);
 assert.match(service,/#ifndef WATCHER_FACTORY_ACCEPTANCE/);assert.match(service,/return eligible&&permitted&&request\(mask\)/);
});
