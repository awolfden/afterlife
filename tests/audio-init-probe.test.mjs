import test from 'node:test';
import assert from 'node:assert/strict';
import {AttentionModel} from '../host/attention.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {crc32,encodeAudioInit} from '../host/wire.mjs';
const ledger={muted:false,claim:()=>[],acknowledged:()=>false};
function status(boot,seq){const p=`D2 ${boot} ${seq} ${seq} 0 0 0 0 0 `;return p+crc32(p);}
function fixture(options){const m=new AttentionModel();m.register('a'.repeat(64));m.status('a'.repeat(64),{state:'idle'});return {m,c:new UsbController(m,ledger,options)};}
test('normal bridge never initializes audio; probe is explicit and waits for two-way proof',()=>{
 for(const audioInitProbe of [false,true]){
  const {c}=fixture({audioInitProbe});c.observe(status(1,0),0);
  assert.equal(c.tick(1).includes('A1 '),false);c.observe(status(1,1),2);
  const line=c.tick(3);assert.equal(line.includes('A1 '),audioInitProbe);
  if(audioInitProbe)assert.ok(line.endsWith(encodeAudioInit(1,2)));
  assert.equal(line.includes('C1 '),false);assert.equal(c.tick(4).includes('A1 '),false);
  c.disconnected();c.observe(status(2,0),5);c.tick(6);c.observe(status(2,1),7);
  assert.equal(c.tick(8).includes('A1 '),false);
 }
});
test('muted sessions cannot trigger probe; sound and silent probe are mutually exclusive',()=>{
 const {m,c}=fixture({audioInitProbe:true});m.mute(true);c.observe(status(1,0),0);c.tick(1);c.observe(status(1,1),2);
 assert.equal(c.tick(3).includes('A1 '),false);assert.equal(c.status().audioInitSent,false);
 assert.throws(()=>fixture({audioInitProbe:true,completionChime:true}),/completionChime is retired/);
});
