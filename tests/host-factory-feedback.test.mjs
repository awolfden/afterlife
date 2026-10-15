import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {runHost} from '../host/run.mjs';
import {factoryFeedbackStatus,deviceStatus,crc32} from '../host/wire.mjs';
const packet=p=>p+crc32(p)+'\n',delay=ms=>new Promise(r=>setTimeout(r,ms));
const report=(fields='123 1 0 1 0 90 1')=>packet(`F1 ${fields} `).trim();
test('factory telemetry is bounded, CRC checked, immutable and explicitly not physical proof',()=>{
 const data=factoryFeedbackStatus(report());assert.equal(data.audioDrains,1);assert.equal(data.closed,true);assert.equal(data.physicalConfirmed,false);assert.ok(Object.isFrozen(data));assert.equal(deviceStatus(report()),null);
 for(const fields of ['0 1 0 1 0 90 1','123 0 0 1 0 90 1','123 1 128 1 0 90 1','123 1 0 1 0 90 2','123 1 0 1 0 0 1','123 1 0 4294967296 0 90 1'])assert.equal(factoryFeedbackStatus(report(fields)),null);
 assert.equal(factoryFeedbackStatus(report()+'0'),null);assert.equal(factoryFeedbackStatus('F1 '+'0'.repeat(200)),null);
});
test('F1 cannot refresh link liveness and is discarded for other boots or disconnect',{timeout:7000},async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-factory-host-')),helper=join(directory,'fake-python');
 const hello=packet('D2 123 0 0 0 0 0 0 0 '),ack=packet('D2 123 1 1 0 0 0 0 4 ');
 const reports=report()+'\n'+report('999 1 0 1 0 999 1')+'\n';
 writeFileSync(helper,`#!/usr/bin/env node\nprocess.stdout.write(${JSON.stringify(hello)});process.stdin.once('data',()=>{process.stdout.write(${JSON.stringify(ack+reports)});setInterval(()=>process.stdout.write(${JSON.stringify(reports)}),100);});\n`,{mode:0o700});
 const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper});
 t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 const deadline=Date.now()+3500;while(!host.status().factoryFeedback&&Date.now()<deadline)await delay(25);
 assert.equal(host.status().factoryFeedback?.boot,123);assert.equal(host.status().factoryFeedback?.dmaBlocks,90);
 await delay(1800);assert.equal(host.status().receiving,false);assert.equal(host.status().factoryFeedback,null);
});
