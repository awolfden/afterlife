import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,readFileSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';import {join} from 'node:path';import {once} from 'node:events';import {createConnection} from 'node:net';
import {runHost} from '../host/run.mjs';
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn){const end=Date.now()+6500;while(!fn()){if(Date.now()>end)throw new Error('Synthetic family host deadline');await delay(25);}}
test('actual host negotiates G2 only after active D2 proof; G0 cannot keep a silent device alive',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-family-host-')),mode=join(directory,'mode'),trace=join(directory,'trace'),helper=join(directory,'fake-python');
 writeFileSync(mode,'0');
 writeFileSync(helper,`#!/usr/bin/env node
(async()=>{const fs=require('node:fs');const {crc32}=await import(${JSON.stringify(new URL('../host/wire.mjs',import.meta.url).href)});let seq=0,buffer='',writes=0;
const send=p=>process.stdout.write(p+crc32(p)+'\\n');
setInterval(()=>{const mode=fs.readFileSync(${JSON.stringify(mode)},'utf8');if(mode!=='2')send('D2 77 '+seq+' '+seq+' 0 0 0 0 8 ');if(mode!=='0')send('G0 77 '+seq+' 2 6 ');},40);
process.stdin.on('data',d=>{buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const line=buffer.slice(0,i);buffer=buffer.slice(i+1);const parts=line.split(' ');if(parts[0]==='W1')seq=Number(parts[2]);if(/^G[12] /.test(line)&&writes++<200)fs.appendFileSync(${JSON.stringify(trace)},parts[0]+'\\n');}});})();
`,{mode:0o700});
 let socket;const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper});
 t.after(async()=>{socket?.destroy();await host.close();rmSync(directory,{recursive:true,force:true});});
 await until(()=>host.status().ready);socket=createConnection(host.socket);socket.resume();await once(socket,'connect');
 socket.write(JSON.stringify({v:1,seq:1,op:'hello',id:'a'.repeat(64)})+'\n'+JSON.stringify({v:1,seq:2,op:'status',state:'idle',children:0,available:true})+'\n');
 await until(()=>existsSync(trace)&&readFileSync(trace,'utf8').includes('G1'));assert.doesNotMatch(readFileSync(trace,'utf8'),/G2/);
 writeFileSync(mode,'1');await until(()=>readFileSync(trace,'utf8').includes('G2'));assert.equal(host.snapshot().sessions.length,1);
 writeFileSync(mode,'2');await until(()=>!host.status().receiving);assert.equal(host.status().ready,false);assert.equal(host.snapshot().sessions.length,1);assert.equal(host.status().fault,null);
});
