import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,writeFileSync,chmodSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createConnection,createServer} from 'node:net';
import {once} from 'node:events';
import {runHost} from '../host/run.mjs';
import {startHubServer} from '../host/hub-server.mjs';
import {AttentionModel} from '../host/attention.mjs';
import {inspectHost,inspectionRequest,validHostStatus,describeHostStatus} from '../host/host-status.mjs';
const id='a'.repeat(64),delay=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn){for(let i=0;i<100;i++){if(fn())return;await delay(25);}throw Error('Fixture deadline');}
const status={v:1,op:'inspection',phase:'confirmed',transport:'usb',sessions:1,session:'selected',serialRestarts:0};
test('inspection accepts only fixed bounded metadata and never equates IPC with delivery',()=>{
 assert.ok(inspectionRequest({v:1,op:'inspect',seq:1,id}));assert.ok(inspectionRequest({v:1,op:'inspect',seq:1,id:null}));
 for(const request of [{v:1,op:'inspect',seq:2,id},{v:1,op:'inspect',seq:1,id:'private'},{v:1,op:'inspect',seq:1,id,prompt:'no'}])assert.equal(inspectionRequest(request),false);
 assert.ok(validHostStatus(status));
 for(const value of [{...status,prompt:'no'},{...status,sessions:17},{...status,phase:'ready'},{...status,transport:'cloud'},{...status,serialRestarts:-1},{...status,phase:'ipc-only'}])assert.equal(validHostStatus(value),false);
 assert.match(describeHostStatus({...status,phase:'waiting-device'}),/waiting for device status/);
 assert.match(describeHostStatus({...status,phase:'handshaking'}),/not confirmed/);
 assert.match(describeHostStatus({...status,phase:'ipc-only',transport:'none'}),/no device transport/);
 assert.match(describeHostStatus(null),/not confirmed/);
});
test('private inspection reports current registration without creating sessions or changing selection',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watch-status-')),host=await runHost({directory,ipcOnly:true});
 let source;t.after(async()=>{source?.destroy();await host.close();rmSync(directory,{recursive:true,force:true});});
 let s=await inspectHost(host.socket,id);assert.equal(s.phase,'ipc-only');assert.equal(s.session,'unwatched');assert.equal(host.snapshot().sessions.length,0);
 source=createConnection(host.socket);source.resume();await once(source,'connect');source.write(JSON.stringify({v:1,seq:1,op:'hello',id})+'\n');await until(()=>host.snapshot().sessions.length===1);
 const before=host.snapshot();s=await inspectHost(host.socket,id);assert.equal(s.session,'selected');assert.equal(s.sessions,1);assert.deepEqual(host.snapshot(),before);
 assert.equal((await inspectHost(host.socket,'b'.repeat(64))).session,'unwatched');
 await host.close();assert.equal(await inspectHost(host.socket,id),null);
});
test('inspection times out on a silent peer and rejects unsafe socket permissions',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watch-status-timeout-')),path=join(directory,'watcher.sock');let peer;
 const server=createServer(s=>{peer=s;});await new Promise(r=>server.listen(path,r));chmodSync(path,0o600);
 t.after(async()=>{peer?.destroy();await new Promise(r=>server.close(r));rmSync(directory,{recursive:true,force:true});});
 assert.equal(await inspectHost(path,null,{timeoutMs:30}),null);peer?.destroy();
 chmodSync(path,0o666);assert.equal(await inspectHost(path),null);
});
test('inspection is silent on unsupported hosts and isolated from voice offers or pipelined hello',async()=>{
 for(const supported of [false,true]){
  const directory=mkdtempSync(join(tmpdir(),'watch-inspect-voice-')),model=new AttentionModel();
  const hub=await startHubServer(model,directory,()=>0,{...(supported?{inspect:()=>status}:{})});
  try{
   assert.deepEqual(await inspectHost(hub.path),supported?status:null);assert.equal(model.snapshot().sessions.length,0);
   if(supported){const socket=createConnection(hub.path);let data='';socket.on('data',b=>{data+=b;});await once(socket,'connect');socket.write(JSON.stringify({v:1,op:'inspect',seq:1,id:null})+'\n'+JSON.stringify({v:1,op:'hello',seq:1,id})+'\n');await once(socket,'close');assert.deepEqual(JSON.parse(data),status);assert.equal(model.snapshot().sessions.length,0);}
  }finally{await hub.close();rmSync(directory,{recursive:true,force:true});}
 }
});
test('persistently silent USB gets at most six queries without reset or helper reopen',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watch-query-cap-')),helper=join(directory,'fake-python'),received=join(directory,'received');writeFileSync(received,'');
 writeFileSync(helper,`#!/usr/bin/env node\nconst fs=require('node:fs');process.stdin.on('data',d=>fs.appendFileSync(${JSON.stringify(received)},d));\n`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper});t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 await delay(3400);const before=readFileSync(received,'utf8');assert.deepEqual(before.trim().split('\n'),Array(6).fill('Q1'));
 await delay(600);assert.equal(readFileSync(received,'utf8'),before);assert.equal((await inspectHost(host.socket)).phase,'waiting-device');assert.equal(host.status().serialRestarts,0);
});
test('the six-query budget survives a silent helper restart',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watch-query-restart-')),helper=join(directory,'fake-python'),received=join(directory,'received'),started=join(directory,'started');writeFileSync(received,'');
 writeFileSync(helper,`#!/usr/bin/env node\nconst fs=require('node:fs'),first=!fs.existsSync(${JSON.stringify(started)});fs.writeFileSync(${JSON.stringify(started)},'started');let count=0;process.stdin.on('data',d=>{fs.appendFileSync(${JSON.stringify(received)},d);count+=(d.toString().match(/Q1/g)||[]).length;if(first&&count>=3)process.exit(2);});\n`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper});t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 await delay(5000);assert.deepEqual(readFileSync(received,'utf8').trim().split('\n'),Array(6).fill('Q1'));assert.equal(host.status().serialRestarts,1);
 await delay(600);assert.deepEqual(readFileSync(received,'utf8').trim().split('\n'),Array(6).fill('Q1'));
});
test('USB startup actively queries a silent fake device and confirms only after handshake',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watch-query-')),helper=join(directory,'fake-python');
 const wire=new URL('../host/wire.mjs',import.meta.url).href;
 writeFileSync(helper,`#!/usr/bin/env node
(async()=>{const {crc32}=await import(${JSON.stringify(wire)});let seq=0,buffer='';function report(){const p='D2 123 '+seq+' '+seq+' 0 0 0 0 0 ';process.stdout.write(p+crc32(p)+'\\n');}process.stdin.on('data',d=>{buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const line=buffer.slice(0,i);buffer=buffer.slice(i+1);const f=line.split(' ');if(line==='Q1')report();else if(f[0]==='W1'){seq=Number(f[2]);report();}}});})();
`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper});
 t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 const initial=await inspectHost(host.socket);assert.equal(initial.phase,'waiting-device');
 await until(()=>host.status().ready);assert.equal((await inspectHost(host.socket)).phase,'confirmed');assert.equal(host.status().serialRestarts,0);
});
