import test from 'node:test';
import assert from 'node:assert/strict';
import {fork} from 'node:child_process';
import {once} from 'node:events';
import {mkdtemp,readFile,rm} from 'node:fs/promises';
import {watch} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {OwnedServer,discoverServerSetup} from '../host/server-process.mjs';
test('server discovery requires an explicit absolute interpreter before any execution',async()=>{
 let calls=0;const executePython=async()=>{calls++;throw Error('unexpected execution');};
 for(const python of [null,'','python3','relative/python','/bad\npython','/bad\0python','/'+'a'.repeat(4096)])await assert.rejects(discoverServerSetup({python,platform:'darwin',executePython}),/WATCHER_PYTHON/);
 await assert.rejects(discoverServerSetup({python:'/fixture/python',platform:'linux',executePython}),/macOS/);
 assert.equal(calls,0);
});
test('server discovery uses only selected Python, bounded metadata listing and validated local addresses',async()=>{
 const python='/fixture/venv with spaces/bin/python';let calls=0;
 const result=await discoverServerSetup({python,platform:'darwin',executePython:async(command,args,options)=>{
  calls++;assert.equal(command,python);assert.deepEqual(args.slice(0,2),['-I','-c']);assert.match(args[2],/serial\.__version__ == "3\.5"/);assert.match(args[2],/list_ports\.comports\(\)/);assert.doesNotMatch(args[2],/Serial\(/);assert.deepEqual(options,{timeout:3000,maxBuffer:8192});
  return {stdout:JSON.stringify(['/dev/cu.usbmodemB','/dev/cu.usbmodemA','/dev/cu.usbmodemB'])};
 },interfaces:()=>({en0:[{address:'192.168.1.2',family:'IPv4',internal:false},{address:'8.8.8.8',family:'IPv4',internal:false}]})});
 assert.equal(calls,1);assert.deepEqual(result,{python,ports:['/dev/cu.usbmodemA','/dev/cu.usbmodemB'],addresses:['192.168.1.2']});
});
test('server discovery rejects bad metadata or interpreter failure without fallback',async()=>{
 for(const stdout of ['not json','{}','["/dev/tty.other"]',JSON.stringify(Array(33).fill('/dev/cu.usbmodemA'))])await assert.rejects(discoverServerSetup({python:'/fixture/python',platform:'darwin',executePython:async()=>({stdout}),interfaces:()=>{throw Error('must not inspect interfaces');}}));
 let calls=0;await assert.rejects(discoverServerSetup({python:'/fixture/python',platform:'darwin',executePython:async()=>{calls++;throw Error('fixture failure');}}),/fixture failure/);assert.equal(calls,1);
});
const launch=name=>fork(fileURLToPath(new URL(`./fixtures/${name}.mjs`,import.meta.url)),[],{execArgv:[],stdio:['ignore','ignore','ignore','ipc']});
const config=directory=>({directory,python:'/fixture/python',port:'/dev/cu.usbmodemTEST',lan:{ssid:'synthetic',password:'synthetic-pass',address:'192.168.1.2',port:0}});
test('real IPC child starts once and cleans up on explicit owner close', {timeout:8000},async t=>{
 const directory=await mkdtemp(join(tmpdir(),'watcher-server-'));
 const server=new OwnedServer({launch:()=>launch('server-worker')});
 t.after(async()=>{await server.close();await rm(directory,{recursive:true,force:true});});
 await server.start(config(directory));assert.equal(server.ready,true);
 assert.deepEqual(await server.inspectLink(),{paired:false,deviceHasIp:false,connected:false,live:false,deviceFault:false,provisionError:false});
 await assert.rejects(server.start(config(directory)),/already owned/);
 await Promise.all([server.close(),server.close()]);assert.equal(server.running,false);
 assert.equal(await readFile(join(directory,'closed'),'utf8'),'closed');
});
test('real IPC owner crash causes worker cleanup without PID adoption or hardware access', {timeout:8000},async t=>{
 const directory=await mkdtemp(join(tmpdir(),'watcher-owner-'));
 const owner=fork(fileURLToPath(new URL('./fixtures/server-owner.mjs',import.meta.url)),[directory],{execArgv:[],stdio:['ignore','ignore','ignore','ipc']});
 let watcher,timer;
 t.after(async()=>{watcher?.close();clearTimeout(timer);if(owner.exitCode===null&&owner.signalCode===null)owner.kill('SIGKILL');await rm(directory,{recursive:true,force:true});});
 const signal=AbortSignal.timeout(5000);
 const [message]=await once(owner,'message',{signal});assert.equal(message.type,'ready');
 const cleaned=new Promise((resolve,reject)=>{
  watcher=watch(directory,(_event,file)=>{if(file==='closed'){watcher.close();clearTimeout(timer);resolve();}});
  timer=setTimeout(()=>reject(Error('Worker did not clean up after owner crash')),5000);
 });
 const exited=once(owner,'exit');owner.kill('SIGKILL');await exited;await cleaned;
 assert.equal(await readFile(join(directory,'closed'),'utf8'),'closed');
});
test('production worker rejects invalid device options before accessing a device or state directory', {timeout:8000},async()=>{
 const server=new OwnedServer();
 try{await assert.rejects(server.start({...config('/does-not-exist-watcher-fixture'),port:'invalid'}),/startup unavailable/);assert.equal(server.running,false);}
 finally{await server.close();}
});
