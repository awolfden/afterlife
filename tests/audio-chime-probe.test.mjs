import test from 'node:test';
import assert from 'node:assert/strict';
import {AttentionModel} from '../host/attention.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {crc32,encodeAudioChime} from '../host/wire.mjs';
const ledger={muted:false,claim:()=>[],acknowledged:()=>false};
function status(boot,seq){const p=`D2 ${boot} ${seq} ${seq} 0 0 0 0 0 `;return p+crc32(p);}
function fixture(options){const m=new AttentionModel();m.register('a'.repeat(64));m.status('a'.repeat(64),{state:'idle'});return {m,c:new UsbController(m,ledger,options)};}
test('B1 requires explicit probe, two-way proof, live session and unmuted state; never replayed',()=>{
 for(const audioChimeProbe of [false,true]){
  const {c}=fixture({audioChimeProbe});c.observe(status(1,0),0);assert.equal(c.tick(1).includes('B1 '),false);c.observe(status(1,1),2);
  const line=c.tick(3);assert.equal(line.includes('B1 '),audioChimeProbe);if(audioChimeProbe)assert.ok(line.endsWith(encodeAudioChime(1,2)));
  assert.equal(line.includes('A1 '),false);assert.equal(line.includes('C1 '),false);assert.equal(c.tick(4).includes('B1 '),false);
  c.disconnected();c.observe(status(2,0),5);c.tick(6);c.observe(status(2,1),7);assert.equal(c.tick(8).includes('B1 '),false);
 }
});
test('audible probe respects mute and rejects automatic/silent combinations',()=>{
 const {m,c}=fixture({audioChimeProbe:true});m.mute(true);c.observe(status(1,0),0);c.tick(1);c.observe(status(1,1),2);
 assert.equal(c.tick(3).includes('B1 '),false);assert.equal(c.status().audioChimeSent,false);assert.equal(c.status().audioChimeSuppressed,true);
 m.mute(false);assert.equal(c.tick(4).includes('B1 '),false);
 for(const extra of [{audioInitProbe:true},{completionChime:true}])assert.throws(()=>fixture({audioChimeProbe:true,...extra}),/must run alone|completionChime is retired/);
});
