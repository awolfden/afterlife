import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const main=fileURLToPath(new URL('../firmware/pet/main/CMakeLists.txt',import.meta.url));
const cmake=process.env.WATCHER_CMAKE??'cmake';
function configure(options){
 const directory=mkdtempSync(join(tmpdir(),'watcher-build-policy-'));
 try{
  const script=join(directory,'check.cmake');
  writeFileSync(script,`cmake_minimum_required(VERSION 3.16)\nfunction(idf_component_register)\n set(COMPONENT_LIB fixture PARENT_SCOPE)\nendfunction()\nfunction(target_compile_definitions)\n set_property(GLOBAL APPEND PROPERTY RECORDED_DEFS "\${ARGV}")\nendfunction()\nfunction(target_link_libraries)\nendfunction()\ninclude("${main}")\nget_property(DEFS GLOBAL PROPERTY RECORDED_DEFS)\nmessage(STATUS "Definitions: \${DEFS}")\n`);
  const args=Object.entries(options).map(([key,value])=>`-D${key}=${value?'ON':'OFF'}`);
  const result=spawnSync(cmake,[...args,'-P',script],{encoding:'utf8',timeout:10000});
  if(result.error)throw result.error;
  return {status:result.status,text:result.stdout+result.stderr};
 }finally{rmSync(directory,{recursive:true,force:true});}
}
const factory={WATCHER_FACTORY_FEEDBACK:true,WATCHER_COMPLETION_CHIME:true};
test('retained paging and unsolicited frame metrics require separate explicit normal-profile opt-ins',()=>{
 const defaults=configure({});assert.equal(defaults.status,0,defaults.text);assert.doesNotMatch(defaults.text,/WATCHER_(RETAINED_PAGING|MOTION_METRICS)=1/);
 const profile={WATCHER_BOUNDED_RENDER:true,WATCHER_RETAINED_PAGING:true,WATCHER_GHOST_PAGES:true,WATCHER_TOUCH_SWIPE:true,WATCHER_LAN:true};
 const enabled=configure(profile);assert.equal(enabled.status,0,enabled.text);assert.match(enabled.text,/WATCHER_RETAINED_PAGING=1/);assert.doesNotMatch(enabled.text,/WATCHER_MOTION_METRICS=1/);
 for(const flag of Object.keys(profile).filter(f=>f!=='WATCHER_RETAINED_PAGING'))assert.notEqual(configure({...profile,[flag]:false}).status,0);
 assert.notEqual(configure({...profile,WATCHER_MOTION_METRICS:true}).status,0);
 const metrics=configure({...profile,WATCHER_MOTION_METRICS:true,WATCHER_TOUCH_DIAGNOSTICS:true,WATCHER_TOUCH_USB_DIAGNOSTICS:true,WATCHER_TOUCH_TIMING_DIAGNOSTICS:true});assert.equal(metrics.status,0,metrics.text);assert.match(metrics.text,/WATCHER_MOTION_METRICS=1/);
 for(const flag of ['WATCHER_SUPERVISED_MOTOR_TEST','WATCHER_FACTORY_ACCEPTANCE','WATCHER_FACTORY_HAPTIC_ACCEPTANCE','WATCHER_AUDIO_INIT_PROBE','WATCHER_AUDIO_CHIME_PROBE','WATCHER_AUDIO_PATH_PROBE'])assert.notEqual(configure({...factory,...profile,[flag]:true}).status,0);
});
test('measured normalization is separately default-off, requires touch and retains probe exclusions',()=>{
 const defaults=configure({WATCHER_TOUCH_SWIPE:true});assert.equal(defaults.status,0,defaults.text);assert.ok(!defaults.text.includes('WATCHER_TOUCH_MEASURED_SCALE=1'));
 assert.notEqual(configure({WATCHER_TOUCH_MEASURED_SCALE:true}).status,0);
 const enabled=configure({WATCHER_TOUCH_SWIPE:true,WATCHER_TOUCH_MEASURED_SCALE:true});assert.equal(enabled.status,0,enabled.text);assert.match(enabled.text,/WATCHER_TOUCH_MEASURED_SCALE=1/);
 for(const flag of ['WATCHER_SUPERVISED_MOTOR_TEST','WATCHER_FACTORY_ACCEPTANCE','WATCHER_FACTORY_HAPTIC_ACCEPTANCE','WATCHER_AUDIO_INIT_PROBE','WATCHER_AUDIO_CHIME_PROBE','WATCHER_AUDIO_PATH_PROBE'])assert.notEqual(configure({...factory,WATCHER_TOUCH_SWIPE:true,WATCHER_TOUCH_MEASURED_SCALE:true,[flag]:true}).status,0);
});
test('range windows require an additional explicit USB diagnostic opt-in',()=>{
 const defaults=configure({});assert.equal(defaults.status,0,defaults.text);assert.ok(!defaults.text.includes('WATCHER_TOUCH_RANGE_DIAGNOSTICS=1'));
 const profile={WATCHER_TOUCH_SWIPE:true,WATCHER_TOUCH_DIAGNOSTICS:true,WATCHER_TOUCH_USB_DIAGNOSTICS:true,WATCHER_TOUCH_RANGE_DIAGNOSTICS:true};
 const enabled=configure(profile);assert.equal(enabled.status,0,enabled.text);assert.match(enabled.text,/WATCHER_TOUCH_RANGE_DIAGNOSTICS=1/);
 for(const flag of ['WATCHER_TOUCH_SWIPE','WATCHER_TOUCH_DIAGNOSTICS','WATCHER_TOUCH_USB_DIAGNOSTICS'])assert.notEqual(configure({...profile,[flag]:false}).status,0);
});
test('USB touch counters are separately opt-in and require the local touch diagnostic profile',()=>{
 const defaults=configure({});assert.equal(defaults.status,0,defaults.text);assert.ok(!defaults.text.includes('WATCHER_TOUCH_USB_DIAGNOSTICS=1'));
 for(const options of [{},{WATCHER_TOUCH_SWIPE:true}])assert.notEqual(configure({...options,WATCHER_TOUCH_USB_DIAGNOSTICS:true}).status,0);
 const accepted=configure({WATCHER_TOUCH_SWIPE:true,WATCHER_TOUCH_DIAGNOSTICS:true,WATCHER_TOUCH_USB_DIAGNOSTICS:true});assert.equal(accepted.status,0,accepted.text);assert.match(accepted.text,/WATCHER_TOUCH_USB_DIAGNOSTICS=1/);
});
test('local touch diagnostics requires touch and never enables feedback/audio probes',()=>{
 const defaults=configure({});assert.equal(defaults.status,0,defaults.text);assert.ok(!defaults.text.includes('WATCHER_TOUCH_DIAGNOSTICS=1'));
 const denied=configure({WATCHER_TOUCH_DIAGNOSTICS:true});assert.notEqual(denied.status,0);assert.match(denied.text,/requires touch navigation/);
 const enabled=configure({WATCHER_TOUCH_SWIPE:true,WATCHER_TOUCH_DIAGNOSTICS:true});assert.equal(enabled.status,0,enabled.text);assert.match(enabled.text,/WATCHER_TOUCH_DIAGNOSTICS=1/);
 for(const flag of ['WATCHER_SUPERVISED_MOTOR_TEST','WATCHER_FACTORY_ACCEPTANCE','WATCHER_FACTORY_HAPTIC_ACCEPTANCE','WATCHER_AUDIO_INIT_PROBE','WATCHER_AUDIO_CHIME_PROBE','WATCHER_AUDIO_PATH_PROBE'])assert.notEqual(configure({...factory,WATCHER_TOUCH_SWIPE:true,WATCHER_TOUCH_DIAGNOSTICS:true,[flag]:true}).status,0);
});
test('microphone capture is rejected in every profile',()=>{
 for(const profile of [{},factory,{...factory,WATCHER_FACTORY_ACCEPTANCE:true}]){
  const denied=configure({...profile,WATCHER_MIC_CAPTURE:true});assert.notEqual(denied.status,0);assert.match(denied.text,/Microphone capture has been removed/);
 }
 const output=configure(factory);assert.equal(output.status,0,output.text);assert.doesNotMatch(output.text,/WATCHER_MIC_CAPTURE=1/);
});
test('normal automatic haptics requires explicit software-cutoff acceptance',()=>{
 const denied=configure({...factory,WATCHER_HAPTICS:true});assert.notEqual(denied.status,0);assert.match(denied.text,/requires explicit acceptance/);
 const approved=configure({...factory,WATCHER_HAPTICS:true,WATCHER_HAPTICS_SOFTWARE_CUTOFF:true});assert.equal(approved.status,0,approved.text);assert.match(approved.text,/WATCHER_HAPTICS=1/);
});
test('automatic haptics cannot be mixed with acceptance or legacy motor tests',()=>{
 for(const flag of ['WATCHER_FACTORY_ACCEPTANCE','WATCHER_FACTORY_HAPTIC_ACCEPTANCE','WATCHER_SUPERVISED_MOTOR_TEST']){
  const result=configure({...factory,WATCHER_HAPTICS:true,WATCHER_HAPTICS_SOFTWARE_CUTOFF:true,[flag]:true});assert.notEqual(result.status,0);assert.match(result.text,/excludes\s+acceptance\/probe modes/);
 }
 const result=configure({WATCHER_HAPTICS:true,WATCHER_HAPTICS_SOFTWARE_CUTOFF:true});assert.notEqual(result.status,0);
});
test('default is silent and isolated combined acceptance stays available without automatic haptics',()=>{
 const defaults=configure({});assert.equal(defaults.status,0,defaults.text);assert.ok(!defaults.text.includes('WATCHER_HAPTICS=1'));
 const acceptance=configure({...factory,WATCHER_FACTORY_ACCEPTANCE:true,WATCHER_FACTORY_HAPTIC_ACCEPTANCE:true});assert.equal(acceptance.status,0,acceptance.text);assert.match(acceptance.text,/WATCHER_FACTORY_HAPTIC_ACCEPTANCE=1/);assert.ok(!acceptance.text.includes('PRIVATE;WATCHER_HAPTICS=1'));
});
