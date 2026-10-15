import test from 'node:test';
import assert from 'node:assert/strict';
import { AttentionModel } from '../host/attention.mjs';
import { screenView, motionPose, expressions } from '../preview/screen.mjs';
const id=n=>n.toString(16).padStart(64,'0');
function fixture(){const m=new AttentionModel();for(let n=1;n<=3;n++){m.register(id(n));m.status(id(n),{state:'idle'});}m.connected(true);return m;}
test('preview labels are generated, never source prose or raw identities',()=>{
 const m=fixture();const frame=m.snapshot();const row={...frame.sessions[0],label:'FORBIDDEN PRIVATE PROSE'};
 const view=screenView({...frame,sessions:[row]});assert.equal(view.session,'Session 1');assert.equal(JSON.stringify(view).includes('FORBIDDEN'),false);assert.equal(JSON.stringify(view).includes(id(1)),false);
});
test('ACK remains visible without resolving pending input',()=>{
 const m=fixture();m.attention(id(1),{itemId:id(20),kind:'input'});m.acknowledge(id(1),id(20));
 const view=screenView(m.snapshot());assert.equal(view.state,'needs-input');assert.equal(view.detail,'Acknowledged · still waiting');assert.equal(m.snapshot().sessions[0].pendingCount,1);
});
test('uncertainty and disconnect cannot become completion',()=>{
 const m=fixture();m.status(id(1),{state:'completed'});m.uncertain(id(1),id(20));
 assert.equal(screenView(m.snapshot()).state,'unavailable');m.connected(false);assert.equal(screenView(m.snapshot()).state,'offline');assert.equal(screenView(m.snapshot()).detail,'Request retained');
});
test('paging preserves parent grouping and reveals attention elsewhere',()=>{
 const m=fixture();m.attention(id(1),{itemId:id(20),kind:'child'});m.page(1);
 const v=screenView(m.snapshot());assert.equal(v.session,'Session 2');assert.equal(v.otherWaiting,1);assert.equal(v.state,'idle');assert.equal(v.pages[0].attention,true);
});
test('empty state and muted footer are explicit',()=>{
 assert.equal(screenView(new AttentionModel().snapshot()).state,'empty');const m=fixture();m.mute(true);assert.match(screenView(m.snapshot()).footer,/Muted/);
});
test('motion is bounded and reduced motion is stationary across every expression',()=>{
 for(const state of Object.keys(expressions)){
  for(let t=0;t<60000;t+=137){const p=motionPose(state,t);assert.ok(Math.abs(p.x)<=21 && Math.abs(p.y)<=7 && Math.abs(p.tilt)<=7);}
  assert.deepEqual(motionPose(state,5000,true),{x:0,y:0,tilt:0,gazeX:0,gazeY:0});
 }
 assert.deepEqual(motionPose('working',NaN),{x:0,y:0,tilt:0,gazeX:0,gazeY:0});
});
test('failure is visibly grumpy, with narrower eyes and teeth rather than the unknown face',()=>{
 assert.equal(expressions.failed.teeth,true);
 assert.ok(expressions.failed.eyeRy < expressions['needs-input'].eyeRy);
 assert.notEqual(expressions.failed.brows,expressions.unavailable.brows);
 assert.notDeepEqual(motionPose('failed',1500),motionPose('child-help',1500));
});
test('presentation never consumes haptic intents',()=>{
 const m=fixture();m.attention(id(1),{itemId:id(20),kind:'input'});for(let n=0;n<10;n++)screenView(m.snapshot());assert.equal(m.flush().length,1);
});
