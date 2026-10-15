import test from 'node:test';
import assert from 'node:assert/strict';
import {Readable,PassThrough} from 'node:stream';
import {readLanInput,lanOptions} from '../tools/watcher-lan.mjs';
test('LAN launcher uses explicit device/Python and rejects retired voice configuration',()=>{
 const base=['--python','/fixture/python','--port','/dev/cu.usbmodemTEST'];
 assert.equal(lanOptions(base).voice,undefined);assert.equal(lanOptions(base).port,'/dev/cu.usbmodemTEST');
 assert.throws(()=>lanOptions([...base,'--voice-helper','/fixture/speech','--voice-sha256','a'.repeat(64)]));
 for(const args of [[],['--python','/fixture/python'],[...base,'--device-serial','aa'],[...base,'--password','secret'],[...base,'--voice-helper','/fixture/speech']])assert.throws(()=>lanOptions(args));
});
test('bounded credential pipe consumes only the LAN envelope and wipes byte buffers',async()=>{
 const value={ssid:'synthetic-network',password:'synthetic-passphrase',address:'192.168.1.2',port:0},buffer=Buffer.from(JSON.stringify(value));
 assert.deepEqual(await readLanInput(Readable.from([buffer])),value);assert.ok(buffer.every(b=>b===0));
 await assert.rejects(readLanInput(Readable.from([Buffer.alloc(1025,65)])),/bound/);
 await assert.rejects(readLanInput(Readable.from([Buffer.from(JSON.stringify({...value,unexpected:true}))])),/fields/);
 await assert.rejects(readLanInput(new PassThrough(),{timeoutMs:20}),/deadline/);
});
