import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createConnection} from 'node:net';
import {once} from 'node:events';
import {runHost} from '../host/run.mjs';
test('IPC-only host ingests synthetic roots and closes without device access',async t=>{
 const directory=mkdtempSync(join(tmpdir(),'wh-'));
 const host=await runHost({directory,ipcOnly:true});
 t.after(async()=>{await host.close();rmSync(directory,{recursive:true,force:true});});
 const socket=createConnection(host.socket);await once(socket,'connect');
 const messages=[{v:1,seq:1,op:'hello',id:'1'.repeat(64)},{v:1,seq:2,op:'status',state:'working',children:2,available:true},{v:1,seq:3,op:'end'}];
 socket.write(messages.map(value=>JSON.stringify(value)).join('\n')+'\n');socket.resume();await once(socket,'close');
 assert.equal(host.snapshot().sessions.length,1);assert.equal(host.snapshot().sessions[0].available,false);assert.equal(host.status().boot,null);
 await host.close();assert.equal(await host.done,null);assert.equal(existsSync(join(directory,'delivery.lock')),false);
});
test('device mode refuses implicit discovery or a missing Python executable',async()=>{
 await assert.rejects(runHost({directory:'/unused'}),/Explicit badge/);
 await assert.rejects(runHost({directory:'/unused',ipcOnly:true,port:'/dev/cu.usbmodem2101'}),/must not specify/);
});
