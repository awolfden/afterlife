import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {crc32} from '../host/wire.mjs';
import {TouchUsbReader,touchRangeReply,touchRangeQuery,validRangeSnapshot} from '../host/touch-usb.mjs';
import {inspectTouchRange,rangeInspectionRequest,validRangeStatus} from '../host/touch-status.mjs';
import {runHost} from '../host/run.mjs';
import {startHubServer} from '../host/hub-server.mjs';
import {AttentionModel} from '../host/attention.mjs';
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const base=[123,7,42,42,4,0,0,2,20,0,2,0,9,1200,1,0,1,0,0,90];
const armed=[7,1,0,0,0,0,0,0,1];
function reply(range=armed,b=base){const p=`TD2 ${[...b,...range].join(' ')} `;return p+crc32(p);}
test('range window native bounds, context cancellation, unchanged decoder and wire roundtrip under sanitizers',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-touch-range-'));
 try{const bin=join(dir,'native');const built=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('touch-range.cpp',import.meta.url)),'-o',bin],{encoding:'utf8',timeout:60000});assert.equal(built.status,0,built.stderr);const run=spawnSync(bin,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stderr);const lines=run.stdout.trim().split('\n');assert.equal(lines.length,2);for(const l of lines)assert.ok(touchRangeReply(l),l);assert.equal(touchRangeReply(lines[0]).rangeXMin,350);assert.equal(touchRangeReply(lines[1]).rangeXMax,4095);assert.equal(touchRangeReply(lines[1]).contacts,65535);}finally{rmSync(dir,{recursive:true,force:true});}
});
test('range wire and IPC schemas reject unbounded, inconsistent or arbitrary commands',()=>{
 const {request,...s}=touchRangeReply(reply());assert.ok(validRangeSnapshot(s));assert.ok(validRangeStatus({v:1,op:'touch-range-inspection',phase:'ok',snapshot:s}));
 for(const l of [reply()+' ',reply().replace('TD2','TD1'),reply().slice(0,-1),'x'.repeat(192)])assert.equal(touchRangeReply(l),null);
 for(const [k,v] of Object.entries({rangeState:6,rangeSamples:51,rangeXMax:4096,rangeWindows:11,rangeElapsedMs:1001,extra:1,rangeId:0}))assert.equal(validRangeSnapshot({...s,[k]:v}),false,k);
 assert.equal(validRangeSnapshot({...s,rangeXMin:1}),false);assert.throws(()=>touchRangeQuery(123,7,2));
 assert.ok(rangeInspectionRequest({v:1,op:'touch-range',seq:1,mode:'arm'}));assert.ok(!rangeInspectionRequest({v:1,op:'touch-range',seq:1,mode:'arm',command:'anything'}));assert.ok(!rangeInspectionRequest({v:1,op:'touch-range',seq:1,mode:'reset'}));
});
test('range arm requires matching window identity; shared one-pending budget and no cached response',async()=>{
 for(const accepted of [true,false]){let sent=[];const r=new TouchUsbReader({now:()=>0,context:()=>({boot:123,seq:42,epoch:1}),send:l=>{sent.push(l);return true;},seed:6});const p=r.request({version:2,mode:1});assert.ok(sent[0].startsWith('TQ2 123 7 1 '));assert.equal((await r.request()).phase,'busy');const a=[...armed];if(!accepted)a[0]=6;assert.equal(r.receive(reply(a)),true);assert.equal((await p).phase,accepted?'ok':'unavailable');assert.equal(r.receive(reply()),false);}
 const r=new TouchUsbReader({now:()=>0,context:()=>null,send:()=>{throw Error('must not arm');},seed:6});assert.equal((await r.request({version:2,mode:1})).phase,'unavailable');
});
test('range private first-message endpoint does not register sessions and validates mode',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'range-hub-')),model=new AttentionModel(),modes=[];const {request,...snapshot}=touchRangeReply(reply());
 const hub=await startHubServer(model,dir,()=>0,{inspectRange:mode=>{modes.push(mode);return {phase:'ok',snapshot};}});t.after(async()=>{await hub.close();rmSync(dir,{recursive:true,force:true});});
 assert.equal((await inspectTouchRange(hub.path,{mode:'arm'})).phase,'ok');assert.equal((await inspectTouchRange(hub.path)).phase,'ok');assert.equal(await inspectTouchRange(hub.path,{mode:'reset'}),null);assert.deepEqual(modes,['arm','read']);assert.equal(model.snapshot().sessions.length,0);
});
for(const enabled of [false,true])test(`real host range opt-in=${enabled}, explicit arm/read only`,async t=>{
 const directory=mkdtempSync(join(tmpdir(),'range-host-')),helper=join(directory,'fake-python'),received=join(directory,'received');writeFileSync(received,'');
 writeFileSync(helper,`#!/usr/bin/env node\nconst fs=require('node:fs');const crc=${crc32.toString()};let seq=0,id=0,buffer='';const send=p=>process.stdout.write(p+crc(p)+'\\n');const status=()=>send('D2 123 '+seq+' '+seq+' 0 0 0 0 29 ');status();process.stdin.on('data',d=>{fs.appendFileSync(${JSON.stringify(received)},d);buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const l=buffer.slice(0,i);buffer=buffer.slice(i+1);const p=l.split(' ');if(p[0]==='W1'){seq=Number(p[2]);status();}else if(p[0]==='Q1')status();else if(p[0]==='TQ2'){if(p[3]==='1')id=Number(p[2]);const range=p[3]==='1'?id+' 1 0 0 0 0 0 0 1 ':id+' 3 4 345 355 210 220 1000 1 ';send('TD2 123 '+p[2]+' '+seq+' '+seq+' 4 0 0 2 20 0 2 0 9 1200 1 0 1 0 0 90 '+range);}}});\n`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper,touchDiagnostics:true,touchRanges:enabled});t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});const deadline=Date.now()+3000;while(!host.status().ready&&Date.now()<deadline)await delay(25);assert.equal(host.status().ready,true);
 assert.ok(!readFileSync(received,'utf8').includes('TQ2'));const a=await inspectTouchRange(host.socket,{mode:'arm'});assert.equal(a.phase,enabled?'ok':'disabled');if(!enabled){assert.ok(!readFileSync(received,'utf8').includes('TQ2'));return;}
 assert.equal(a.snapshot.rangeState,1);await delay(1100);const s=await inspectTouchRange(host.socket);assert.equal(s.phase,'ok');assert.equal(s.snapshot.rangeId,a.snapshot.rangeId);assert.equal(s.snapshot.rangeXMax,355);assert.equal(s.snapshot.rangeState,3);assert.equal(host.snapshot().sessions.length,0);assert.equal(host.status().serialRestarts,0);assert.equal(readFileSync(received,'utf8').split('\n').filter(l=>l.startsWith('TQ2 ')).length,2);
});
test('range hooks add no reads, normalize no inputs and are separately default off',()=>{
 const main=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8'),cmake=readFileSync(new URL('../firmware/pet/main/CMakeLists.txt',import.meta.url),'utf8');assert.match(cmake,/option\(WATCHER_TOUCH_RANGE_DIAGNOSTICS[^\n]+OFF\)/);assert.match(cmake,/if\(NOT WATCHER_TOUCH_USB_DIAGNOSTICS\)/);
 const touch=main.slice(main.indexOf('static void touch(int64_t'),main.indexOf('static void text('));assert.equal((touch.match(/readRegister\(/g)??[]).length,1);assert.match(touch,/if\(reg==0x02\).*at=ms\(\).*touch_range.guard.*touch_range.observe/);assert.match(touch,/return ok;/);assert.match(touch,/swipe.update\(sample,context,now\)/);
});
