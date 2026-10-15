import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {createServerCommand} from '../host/server-command.mjs';
import {privateAddresses,OwnedServer,validServerLink} from '../host/server-process.mjs';
import {secretField} from '../host/server-secret.mjs';
import {serveOwner} from '../host/server-worker.mjs';
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const options={directory:'/fixture/run',python:'/fixture/python',port:'/dev/cu.usbmodemTEST',lan:{ssid:'synthetic-network',password:'synthetic-password',address:'192.168.1.2',port:0}};
function fixture(overrides={}){
 const hooks=new Map(),notices=[],starts=[],prompts=[];
 const server={running:false,link:null,closes:0,async inspectLink(){return this.link;},async start(value){starts.push(value);this.running=true;},async close(){this.closes++;this.running=false;}};
 const ctx={mode:'tui',ui:{notify:text=>notices.push(text),select:async()=> 'Start wireless',confirm:async()=>true}};
 const command=createServerCommand({on:(event,handler)=>hooks.set(event,handler)},{server,directory:'/fixture/run',locked:async()=>false,discover:async()=>({python:options.python,ports:[options.port],addresses:[options.lan.address]}),secret:async(_ctx,title)=>{prompts.push(title);return title.includes('network')?options.lan.ssid:options.lan.password;},inspect:async()=>null,isChild:false,...overrides});
 return {server,ctx,starts,notices,prompts,hooks,run:args=>command(args,ctx),shutdown:()=>hooks.get('session_shutdown')()};
}
test('server registration is dormant; menu starts with no voice or diagnostics and no secret notices',async()=>{
 const f=fixture();assert.equal(f.starts.length,0);assert.equal(f.prompts.length,0);
 await f.run('');assert.deepEqual(f.starts,[options]);assert.equal(f.prompts.length,2);
 assert.match(f.notices.at(-1),/not yet confirmed wirelessly/);
 assert.ok(f.notices.every(text=>!text.includes(options.lan.password)&&!text.includes(options.lan.ssid)));
 await f.shutdown();assert.equal(f.server.closes,1);await f.run('start');assert.equal(f.starts.length,1);
});
test('external lock blocks start before discovery or credential prompts and stop never takes ownership',async()=>{
 const f=fixture({locked:async()=>true,discover:()=>{throw Error('Must not discover');}});
 await f.run('start');await f.run('stop');assert.equal(f.starts.length,0);assert.equal(f.prompts.length,0);assert.equal(f.server.closes,0);
 assert.match(f.notices[0],/No duplicate/);assert.match(f.notices[1],/does not own/);
});
test('lock acquired while prompting blocks launch',async()=>{
 let n=0;const f=fixture({locked:async()=>++n>1});await f.run('start');assert.equal(f.starts.length,0);assert.match(f.notices.at(-1),/appeared/);
});
test('duplicate start is status only; stopping is explicit and only closes owned server',async()=>{
 const f=fixture();await f.run('start');await f.run('start');assert.equal(f.starts.length,1);assert.equal(f.prompts.length,2);
 f.ctx.ui.confirm=async()=>false;await f.run('stop');assert.equal(f.server.closes,0);
 f.ctx.ui.confirm=async()=>true;await f.run('stop');assert.equal(f.server.closes,1);
});
test('non-TUI, invalid arguments, child contexts and cancellations cannot launch',async()=>{
 for(const mode of ['rpc','json','print',undefined]){const f=fixture();f.ctx.mode=mode;await f.run('start');assert.equal(f.starts.length,0);assert.equal(f.prompts.length,0);}
 const child=fixture({isChild:true});assert.equal(child.hooks.size,0);await child.run('start');assert.equal(child.starts.length,0);
 const f=fixture();await f.run('start password');f.ctx.ui.confirm=async()=>false;await f.run('start');assert.equal(f.prompts.length,0);
 const cancelled=fixture({secret:async()=>undefined});await cancelled.run('start');assert.equal(cancelled.starts.length,0);
});
test('shutdown cancels hidden input and prevents late startup',async()=>{
 let entered;const ready=new Promise(resolve=>{entered=resolve;});
 const f=fixture({secret:async(_ctx,_title,{signal})=>{entered();return new Promise(resolve=>signal.addEventListener('abort',()=>resolve(undefined),{once:true}));}});
 const pending=f.run('start');await ready;await f.shutdown();await pending;assert.equal(f.starts.length,0);
});
test('concurrent commands cannot open two credential prompts; late discovery after shutdown is ignored',async()=>{
 let finish;const f=fixture({discover:()=>new Promise(resolve=>{finish=resolve;})});
 const pending=f.run('start');await tick();await f.run('start');assert.match(f.notices.at(-1),/already in progress/);
 await f.shutdown();finish({python:options.python,ports:[options.port],addresses:[options.lan.address]});await pending;assert.equal(f.prompts.length,0);assert.equal(f.starts.length,0);
});
test('errors and invalid credentials are redacted, never reflected',async()=>{
 const f=fixture({secret:async()=>{throw Error('synthetic-private-secret');}});await f.run('start');assert.ok(!f.notices.join().includes('synthetic-private-secret'));
 const g=fixture({secret:async()=> 'x'.repeat(100)});await g.run('start');assert.equal(g.starts.length,0);assert.ok(!g.notices.join().includes('x'.repeat(100)));
});
test('owned provisioning status distinguishes preparation, unplug readiness, LAN live and faults',async()=>{
 const observation={v:1,op:'inspection',phase:'confirmed',transport:'usb',sessions:0,session:'none',serialRestarts:0};
 const f=fixture({inspect:async()=>observation});f.server.running=true;f.server.link={paired:false,deviceHasIp:false,connected:false,live:false};
 await f.run('status');assert.match(f.notices.at(-1),/Keep USB/);assert.match(f.notices.at(-1),/USB device link confirmed/);
 Object.assign(f.server.link,{paired:true,deviceHasIp:true,connected:true});await f.run('status');assert.match(f.notices.at(-1),/can unplug USB/);
 f.server.link.live=true;observation.transport='lan';await f.run('status');assert.match(f.notices.at(-1),/connected wirelessly/);
 observation.phase='waiting-device';await f.run('status');assert.ok(!/Watch connected wirelessly|can unplug/.test(f.notices.at(-1)));
 f.server.link.deviceFault=true;await f.run('status');assert.match(f.notices.at(-1),/Provisioning fault/);
});
test('private address selection is local, canonical, deduplicated and excludes public/link-local/IPv6',()=>{
 const entries=['192.168.1.2','10.0.0.1','172.16.0.2','172.32.0.1','8.8.8.8','169.254.1.2','192.168.001.2','10.0.0.256','10.0.0.1'].map(address=>({address,family:'IPv4',internal:false}));
 assert.deepEqual(privateAddresses({en0:entries,lo:[{address:'10.0.0.2',family:'IPv4',internal:true}],v6:[{address:'::1',family:'IPv6',internal:false}]}),['10.0.0.1','172.16.0.2','192.168.1.2']);
});
const keys={matches:(data,key)=>data===({'tui.select.cancel':'ESC','tui.input.submit':'ENTER','tui.editor.deleteCharBackward':'BACK','tui.editor.deleteToLineStart':'CLEAR'}[key])};
test('secret field renders only masks; deletion never uses editor history and input is bounded',()=>{
 let result;const field=secretField({title:'Hidden',maxBytes:8,valid:value=>value.length>=3,keybindings:keys,done:value=>{result=value;}});
 field.handleInput('secret');for(const width of [0,1,4,80]){const lines=field.render(width);assert.ok(lines.every(line=>line.length<=width));assert.ok(!lines.join().includes('secret'));}
 field.handleInput('BACK');field.handleInput('ENTER');assert.equal(result,'secre');assert.ok(!field.render(80).join().includes('*****'));
 const other=secretField({title:'Hidden',maxBytes:8,valid:()=>true,keybindings:keys,done:value=>{result=value;}});
 other.handleInput('123456789');assert.match(other.render(80).join(),/rejected/);other.handleInput('CLEAR');other.handleInput('abc');other.handleInput('ENTER');assert.equal(result,'abc');
});
test('secret paste handles split markers, rejects control characters and oversized input, aborts cleanly',()=>{
 let result='unset';const abort=new AbortController();
 const field=secretField({title:'Hidden',maxBytes:8,valid:()=>true,keybindings:keys,done:value=>{result=value;},signal:abort.signal});
 field.handleInput('\x1b[200~abc');field.handleInput('def\x1b[201~');assert.match(field.render(80).join(),/\*{6}/);
 field.handleInput('CLEAR');field.handleInput('\x1b[200~123456789\x1b[201~');assert.match(field.render(80).join(),/too long/);
 field.handleInput('CLEAR');field.handleInput('\x1b[200~abc\ndef\x1b[201~');assert.match(field.render(80).join(),/rejected/);
 abort.abort();assert.equal(result,undefined);assert.ok(!field.render(80).join().includes('abcdef'));
});
function channel(){const c=new EventEmitter();c.connected=true;c.sent=[];c.send=(message,cb)=>{c.sent.push(message);cb?.();};return c;}
test('worker owner disconnect closes only its host, with whitelisted status and no credentials emitted',async()=>{
 const c=channel();let finish,closed=0,exited=0;const done=new Promise(resolve=>{finish=resolve;});
 const worker=serveOwner(c,async()=>({status:()=>({lan:{paired:true,password:'never emit'}}),done,close:async()=>{closed++;finish();}}),()=>{exited++;});
 c.emit('message',{type:'start',options:structuredClone(options)});await tick();assert.equal(c.sent[0].type,'ready');assert.ok(validServerLink(c.sent[1].value));assert.ok(!JSON.stringify(c.sent).includes('never emit'));
 c.emit('message',{type:'inspect',request:1});assert.equal(c.sent.at(-1).request,1);assert.ok(validServerLink(c.sent.at(-1).value));
 c.connected=false;c.emit('disconnect');await worker.close();assert.equal(closed,1);assert.equal(exited,1);
});
test('disconnect during startup closes late host and never publishes readiness',async()=>{
 const c=channel();let finish,closed=0;const worker=serveOwner(c,()=>new Promise(resolve=>{finish=resolve;}),()=>{});
 c.emit('message',{type:'start',options:structuredClone(options)});c.connected=false;c.emit('disconnect');
 finish({close:async()=>{closed++;}});await worker.close();assert.equal(closed,1);assert.equal(c.sent.length,0);
});
test('worker rejects extra host authority options before launching',async()=>{
 const c=channel();let starts=0;const worker=serveOwner(c,async()=>{starts++;},()=>{});
 c.emit('message',{type:'start',options:{...options,voice:{}}});await tick();await worker.close();assert.equal(starts,0);assert.equal(c.sent[0].type,'failed');
});
test('late errors from an exited child cannot stop a replacement owned child',async()=>{
 const children=[];const server=new OwnedServer({launch:()=>{
  const child=channel();child.exitCode=null;child.signalCode=null;
  child.send=(_message,callback)=>{callback?.();queueMicrotask(()=>child.emit('message',{type:'ready'}));};
  child.disconnect=()=>{child.connected=false;queueMicrotask(()=>{child.exitCode=0;child.emit('exit',0);});};child.kill=()=>{};
  children.push(child);return child;
 }});
 await server.start(options);await server.close();await server.start(options);
 children[0].emit('error',Error('late fixture error'));assert.equal(children[1].connected,true);assert.equal(server.running,true);
 await server.close();
});

test('owned process rejects spawn/send failure and clears ownership without exposing errors',async()=>{
 const child=channel();child.exitCode=null;child.signalCode=null;
 child.send=()=>{throw Error('synthetic-private-secret');};child.disconnect=()=>{child.connected=false;queueMicrotask(()=>{child.exitCode=0;child.emit('close');});};child.kill=()=>{};
 const server=new OwnedServer({launch:()=>child});await assert.rejects(server.start(options),/^Error: Server startup unavailable$/);assert.equal(server.running,false);
});
