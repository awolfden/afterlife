import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,readFileSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createConnection} from 'node:net';
import {once} from 'node:events';
import {runHost} from '../host/run.mjs';
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(predicate){const end=Date.now()+6000;while(!predicate()){if(Date.now()>end)throw new Error('Test deadline');await delay(25);}}
test('serial helper exit preserves hub, root state and ledger; replacement handshakes',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-reconnect-')),marker=join(directory,'attempts'),trigger=join(directory,'disconnect'),helper=join(directory,'fake-python');
 const wire=new URL('../host/wire.mjs',import.meta.url).href;
 writeFileSync(helper,`#!/usr/bin/env node
(async()=>{const fs=require('node:fs');const {crc32}=await import(${JSON.stringify(wire)});const marker=${JSON.stringify(marker)},trigger=${JSON.stringify(trigger)};const n=fs.existsSync(marker)?Number(fs.readFileSync(marker,'utf8'))+1:1;fs.writeFileSync(marker,String(n));let seq=0,buffer='';function report(){const p='D2 '+(122+n)+' '+seq+' '+seq+' 0 0 0 0 0 ';process.stdout.write(p+crc32(p)+'\\n');}setInterval(()=>{if(n===1 && fs.existsSync(trigger))process.exit(1);report();},50);process.stdin.on('data',d=>{buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const line=buffer.slice(0,i);buffer=buffer.slice(i+1);const f=line.split(' ');if(f[0]==='W1')seq=Number(f[2]);}});})();
`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper});
 let socket;t.after(async()=>{socket?.destroy();await host.close();rmSync(directory,{recursive:true,force:true});});
 await until(()=>host.status().ready);
 socket=createConnection(host.socket);socket.resume();await once(socket,'connect');
 socket.write(JSON.stringify({v:1,seq:1,op:'hello',id:'a'.repeat(64)})+'\n'+JSON.stringify({v:1,seq:2,op:'status',state:'working',children:0,available:true})+'\n');
 await until(()=>host.snapshot().sessions.length===1);
 writeFileSync(trigger,'disconnect only the fake helper');
 await until(()=>host.status().serialRestarts===1);
 assert.equal(existsSync(host.socket),true);assert.equal(existsSync(join(directory,'delivery.lock')),true);
 assert.equal(socket.destroyed,false);assert.equal(host.snapshot().sessions.length,1);
 await until(()=>host.status().ready && host.status().boot===124);
 assert.equal(readFileSync(marker,'utf8'),'2');assert.equal(host.status().receiving,true);
 assert.equal(host.status().fault,null);assert.deepEqual(host.status().lastSerialExit,{code:1,signal:null});
 await host.close();const attempts=readFileSync(marker,'utf8');await delay(700);
 assert.equal(readFileSync(marker,'utf8'),attempts);assert.equal(await host.done,null);
});
