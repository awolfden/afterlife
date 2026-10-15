import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync,writeFileSync,chmodSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {crc32} from '../host/wire.mjs';
import {timingFields,timingQuery,timingReply,validTimingSnapshot} from '../host/touch-timing.mjs';
import {TouchUsbReader} from '../host/touch-usb.mjs';
import {inspectTouchTiming,timingInspectionRequest,validTimingStatus} from '../host/touch-status.mjs';
import {runHost} from '../host/run.mjs';
import {AttentionModel} from '../host/attention.mjs';
import {startHubServer} from '../host/hub-server.mjs';
const pause=ms=>new Promise(r=>setTimeout(r,ms));
function reply({boot=123,request=7,seq=42,rendered=40,values=new Array(48).fill(0),payload}={}){
 const b=Buffer.alloc(96);values.forEach((n,i)=>b.writeUInt16LE(n,i*2));
 const prefix=`TD3 ${boot} ${request} ${seq} ${rendered} ${payload??b.toString('base64')} `;return prefix+crc32(prefix);
}
function compile(dir,name,source,flags=[]){
 const binary=join(dir,name),result=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',...flags,fileURLToPath(new URL(source,import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(result.status,0,result.stderr);
 const run=spawnSync(binary,[],{encoding:'utf8',timeout:60000});assert.equal(run.status,0,run.stdout+run.stderr);return run.stdout;
}
const flags=['-DWATCHER_TOUCH_DIAGNOSTICS=1','-DWATCHER_TOUCH_USB_DIAGNOSTICS=1','-DWATCHER_TOUCH_TIMING_DIAGNOSTICS=1'];
test('native timing counters, all cancellation causes, saturation, clocks, gate and worst-case C++/JS packet',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-timing-'));
 try{const lines=compile(dir,'timing','touch-timing.cpp',flags).trim().split('\n');assert.equal(lines.length,2);for(const line of lines){assert.ok(line.length<192);assert.ok(timingReply(line));}const largest=timingReply(lines[0]);assert.equal(largest.loopMaxMs,65535);assert.equal(largest.boot,0xffffffff);assert.equal(largest.reasonAndReleaseFlags,0x0f09);const {request,...snapshot}=largest;assert.ok(validTimingSnapshot(snapshot));assert.ok(JSON.stringify({v:1,op:'touch-timing-inspection',phase:'ok',snapshot}).length<2048);}
 finally{rmSync(dir,{recursive:true,force:true});}
});
test('timing instrumentation preserves full deterministic sampler/swipe traces with and without measured scaling',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-timing-traces-'));
 try{for(const measured of [false,true]){
  const extra=measured?['-DWATCHER_TOUCH_MEASURED_SCALE=1']:[];
  const a=JSON.parse(compile(dir,'off','touch-clean.cpp',extra)),b=JSON.parse(compile(dir,'on','touch-clean.cpp',[...extra,...flags]));
  assert.equal(a.trace,b.trace);assert.equal(a.observations,b.observations);assert.equal(a.events,8);assert.equal(b.events,8);assert.ok(a.observations>1000);
 }}finally{rmSync(dir,{recursive:true,force:true});}
});
test('strict canonical codec rejects malformed identities/base64/CRC/flags/shapes without exposing coordinates',()=>{
 assert.equal(timingFields.length,48);const parsed=timingReply(reply());assert.ok(parsed);const {request,...snapshot}=parsed;assert.ok(validTimingSnapshot(snapshot));
 for(const line of [reply()+' ',reply()+'\n',reply().slice(0,-1),reply().replace('TD3','D2'),reply({boot:0}),reply({request:0}),reply({seq:0}),reply({rendered:43}),reply({boot:4294967296}),reply({payload:'A'.repeat(127)}),reply({payload:'!'.repeat(128)}),'x'.repeat(192)])assert.equal(timingReply(line),null);
 for(const [slot,value] of [[17,32],[18,256],[19,256],[43,10],[43,0x1000]]){const values=new Array(48).fill(0);values[slot]=value;assert.equal(timingReply(reply({values})),null);}
 assert.ok(!validTimingSnapshot({...snapshot,x:1}));assert.ok(!validTimingStatus({v:1,op:'touch-timing-inspection',phase:'ok',snapshot:{...snapshot,content:'no'}}));assert.throws(()=>timingQuery(0,1));
 assert.ok(timingInspectionRequest({v:1,op:'touch-timing',seq:1}));assert.ok(!timingInspectionRequest({v:1,op:'touch-timing',seq:1,command:'Q1'}));
});
test('timing reader shares one pending/rate gate and rejects wrong boot/nonce/seq/epoch/version and replay',async()=>{
 let at=0,context={boot:123,seq:42,epoch:1},sent=[];
 const reader=new TouchUsbReader({now:()=>at,context:()=>context,send:s=>{sent.push(s);return true;},seed:6});
 assert.deepEqual(await reader.request({version:3,mode:1}),{phase:'unavailable'});
 const pending=reader.request({version:3});assert.ok(sent[0].startsWith('TQ3 '));assert.deepEqual(await reader.request(),{phase:'busy'});
 for(const options of [{boot:124},{request:8},{seq:41},{seq:43}])assert.equal(reader.receive(reply(options)),false);
 assert.equal(reader.receive(reply()),true);assert.equal((await pending).phase,'ok');assert.equal(reader.receive(reply()),false);
 assert.deepEqual(await reader.request({version:3}),{phase:'busy'});
 at=1000;const next=reader.request({version:3});context={...context,epoch:2};assert.equal(reader.receive(reply({request:8})),false);assert.equal((await next).phase,'unavailable');
 at=2000;const legacy=reader.request();assert.equal(reader.receive(reply({request:9})),false);reader.reset();assert.equal((await legacy).phase,'unavailable');
});
test('timing reader link loss, reversed clock, late response and failed sends are bounded and do not retry',async()=>{
 for(const mode of ['loss','clock','late','send']){
  let at=100,context={boot:123,seq:42,epoch:1},sends=0;
  const reader=new TouchUsbReader({now:()=>at,context:()=>context,send:()=>{sends++;return mode!=='send';},seed:6});
  const p=reader.request({version:3});if(mode==='loss')context=null;if(mode==='clock')at=99;if(mode==='late')at=1601;
  if(mode!=='send')assert.equal(reader.receive(reply()),false);assert.equal((await p).phase,'unavailable');assert.equal(sends,1);
 }
});
test('timing private inspection does not register sessions and rejects unsafe socket permissions',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'timing-hub-')),model=new AttentionModel();const {request,...snapshot}=timingReply(reply());let calls=0;
 const hub=await startHubServer(model,dir,()=>0,{inspectTiming:()=>{calls++;return {phase:'ok',snapshot};}});
 t.after(async()=>{await hub.close();rmSync(dir,{recursive:true,force:true});});
 assert.equal((await inspectTouchTiming(hub.path)).phase,'ok');assert.equal(calls,1);assert.equal(model.snapshot().sessions.length,0);
 chmodSync(hub.path,0o666);assert.equal(await inspectTouchTiming(hub.path),null);chmodSync(hub.path,0o600);
});
for(const mode of ['off','legacy','timing'])test(`real host timing gate=${mode}, private inspection has no delivery/session authority`,async t=>{
 const directory=mkdtempSync(join(tmpdir(),'timing-host-')),helper=join(directory,'fake-python'),received=join(directory,'received');writeFileSync(received,'');
 const payload=Buffer.alloc(96).toString('base64');
 writeFileSync(helper,`#!/usr/bin/env node\nconst fs=require('node:fs');const crc=${crc32.toString()};let seq=0,buffer='';const send=p=>process.stdout.write(p+crc(p)+'\\n');let reports=0;const status=()=>{send('D2 123 '+seq+' '+seq+' 0 0 0 0 29 ');send('R3 123 '+(++reports)+' 1000 2 3 0 4000 16667 100 1000 2000 24 50000 10000 0 ');};status();process.stdin.on('data',d=>{fs.appendFileSync(${JSON.stringify(received)},d);buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const l=buffer.slice(0,i);buffer=buffer.slice(i+1);const p=l.split(' ');if(p[0]==='W1'){seq=Number(p[2]);status();}else if(p[0]==='Q1')status();else if(p[0]==='TQ3')send('TD3 123 '+p[2]+' '+seq+' '+seq+' ${payload} ');else if(p[0]==='TQ4')for(let page=0;page<4;page++)send('TD4 123 '+p[2]+' '+seq+' '+seq+' '+page+' ${payload} ');}});\n`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper,touchDiagnostics:mode!=='off',touchTiming:mode==='timing'});
 t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 const deadline=Date.now()+3000;while((!host.status().ready||(mode==='timing'&&!host.timingStatus()?.motion))&&Date.now()<deadline)await pause(25);assert.ok(host.status().ready);
 const diagnostic=host.timingStatus();
 if(mode==='timing'){assert.equal(diagnostic.fresh,true);assert.ok(Number.isInteger(diagnostic.rosterGeneration));assert.ok(Number.isInteger(diagnostic.linkGeneration));assert.equal(diagnostic.motion.boot,123);assert.equal(diagnostic.motion.frames,2);assert.equal(diagnostic.motion.reused,3);}else assert.equal(diagnostic,null);
 const before=host.snapshot(),ack=host.status().buttonAck;assert.ok(!readFileSync(received,'utf8').includes('TQ4'));
 const s=await inspectTouchTiming(host.socket);assert.equal(s.phase,mode==='timing'?'ok':'disabled');assert.equal(host.snapshot().sessions.length,before.sessions.length);assert.equal(host.status().buttonAck,ack);assert.equal(host.status().serialRestarts,0);
 assert.equal(readFileSync(received,'utf8').split('\n').filter(s=>s.startsWith('TQ4 ')).length,mode==='timing'?1:0);
});
test('instrumentation is compile-time opt-in, no overlay/threshold/backoff/control changes',()=>{
 const main=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8'),cmake=readFileSync(new URL('../firmware/pet/main/CMakeLists.txt',import.meta.url),'utf8'),swipe=readFileSync(new URL('../firmware/pet/main/swipe.h',import.meta.url),'utf8'),sampler=readFileSync(new URL('../firmware/pet/main/touch.h',import.meta.url),'utf8');
 assert.match(cmake,/option\(WATCHER_TOUCH_TIMING_DIAGNOSTICS[^\n]+OFF\)/);assert.match(main,/defined\(WATCHER_TOUCH_DIAGNOSTICS\) && !defined\(WATCHER_TOUCH_TIMING_DIAGNOSTICS\)/);
 assert.match(swipe,/now-last_>100/);assert.match(sampler,/next_=now\+250/);assert.match(main,/if\(!allowed\)return/);assert.match(main,/vTaskDelay\(pdMS_TO_TICKS\(5\)\+1\)/);
 for(const slot of [38,39,40,41])assert.ok(main.includes(`touch_timing.phase(${slot},`));
 assert.ok(!main.includes('touch_timing.phase(42,')); // Former capture slot remains reserved/zero.
 const timing=readFileSync(new URL('../firmware/pet/main/touch-timing.h',import.meta.url),'utf8');assert.doesNotMatch(timing,/malloc|new |vTaskDelay|reg_write|reg_read|esp_restart|network.send/);
});
