import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
test('application loop blocks at least one tick instead of starving IDLE0 at 100 Hz',()=>{
 const source=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8');
 assert.match(source,/vTaskDelay\(pdMS_TO_TICKS\(5\)\+1\);/);
 assert.doesNotMatch(source,/vTaskDelay\(pdMS_TO_TICKS\(5\)\);/);
 // Audited FreeRTOS conversion floors integer milliseconds to ticks.
 assert.equal(Math.floor(5*100/1000)+1,1);
 assert.equal(Math.floor(5*1000/1000)+1,6);
});
