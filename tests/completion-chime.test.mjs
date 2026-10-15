import test from 'node:test';
import assert from 'node:assert/strict';
import {AttentionModel} from '../host/attention.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {CompletionChime} from '../host/completion-chime.mjs';
import {settingsLine,settingsPacket} from '../host/settings-sync.mjs';
import {crc32,deviceStatus,encodeChime} from '../host/wire.mjs';
const id='a'.repeat(64);
function model(){const m=new AttentionModel();m.register(id);m.status(id,{state:'idle'});return m;}
function status(boot,seq,capabilities=4){const p=`D2 ${boot} ${seq} ${seq} 0 0 0 0 ${capabilities} `;return p+crc32(p);}
function run(m){m.status(id,{state:'thinking'});m.status(id,{state:'completed'});}
const ledger={muted:false,claim:()=>[],acknowledged:()=>false};

test('only live busy-to-completed edges count, not replayed cached completion or failed/aborted runs',()=>{
 const m=model();m.status(id,{state:'completed'});assert.equal(m.snapshot().sessions[0].completions,0);
 run(m);m.status(id,{state:'completed'});assert.equal(m.snapshot().sessions[0].completions,1);
 m.status(id,{state:'working'});m.status(id,{state:'working',available:false});m.status(id,{state:'completed'});
 assert.equal(m.snapshot().sessions[0].completions,1);
 m.status(id,{state:'thinking'});m.status(id,{state:'idle'});m.status(id,{state:'failed'});
 assert.equal(m.snapshot().sessions[0].completions,1);
});
test('policy consumes mute/offline events and baselines attach/reconnect; never replays',()=>{
 const m=model(),c=new CompletionChime();
 assert.equal(c.take(m.snapshot(),true),false);
 run(m);assert.equal(c.take(m.snapshot(),true),true);assert.equal(c.take(m.snapshot(),true),false);
 m.mute(true);run(m);assert.equal(c.take(m.snapshot(),true),false);
 m.mute(false);assert.equal(c.take(m.snapshot(),true),false);
 run(m);assert.equal(c.take(m.snapshot(),false),false);assert.equal(c.take(m.snapshot(),true),false);
 run(m);c.reset();assert.equal(c.take(m.snapshot(),true),false);
});
test('controller emits one CRC-protected command with persisted preference, capability and settings proof',()=>{
 for(const enabled of [false,true])for(const caps of [0,4]){
  const settings={failed:false,snapshot:()=>({version:1,revision:1,vibration:false,sounds:enabled,animations:true,cycleSessions:false,brightness:160})};
  const m=model(),c=new UsbController(m,ledger,{settings});
  c.observe(status(99,0,caps),0);assert.equal(c.tick(1).includes('C1'),false);
  c.observe(status(99,1,caps),2);
  c.observeSettings(settingsLine('S0',[99,1,1]).trimEnd(),3);
  const restore=c.tick(4).trim().split('\n').find(line=>line.startsWith('S1 '));const echo=settingsPacket(restore);
  c.observe(status(99,2,caps),5);c.observeSettings(settingsLine('S3',[99,2,echo.token,1,0]).trimEnd(),6);run(m);
  const lines=c.tick(7).trim().split('\n');
  assert.equal(lines.length,enabled&&caps===4?2:1);
  if(lines.length===2)assert.equal(lines[1]+'\n',encodeChime(99,3));
  assert.equal(c.tick(8).includes('C1'),false);
  c.disconnected();run(m);assert.equal(c.tick(9).includes('C1'),false);
  c.observe(status(100,0,caps),10);c.tick(11);c.observe(status(100,1,caps),12);
  assert.equal(c.tick(13).includes('C1'),false);
 }
});
test('completion on a nonselected session still chimes; paging never does',()=>{
 const m=model(),c=new CompletionChime();const other='b'.repeat(64);
 m.register(other);m.status(other,{state:'idle'});c.take(m.snapshot(),true);
 m.status(other,{state:'working'});m.status(other,{state:'completed'});
 assert.equal(c.take(m.snapshot(),true),true);m.page(1);assert.equal(c.take(m.snapshot(),true),false);
});
test('wire capability is backward compatible and rejects undefined bits',()=>{
 assert.equal(deviceStatus(status(1,0,4)).capabilities,4);
 assert.equal(deviceStatus(status(1,0,255)).capabilities,255);
 assert.equal(deviceStatus(status(1,0,256)),null);
 assert.throws(()=>encodeChime(0,1));
});
