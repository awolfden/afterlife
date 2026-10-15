import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync,writeFileSync,chmodSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createConnection} from 'node:net';
import {crc32} from '../host/wire.mjs';
import {TouchUsbReader,touchReply,touchQuery,touchFields,validTouchSnapshot} from '../host/touch-usb.mjs';
import {inspectTouch,touchInspectionRequest,validTouchStatus} from '../host/touch-status.mjs';
import {inspectHost} from '../host/host-status.mjs';
import {runHost} from '../host/run.mjs';
import {startHubServer} from '../host/hub-server.mjs';
import {AttentionModel} from '../host/attention.mjs';
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const values=[123,7,42,40,4,0,0,2,20,0,2,0,9,1200,1,0,1,0,0,90];
function reply(v=values){const p=`TD1 ${v.join(' ')} `;return p+crc32(p);}
function fixture(){let at=0,context={boot:123,seq:42,epoch:Symbol()},sent=[];const reader=new TouchUsbReader({now:()=>at,context:()=>context,send:s=>{sent.push(s);return true;},seed:6});return {reader,sent,time:n=>{at=n;},context:v=>{context=v;},getContext:()=>context};}
test('native USB gate under ASan/UBSan and C++/JS reply contract',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-touch-usb-'));
 try{const bin=join(dir,'native');const built=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('touch-usb.cpp',import.meta.url)),'-o',bin],{encoding:'utf8',timeout:60000});assert.equal(built.status,0,built.stderr);const run=spawnSync(bin,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stderr);const lines=run.stdout.trim().split('\n');assert.equal(lines.length,2);for(const l of lines)assert.ok(touchReply(l));assert.equal(touchReply(lines[0]).blocked,64);assert.equal(touchReply(lines[1]).contacts,65535);assert.equal(touchReply(lines[1]).boot,0xffffffff);}finally{rmSync(dir,{recursive:true,force:true});}
});
test('strict counter schema rejects CRC/range/shape errors and never contains coordinates',()=>{
 const s=touchReply(reply());assert.equal(s.xRejected,2);assert.equal(s.releases,1200);assert.equal(touchFields.length,20);
 for(const l of [reply()+' ',reply()+'\n',reply().slice(0,-1),reply().replace('TD1','D2'),reply().replace(' 123 ',' -123 '),'x'.repeat(192)])assert.equal(touchReply(l),null);
 for(const [i,v] of [[0,0],[1,0],[2,0],[3,43],[4,8],[5,65536],[14,2],[18,256],[19,65536]]){const a=[...values];a[i]=v;assert.equal(touchReply(reply(a)),null);}
 const {request,...snapshot}=s;assert.ok(validTouchSnapshot(snapshot));assert.ok(!validTouchSnapshot({...snapshot,x:1}));assert.ok(!validTouchStatus({v:1,op:'touch-inspection',phase:'ok',snapshot:{...snapshot,content:'no'}}));assert.throws(()=>touchQuery(0,1));
 assert.ok(touchInspectionRequest({v:1,op:'touch-inspect',seq:1}));assert.ok(!touchInspectionRequest({v:1,op:'touch-inspect',seq:1,command:'Q1'}));
});
test('one pending request, one per second, matching boot/nonce/frame/epoch only; no cached replay',async()=>{
 const f=fixture(),p=f.reader.request();assert.equal(f.sent.length,1);assert.deepEqual(await f.reader.request(),{phase:'busy'});
 for(const [i,v] of [[0,124],[1,8],[2,41],[2,43]]){const a=[...values];a[i]=v;assert.equal(f.reader.receive(reply(a)),false);}
 assert.equal(f.reader.receive(reply()),true);const result=await p;assert.equal(result.phase,'ok');assert.equal(result.snapshot.rejected,2);assert.equal(f.reader.receive(reply()),false);assert.deepEqual(await f.reader.request(),{phase:'busy'});
 f.time(1000);const next=f.reader.request();f.context({...f.getContext(),epoch:Symbol()});assert.equal(f.reader.receive(reply()),false);assert.deepEqual(await next,{phase:'unavailable'});
});
test('timeouts, link loss, bad clocks, transport failure and counter exhaustion fail closed',async()=>{
 for(const mode of ['reset','context','backward','late']){const f=fixture(),p=f.reader.request();if(mode==='reset')f.reader.reset();if(mode==='context'){f.context(null);f.reader.receive(reply());}if(mode==='backward'){f.time(-1);f.reader.receive(reply());}if(mode==='late'){f.time(1501);f.reader.receive(reply());}assert.deepEqual(await p,{phase:'unavailable'});}
 const f=fixture();assert.deepEqual(await f.reader.request(),{phase:'unavailable'});assert.equal(f.sent.length,1); // Actual bounded timeout, no retry.
 const bad=new TouchUsbReader({now:()=>0,context:()=>({boot:123,seq:1,epoch:1}),send:()=>false,seed:1});assert.deepEqual(await bad.request(),{phase:'unavailable'});
 const full=new TouchUsbReader({now:()=>0,context:()=>({boot:123}),send:()=>{throw Error('must not send');},seed:0xffffffff});assert.deepEqual(await full.request(),{phase:'unavailable'});
});
test('private first-message endpoint stays isolated from sessions, voice and legacy inspection',async t=>{
 const dir=mkdtempSync(join(tmpdir(),'touch-hub-')),model=new AttentionModel();let calls=0;const {request,...snapshot}=touchReply(reply());
 const hub=await startHubServer(model,dir,()=>0,{inspectTouch:()=>{calls++;return {phase:'ok',snapshot};}});
 t.after(async()=>{await hub.close();rmSync(dir,{recursive:true,force:true});});
 assert.equal((await inspectTouch(hub.path)).phase,'ok');assert.equal(calls,1);assert.equal(model.snapshot().sessions.length,0);
 await new Promise(resolve=>{const s=createConnection(hub.path);s.on('connect',()=>s.write(JSON.stringify({v:1,op:'touch-inspect',seq:1})+'\n'+JSON.stringify({v:1,op:'hello',seq:1,id:'a'.repeat(64)})+'\n'));s.on('data',()=>{});s.on('close',resolve);s.on('error',()=>{});});assert.equal(model.snapshot().sessions.length,0);
 chmodSync(hub.path,0o666);assert.equal(await inspectTouch(hub.path),null);chmodSync(hub.path,0o600);
});
for(const enabled of [false,true])test(`real host integration: touch opt-in=${enabled}, session/liveness isolation`,async t=>{
 const directory=mkdtempSync(join(tmpdir(),'touch-host-')),helper=join(directory,'fake-python'),received=join(directory,'received');writeFileSync(received,'');
 writeFileSync(helper,`#!/usr/bin/env node\nconst fs=require('node:fs');const crc=${crc32.toString()};let seq=0,buffer='';const send=p=>process.stdout.write(p+crc(p)+'\\n');const status=()=>send('D2 123 '+seq+' '+seq+' 0 0 0 0 29 ');status();process.stdin.on('data',d=>{fs.appendFileSync(${JSON.stringify(received)},d);buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const l=buffer.slice(0,i);buffer=buffer.slice(i+1);const p=l.split(' ');if(p[0]==='W1'){seq=Number(p[2]);status();}else if(p[0]==='Q1')status();else if(p[0]==='TQ1')send('TD1 123 '+p[2]+' '+seq+' '+seq+' 4 0 0 2 20 0 2 0 9 1200 1 0 0 0 64 90 ');}});\n`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper,touchDiagnostics:enabled});t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 const deadline=Date.now()+3000;while(!host.status().ready&&Date.now()<deadline)await delay(25);assert.equal(host.status().ready,true);
 const baseline=host.snapshot(),ack=host.status().buttonAck;assert.equal((await inspectHost(host.socket)).phase,'confirmed');assert.ok(!readFileSync(received,'utf8').includes('TQ1'));
 if(!enabled){assert.equal((await inspectTouch(host.socket)).phase,'disabled');assert.ok(!readFileSync(received,'utf8').includes('TQ1'));return;}
 const s=await inspectTouch(host.socket);assert.equal(s.phase,'ok');assert.equal(s.snapshot.xRejected,2);assert.equal(s.snapshot.releases,1200);assert.equal(host.status().buttonAck,ack);assert.equal(host.snapshot().sessions.length,baseline.sessions.length);assert.equal(host.status().serialRestarts,0);
 assert.equal(readFileSync(received,'utf8').split('\n').filter(l=>l.startsWith('TQ1 ')).length,1);assert.equal((await inspectTouch(host.socket)).phase,'busy');
});
test('firmware source gates keep replies USB-only, opt-in, nonblocking, capture-free and authority-neutral',()=>{
 const main=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8'),cmake=readFileSync(new URL('../firmware/pet/main/CMakeLists.txt',import.meta.url),'utf8');
 assert.match(cmake,/option\(WATCHER_TOUCH_USB_DIAGNOSTICS[^\n]+OFF\)/);
 const allowed=main.slice(main.indexOf('static bool touch_usb_allowed'),main.indexOf('static void send_touch_diagnostic'));assert.match(allowed,/link_source!=LinkSource::usb/);assert.doesNotMatch(allowed,/voice\.|WATCHER_MIC_CAPTURE/);
 const send=main.slice(main.indexOf('static void send_touch_diagnostic'),main.indexOf('static void send_status'));assert.match(send,/usb_serial_jtag_wait_tx_done\(0\)/);assert.match(send,/usb_serial_jtag_write_bytes\(line,length,0\)/);assert.doesNotMatch(send,/\b(?:output|reg_read|reg_write|event)\(/);
 const start=main.indexOf('if(length>=4&&(!memcmp(line,"TQ1 ');assert.ok(start>=0);const query=main.slice(start,main.indexOf('#ifdef WATCHER_GHOST_PAGES',start));assert.match(query,/touch_usb_gate.request/);assert.doesNotMatch(query,/last_frame_ms\s*=|frame\s*=|button_id\s*=|\.control\(|\.start\(/);
});
