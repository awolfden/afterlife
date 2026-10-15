import test from 'node:test';
import assert from 'node:assert/strict';
import {AttentionModel} from '../host/attention.mjs';
import {SessionCycle} from '../host/session-cycle.mjs';
const id=n=>n.toString(16).padStart(64,'0');
function fixture(count=3){const model=new AttentionModel({autoFocus:true});for(let n=1;n<=count;n++){model.register(id(n));model.status(id(n),{state:'idle'});model.supervisorSync(id(n),true);}model.connected(true);return {model,cycle:new SessionCycle(model)};}
test('cycling defaults off; enabled clock advances at 10 seconds without catch-up',()=>{
 const {model,cycle}=fixture();assert.equal(cycle.tick(0,false),false);assert.equal(cycle.tick(100000,false),false);assert.equal(model.selection().id,id(1));
 assert.equal(cycle.tick(100001,true),false);assert.equal(cycle.tick(110000,true),false);assert.equal(cycle.tick(110001,true),true);assert.equal(model.selection().id,id(2));
 assert.equal(cycle.tick(999999,true),true);assert.equal(model.selection().id,id(3));assert.equal(cycle.tick(1000000,true),false);
 model.attention(id(1),{itemId:id(50),kind:'input'});assert.equal(model.selection().id,id(1)); // Cycling did not latch manual focus.
});
for(const count of [0,1])test(`cycling ignores ${count} sessions`,()=>{const {cycle}=fixture(count);cycle.tick(0,true);assert.equal(cycle.tick(10000,true),false);});
test('manual navigation, disable and roster change restart the full interval',()=>{
 const {model,cycle}=fixture();cycle.tick(0,true);model.page(1);assert.equal(cycle.tick(9999,true),false);assert.equal(cycle.tick(10000,true),false);
 assert.equal(cycle.tick(19999,true),true);cycle.tick(20000,false);assert.equal(cycle.tick(40000,true),false);
 model.register(id(4));model.status(id(4),{state:'idle'});assert.equal(cycle.tick(49999,true),false);assert.equal(cycle.tick(59999,true),true);
});
test('acknowledged and uncertain requests suppress cycling; no recovery burst',()=>{
 const {model,cycle}=fixture();cycle.tick(0,true);model.attention(id(1),{itemId:id(50),kind:'input'});model.acknowledge(id(1),id(50));assert.equal(cycle.tick(10000,true),false);
 model.uncertain(id(1),id(50),'input');assert.equal(cycle.tick(20000,true),false);model.resolve(id(1),id(50));assert.equal(cycle.tick(30000,true),false);assert.equal(cycle.tick(39999,true),false);assert.equal(cycle.tick(40000,true),true);
});
test('interaction hold defers existing automatic focus but never resolves attention',()=>{
 const {model,cycle}=fixture();model.interaction(true);model.attention(id(2),{itemId:id(50),kind:'input'});assert.equal(model.selection().id,id(1));
 assert.equal(model.cycle(),false);assert.equal(cycle.tick(10000,false),false);model.interaction(false);assert.equal(model.selection().id,id(2));assert.equal(model.snapshot().sessions[1].attentionCount,1);
});
