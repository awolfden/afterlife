import test from 'node:test';
import assert from 'node:assert/strict';
import {AttentionModel} from '../host/attention.mjs';
import {SessionHub} from '../host/session-hub.mjs';
import {readFileSync} from 'node:fs';
import {displayTitle,validIdentity,herdrIdentity,personality,ghostPage,ghostPalette,allocatePersonality,spaceIdentityEnabled} from '../host/session-identity.mjs';
const id=n=>n.toString(16).padStart(64,'0');
const identity={spaceId:id(20),spaceTitle:'Stopwatch'};
test('workspace metadata requires exact opt-in in both public entry points',()=>{
 assert.equal(spaceIdentityEnabled({HERDR_ENV:'1',HERDR_PANE_ID:'synthetic',HERDR_SOCKET_PATH:'/synthetic/socket'}),false);
 for(const value of [undefined,'0','true',true,1,'yes'])assert.equal(spaceIdentityEnabled({WATCHER_SPACE_IDENTITY:value}),false);
 assert.equal(spaceIdentityEnabled({WATCHER_SPACE_IDENTITY:'1'}),true);
 for(const path of ['../host/pi-observer.mjs','../extensions/watch-command.ts'])assert.match(readFileSync(new URL(path,import.meta.url),'utf8'),/spaceIdentity:spaceIdentityEnabled\(\)/);
});
test('title metadata is bounded UTF8 and removes control/bidi escapes',()=>{
 assert.equal(displayTitle('  hi\x1b\nthere\u202e  '),'hi there');
 const title=displayTitle('🐺'.repeat(20));assert.equal(Buffer.byteLength(title),48);
 assert.ok(validIdentity({...identity,spaceTitle:title}));
 for(const value of [{...identity,spaceTitle:'x\n'}, {...identity,prompt:'private'}, {...identity,spaceId:'w1'}, {...identity,spaceTitle:''}])assert.equal(validIdentity(value),false);
});
test('Herdr resolves caller pane rather than UI focus; IDs scoped to server, not inherited workspace',async()=>{
 const calls=[],env={HERDR_ENV:'1',HERDR_PANE_ID:'w1:p1',HERDR_SOCKET_PATH:'/private/server-a.sock'};
 const run=async(command,args,options)=>{calls.push([command,args,options]);return {stdout:JSON.stringify({result:args[0]==='pane'?{pane:{workspace_id:'w2'}}:{workspace:{workspace_id:'w2',label:'New title'}}})};};
 const value=await herdrIdentity({env,run});assert.equal(value.spaceTitle,'New title');assert.ok(validIdentity(value));
 assert.deepEqual(calls.map(x=>x[1]),[['pane','current','--current'],['workspace','get','w2']]);
 assert.equal((await herdrIdentity({env:{...env,HERDR_PANE_ID:'w2:p8'},run})).spaceId,value.spaceId);
 assert.notEqual((await herdrIdentity({env:{...env,HERDR_SOCKET_PATH:'/private/server-b.sock'},run})).spaceId,value.spaceId);
 assert.equal(JSON.stringify(value).includes('/private'),false);
});
test('Herdr lookup fails closed outside managed panes or on errors',async()=>{
 const run=()=>{throw new Error('unavailable');};
 assert.equal(await herdrIdentity({env:{},run}),null);
 assert.equal(await herdrIdentity({env:{HERDR_ENV:'1',HERDR_PANE_ID:'w1:p1',HERDR_SOCKET_PATH:'/a'},run}),null);
});
test('strict hub identity ingress, grouping counts and stable personality',()=>{
 const m=new AttentionModel(),hub=new SessionHub(m);
 const a=hub.attach();assert.ok(a.receive({v:1,seq:1,op:'hello',id:id(1)},0));
 assert.ok(a.receive({v:1,seq:2,op:'identity',identity},0));
 assert.equal(a.receive({v:1,seq:3,op:'identity',identity,prompt:'private'},0),false);
 m.register(id(2));m.identity(id(2),identity);
 const before=m.snapshot();assert.equal(before.sessions[0].spaceCount,2);assert.equal(before.sessions[0].label,'Stopwatch');
 m.identity(id(1),{...identity,spaceTitle:'Renamed'});
 assert.deepEqual(m.snapshot().sessions[0].personality,before.sessions[0].personality);
 m.remove(id(2));assert.equal(m.snapshot().sessions[0].spaceCount,1);
 assert.notDeepEqual(personality(id(1)),personality(id(2)));
});
test('first registered ghost is purple; palette and archetypes survive focus/group changes and reuse only released slots',()=>{
 const m=new AttentionModel(),traits=n=>m.snapshot().sessions.find(s=>s.id===id(n)).personality;
 for(let n=1;n<=16;n++)assert.equal(m.register(id(n)),true);
 assert.equal(traits(1).hue,270);assert.equal(traits(1).rhythm,0);
 assert.equal(new Set(m.snapshot().sessions.map(s=>s.personality.hue)).size,16);
 assert.deepEqual([1,2,3,4].map(n=>traits(n).hue),[270,165,45,205]);
 assert.deepEqual([1,2,3,4].map(n=>traits(n).rhythm),[0,1,2,3]);
 const before=new Map(m.snapshot().sessions.map(s=>[s.id,s.personality]));
 m.page(1);m.identity(id(2),identity);m.identity(id(4),identity);assert.equal(m.register(id(2)),true);
 for(const s of m.snapshot().sessions)assert.deepEqual(s.personality,before.get(s.id));
 assert.equal(m.register(id(17)),false);assert.throws(()=>allocatePersonality(id(17),new Set(ghostPalette)));
 m.remove(id(1));for(const s of m.snapshot().sessions)assert.deepEqual(s.personality,before.get(s.id));
 assert.equal(m.register(id(17)),true);assert.equal(traits(17).hue,270);
 m.remove(id(3));assert.equal(m.register(id(19)),true);assert.equal(traits(19).hue,45);
 for(const s of m.snapshot().sessions)if(before.has(s.id))assert.deepEqual(s.personality,before.get(s.id));
 m.remove(id(19));for(let n=2;n<=17;n++)m.remove(id(n));assert.equal(m.register(id(18)),true);assert.equal(traits(18).hue,270);
});
test('oldest attention focuses without new-arrival stealing; ACK is not resolution',()=>{
 const m=new AttentionModel({autoFocus:true});for(let i=1;i<=3;i++){m.register(id(i));m.status(id(i),{state:'idle'});}m.connected(true);
 m.time(1);m.attention(id(2),{itemId:id(10),kind:'child'});
 assert.equal(m.snapshot().selectedSessionId,id(2));
 m.time(2);m.attention(id(3),{itemId:id(11),kind:'input'});
 assert.equal(m.snapshot().selectedSessionId,id(2));
 assert.ok(m.acknowledge(id(2),id(10)));assert.equal(m.snapshot().sessions[1].pendingCount,1);
 m.resolve(id(2),id(10));assert.equal(m.snapshot().selectedSessionId,id(3));
 m.page(1);m.attention(id(2),{itemId:id(12),kind:'input'});assert.equal(m.snapshot().selectedSessionId,id(2));
});
test('small pages report hidden pending ghosts, not only unacknowledged alerts',()=>{
 const m=new AttentionModel();for(let i=1;i<=9;i++)m.register(id(i));
 m.attention(id(9),{itemId:id(10),kind:'input'});
 let page=ghostPage(m.snapshot());assert.equal(page.visible.length,4);assert.equal(page.offscreen,5);assert.equal(page.offscreenWaiting,1);
 for(let i=0;i<8;i++)m.page(1);page=ghostPage(m.snapshot());assert.equal(page.visible.length,1);assert.equal(page.offscreen,8);
 assert.throws(()=>ghostPage(m.snapshot(),20));
});
