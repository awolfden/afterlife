import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {runHost} from '../host/run.mjs';
import {crc32} from '../host/wire.mjs';
const packet=p=>p+crc32(p)+'\n',delay=ms=>new Promise(r=>setTimeout(r,ms));
test('factory diagnostics reject future and regressing sequence numbers',{timeout:5000},async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-factory-order-')),helper=join(directory,'fake-python');
 const hello=packet('D2 123 0 0 0 0 0 0 0 ');
 const first=packet('D2 123 1 1 0 0 0 0 4 ')+packet('F1 123 1 0 1 0 90 1 ')+packet('F1 123 999 0 1 0 1000 1 ');
 const second=packet('D2 123 2 2 0 0 0 0 4 ')+packet('F1 123 2 0 1 0 95 1 ')+packet('F1 123 1 0 1 0 77 1 ')+packet('F1 123 999 0 1 0 1000 1 ');
 writeFileSync(helper,`#!/usr/bin/env node\nlet phase=0,buffer='';process.stdout.write(${JSON.stringify(hello)});process.stdin.on('data',data=>{buffer+=data;let index;while((index=buffer.indexOf('\\n'))>=0){const line=buffer.slice(0,index);buffer=buffer.slice(index+1);if(!line.startsWith('W1 '))continue;if(++phase===1)process.stdout.write(${JSON.stringify(first)});else if(phase===2)process.stdout.write(${JSON.stringify(second)});}});\n`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper});
 t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 const deadline=Date.now()+3500;while(host.status().factoryFeedback?.seq!==2&&Date.now()<deadline)await delay(25);
 assert.equal(host.status().factoryFeedback?.seq,2);assert.equal(host.status().factoryFeedback?.dmaBlocks,95);
});
