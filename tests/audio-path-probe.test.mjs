import test from 'node:test';
import assert from 'node:assert/strict';
import {audioPathStatus,crc32,deviceStatus} from '../host/wire.mjs';
const line=(fields='12 3 6 65535 0 34 0',hex='80b5180001001000bf08000404000200')=>{const p=`E1 ${fields} ${hex} `;return p+crc32(p);};
test('bounded CRC-checked read-only codec and DMA diagnostics',()=>{
 const data=audioPathStatus(line());assert.equal(data.stage,6);assert.equal(data.dmaBlocks,34);assert.equal(data.registers[8],0xbf);assert.equal(data.registers.length,16);
 assert.equal(deviceStatus(line()),null);assert.ok(Object.isFrozen(data.registers));
 for(const fields of ['0 3 6 65535 0 34 0','12 0 6 65535 0 34 0','12 3 7 65535 0 34 0','12 3 6 65536 0 34 0','12 3 6 0 1 34 0','12 3 6 65535 0 34 1024','12 3 6 65535 0 4294967296 0'])assert.equal(audioPathStatus(line(fields)),null);
 assert.equal(audioPathStatus(line()+'0'),null);assert.equal(audioPathStatus(line().replace('E1','D2')),null);
 assert.equal(audioPathStatus(line(undefined,'00')),null);assert.equal(audioPathStatus('x'.repeat(192)),null);
});
