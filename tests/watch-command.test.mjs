import test from 'node:test';
import assert from 'node:assert/strict';
import {registerWatcher} from '../host/pi-observer.mjs';

function fixture(options={}){
 const hooks=new Map(),commands=new Map(),clients=[],notices=[];
 let owner='root-one';
 const ctx={sessionManager:{getSessionId:()=>owner},isIdle:()=>false,ui:{notify:message=>notices.push(message)}};
 const pi={on(name,fn){if(!hooks.has(name))hooks.set(name,new Set());hooks.get(name).add(fn);return()=>hooks.get(name)?.delete(fn);},events:{on(){return()=>{};},emit(){}},registerCommand(name,command){commands.set(name,command);}};
 const observer=registerWatcher(pi,{autoStart:false,isChild:false,clientFactory:()=>{
  const client={connected:false,states:[],closed:0,reconnections:0,register:()=>true,start(){},status(_id,value){this.states.push(value);},supervisorSync(){},resolve(){},close(){this.closed++;},reconnect(){this.reconnections++;}};
  clients.push(client);return client;
 },...options});
 return {clients,notices,observer,commands,ctx,fire:(name,event={})=>{for(const handler of [...(hooks.get(name)??[])])handler(event,ctx);},watch:(args='')=>commands.get('watch').handler(args,ctx),switchOwner:()=>{owner='root-two';}};
}

test('watch reports inspected device link separately from IPC and ignores replaced-runtime results',async t=>{
 const f=fixture();t.after(()=>f.observer.dispose());await f.watch('on');const client=f.clients[0];client.connected=true;
 client.inspect=async()=>({v:1,op:'inspection',phase:'confirmed',transport:'usb',sessions:1,session:'selected',serialRestarts:0});
 await f.watch('status');assert.match(f.notices.at(-1),/USB device link confirmed; this session selected/);
 let finish;client.inspect=()=>new Promise(r=>{finish=r;});const pending=f.watch('status');await f.watch('off');const before=f.notices.length;finish(null);await pending;assert.equal(f.notices.length,before);
});
test('former voice commands cannot activate watching, UI, or control',async t=>{
 let activated=0;const f=fixture({voiceFactory:()=>{activated++;throw Error('must not run');}});t.after(()=>f.observer.dispose());
 for(const command of ['voice','voice on','voice off','voice status','voice on extra']){await f.watch(command);assert.match(f.notices.at(-1),/Usage/);}
 assert.equal(f.clients.length,0);assert.equal(activated,0);
 await f.watch('on');const count=f.clients.length;await f.watch('voice on');assert.equal(f.clients.length,count);assert.equal(activated,0);
 assert.equal(f.commands.get('watch').getArgumentCompletions('voice'),null);
 await f.commands.get('watcher-reconnect').handler('',f.ctx);assert.equal(f.clients[0].reconnections,1);
});
test('global observer stays dormant until explicit opt-in; repeated on is idempotent',async t=>{
 const f=fixture();t.after(()=>f.observer.dispose());
 f.fire('session_start');f.fire('agent_start');await f.watch('status');await f.watch('invalid');assert.equal(f.clients.length,0);
 await f.watch();await f.watch('on');assert.equal(f.clients.length,1);assert.equal(f.clients[0].states[0].state,'thinking');
 assert.match(f.notices.at(-1),/not connected; retrying.*unknown/);
 f.clients[0].connected=true;await f.watch('status');assert.match(f.notices.at(-1),/device delivery not confirmed/);
 await f.watch('off');await f.watch('off');assert.equal(f.clients[0].closed,1);
 const count=f.clients[0].states.length;f.fire('agent_start');assert.equal(f.clients[0].states.length,count);
 await f.watch('status');assert.match(f.notices.at(-1),/Watching is off/);await f.watch();assert.equal(f.clients.length,2);
});
test('switch and reload do not carry opt-in into another runtime',async t=>{
 const f=fixture();t.after(()=>f.observer.dispose());await f.watch();f.fire('session_shutdown');f.switchOwner();f.fire('session_start');
 assert.equal(f.clients[0].closed,1);assert.equal(f.clients.length,1);
 await f.watch();f.fire('session_start');await f.watch('status');assert.equal(f.clients[1].closed,1);assert.match(f.notices.at(-1),/Watching is off/);
});
test('launcher auto-start remains supported',t=>{
 const f=fixture({autoStart:true});t.after(()=>f.observer.dispose());f.fire('session_start');assert.equal(f.clients.length,1);
});
test('children register no commands or observer',()=>{
 const f=fixture({isChild:true});assert.equal(f.commands.size,0);f.fire('session_start');assert.equal(f.clients.length,0);f.observer.dispose();
});
test('aborted assistant runs settle idle, not completed',async t=>{
 const f=fixture();t.after(()=>f.observer.dispose());await f.watch();
 f.fire('agent_start');f.fire('message_end',{message:{role:'assistant',stopReason:'aborted'}});f.fire('agent_settled');assert.equal(f.clients[0].states.at(-1).state,'idle');
 f.fire('agent_start');f.fire('message_end',{message:{role:'assistant',stopReason:'stop'}});f.fire('agent_settled');assert.equal(f.clients[0].states.at(-1).state,'completed');
});
test('server controls route without implicit watching and expose argument completions',async t=>{
 const calls=[],f=fixture({serverCommand:async(args,ctx)=>calls.push({args,ctx})});t.after(()=>f.observer.dispose());
 await f.watch('server');await f.watch('server status');assert.deepEqual(calls.map(call=>call.args),['','status']);assert.equal(calls[0].ctx,f.ctx);assert.equal(f.clients.length,0);
 assert.deepEqual(f.commands.get('watch').getArgumentCompletions('server s').map(item=>item.value),['server start','server status','server stop']);
 const g=fixture();t.after(()=>g.observer.dispose());await g.watch('server');assert.match(g.notices.at(-1),/unavailable/);assert.equal(g.clients.length,0);
});
test('start failure can be retried without claiming connection',async t=>{
 const f=fixture({clientFactory:()=>{throw new Error('synthetic failure');}});t.after(()=>f.observer.dispose());
 await f.watch();assert.match(f.notices.at(-1),/could not start/);await f.watch('status');assert.match(f.notices.at(-1),/Watching is off/);
});
