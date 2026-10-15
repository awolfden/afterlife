import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {crc32} from '../host/wire.mjs';
import {timingFullQuery,timingFullPage,validFullTimingSnapshot,decodeFullTiming} from '../host/touch-timing.mjs';
import {TouchUsbReader} from '../host/touch-usb.mjs';
function page(p,{seq=42,rendered=40,request=7,boot=123,values=new Array(48).fill(0)}={}){const b=Buffer.alloc(96);values.forEach((v,i)=>b.writeUInt16LE(v,2*i));const head=`TD4 ${boot} ${request} ${seq} ${rendered} ${p} ${b.toString('base64')} `;return head+crc32(head);}
test('sanitized native extended evidence and worst-case four-page C++/JS framing',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-full-'));
 try{
  const binary=join(dir,'full'),build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DWATCHER_TOUCH_DIAGNOSTICS=1','-DWATCHER_TOUCH_USB_DIAGNOSTICS=1','-DWATCHER_TOUCH_TIMING_DIAGNOSTICS=1',fileURLToPath(new URL('touch-timing-full.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:60000});assert.equal(run.status,0,run.stderr);
  const pages=run.stdout.trim().split('\n').map(timingFullPage);assert.equal(pages.length,4);assert.ok(pages.every(Boolean));
  const s={format:4,boot:pages[0].boot,seq:pages[0].seq,rendered:pages[0].rendered,values:pages.flatMap(p=>p.values)};assert.ok(validFullTimingSnapshot(s));assert.ok(JSON.stringify({v:1,op:'touch-timing-inspection',phase:'ok',snapshot:s}).length<2048);
  assert.equal(decodeFullTiming(s).slow.ingress.count,65535);assert.equal(decodeFullTiming(s).core.renderAttempts,65535);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('timing-off firmware rejects both diagnostic timing query versions',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-query-off-'));
 try{const binary=join(dir,'clean'),build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DWATCHER_TOUCH_DIAGNOSTICS=1',fileURLToPath(new URL('touch-query-clean.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:'utf8',timeout:60000});assert.equal(run.status,0,run.stderr);}finally{rmSync(dir,{recursive:true,force:true});}
});
test('complete same-snapshot pages only; duplicates, mismatches, timeout/reset and stale epoch fail closed',async()=>{
 for(const failure of ['none','duplicate','identity','partial','epoch']){
  let at=0,context={boot:123,seq:43,epoch:1};const sent=[];
  const reader=new TouchUsbReader({now:()=>at,context:()=>context,send:s=>{sent.push(s);return true;},seed:6});
  // Query seq must be<=reply seq; capture42 then permit43 to test mixed snapshots.
  context.seq=42;const pending=reader.request({version:4});context={...context,seq:43};assert.ok(sent[0].startsWith('TQ4 '));
  assert.equal(reader.receive(page(2)),true);assert.deepEqual(await reader.request(),{phase:'busy'});
  if(failure==='duplicate')assert.equal(reader.receive(page(2)),false);
  else if(failure==='identity')assert.equal(reader.receive(page(1,{seq:43})),false);
  else if(failure==='partial'){reader.receive(page(1));reader.reset();}
  else if(failure==='epoch'){context={...context,epoch:2};assert.equal(reader.receive(page(1)),false);}
  else{for(const n of [0,3,1])assert.equal(reader.receive(page(n)),true);}
  const result=await pending;assert.equal(result.phase,failure==='none'?'ok':'unavailable');if(failure==='none')assert.ok(validFullTimingSnapshot(result.snapshot));assert.equal(sent.length,1);
 }
});
test('extended codec rejects malformed page, CRC, identity, flags and sparse arrays',()=>{
 assert.throws(()=>timingFullQuery(0,1));assert.ok(timingFullQuery(1,2).startsWith('TQ4 1 2 '));
 for(const bad of [page(4),page(0)+'\n',page(0)+' ',page(0).slice(0,-1),page(0,{boot:0}),page(0,{rendered:43})])assert.equal(timingFullPage(bad),null);
 const s={format:4,boot:123,seq:42,rendered:40,values:new Array(192).fill(0)};assert.ok(validFullTimingSnapshot(s));
 for(const [i,value] of [[51,512],[52,512],[80,8],[128,16],[156,512],[157,2],[158,2],[160,10]]){const values=s.values.slice();values[i]=value;assert.ok(!validFullTimingSnapshot({...s,values}));}
 assert.ok(!validFullTimingSnapshot({...s,values:new Array(192)}));assert.ok(!validFullTimingSnapshot({...s,session:'never'}));
});
