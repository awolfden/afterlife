import test from 'node:test';
import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import {AttentionModel,advanceFocusOrdinal} from '../host/attention.mjs';
const id=n=>n.toString(16).padStart(64,'0');
function fixture(n=4){const m=new AttentionModel({autoFocus:true});for(let i=1;i<=n;i++){m.register(id(i));m.status(id(i),{state:'idle'});}m.connected(true);return m;}
const event=(m,s,i=s+100)=>m.attention(id(s),{itemId:id(i),kind:'input'});
const selected=m=>m.selection().id;
test('manual C excludes old A/B, new D focuses after hold, resolution never revives backlog',()=>{
 const m=fixture();event(m,1);event(m,2);m.page(1);m.page(1);assert.equal(selected(m),id(3));m.interaction(true);event(m,4);assert.equal(selected(m),id(3));m.interaction(false);assert.equal(selected(m),id(4));m.resolve(id(4),id(104));assert.equal(selected(m),id(4));
});
test('receipt order, not lexical IDs, determines same-tick waiting focus',()=>{
 const m=fixture();m.interaction(true);event(m,4,999);event(m,2,1);m.interaction(false);assert.equal(selected(m),id(4));m.resolve(id(4),id(999));assert.equal(selected(m),id(2));
});
test('reconnect keeps selection without consuming never-delivered attention',()=>{
 const m=fixture();m.connected(false);event(m,2);assert.equal(selected(m),id(1));m.connected(true);assert.equal(selected(m),id(1));assert.equal(m.flush().length,1);assert.deepEqual(m.flush(),[]);m.interaction(true);event(m,3);m.connected(true);m.interaction(false);assert.equal(selected(m),id(3));
});
test('reconfirmation is not a new event; first unknown promotion is',()=>{
 const m=fixture();event(m,2);m.page(1);m.uncertain(id(2),id(102),'input');event(m,2);assert.equal(selected(m),id(3));m.uncertain(id(4),id(104),'input');event(m,4);assert.equal(selected(m),id(4));
});
test('unavailable targets are excluded; selected pending wins; resolved held targets disappear',()=>{
 const m=fixture();event(m,1);m.interaction(true);event(m,2);m.resolve(id(2),id(102));event(m,4);m.status(id(4),{state:'idle',available:false});m.interaction(false);assert.equal(selected(m),id(1));m.resolve(id(1),id(101));assert.equal(selected(m),id(1));m.status(id(4),{state:'idle'});assert.equal(selected(m),id(4));
});
test('single-session manual paging establishes cutoff without changing selection epoch',()=>{
 const m=fixture(1);event(m,1);const epoch=m.selection().epoch;m.page(1);assert.equal(m.selection().epoch,epoch);m.register(id(2));m.status(id(2),{state:'idle'});m.page(1);event(m,1);assert.equal(selected(m),id(2));
});
test('focus counter saturates without wrap or reusable order',()=>{
 assert.equal(advanceFocusOrdinal(Number.MAX_SAFE_INTEGER-1),Number.MAX_SAFE_INTEGER);
 assert.equal(advanceFocusOrdinal(Number.MAX_SAFE_INTEGER),null);assert.equal(advanceFocusOrdinal(-1),null);
});
test('selected removal and availability cannot revive pre-watermark requests',()=>{
 const m=fixture();event(m,2);m.page(1);m.page(1);assert.equal(selected(m),id(4));m.remove(id(4));assert.equal(selected(m),id(1));m.status(id(1),{state:'idle',available:false});assert.equal(selected(m),id(1));event(m,3);assert.equal(selected(m),id(3));
});
test('reconfirmation after reconnect keeps original ordering',()=>{
 const m=fixture();m.connected(false);event(m,2);m.connected(true);m.uncertain(id(2),id(102),'input');event(m,2);assert.equal(selected(m),id(1));event(m,3);assert.equal(selected(m),id(3));
});
test('model exhaustion latches without disabling manual controls or source updates',async()=>{
 // Compile the exact model with only its private initial counter seeded near
 // exhaustion. No public production reset/configuration or billions of events.
 let source=readFileSync(new URL('../host/attention.mjs',import.meta.url),'utf8');
 assert.equal(source.split('#focusOrdinal=0').length,2);
 source=source.replace('#focusOrdinal=0','#focusOrdinal=Number.MAX_SAFE_INTEGER-1').replace("'./session-identity.mjs'",JSON.stringify(new URL('../host/session-identity.mjs',import.meta.url).href));
 const {AttentionModel:NearLimit}=await import('data:text/javascript;base64,'+Buffer.from(source).toString('base64'));
 const m=new NearLimit({autoFocus:true});for(let i=1;i<=3;i++){m.register(id(i));m.status(id(i),{state:'idle'});}m.connected(true);
 event(m,2);assert.equal(selected(m),id(2));m.page(1);event(m,1);assert.equal(selected(m),id(3));m.uncertain(id(1),id(101),'input');event(m,1);assert.equal(selected(m),id(3));m.page(1);assert.equal(selected(m),id(1));assert.equal(m.snapshot().sessions[0].pendingCount,1);
});
test('completion and mute never change focus; child focus need not wait for notification grace',()=>{
 const m=fixture();m.mute(true);m.status(id(2),{state:'working'});m.status(id(2),{state:'completed'});assert.equal(selected(m),id(1));m.attention(id(3),{itemId:id(500),kind:'child'});assert.equal(selected(m),id(3));assert.deepEqual(m.flush(),[]);
});
