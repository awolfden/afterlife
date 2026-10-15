import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,readFileSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,delimiter} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const script=fileURLToPath(new URL('../tools/watcher-host.sh',import.meta.url));
function fixture(t){
 const directory=mkdtempSync(join(tmpdir(),'watch-host-launcher-')),log=join(directory,'arguments'),python=join(directory,'python');
 t.after(()=>rmSync(directory,{recursive:true,force:true}));
 writeFileSync(join(directory,'node'),'#!/bin/sh\nprintf "%s\\n" "$@" > "$FAKE_ARGS"\n',{mode:0o700});
 writeFileSync(python,'#!/bin/sh\nexit 99\n',{mode:0o700});
 return {python,run(args,extra={}){rmSync(log,{force:true});const result=spawnSync('/bin/sh',[script,...args],{encoding:'utf8',env:{...process.env,PI_SUBAGENT_CHILD:'',HOME:directory,PATH:directory+delimiter+process.env.PATH,WATCHER_PYTHON:python,FAKE_ARGS:log,...extra}});return {...result,args:existsSync(log)?readFileSync(log,'utf8').trim().split('\n'):null};}};
}
test('host launcher IPC and explicit Python bypass the private SDK fallback',t=>{
 const f=fixture(t);let result=f.run(['--ipc-only'],{WATCHER_PYTHON:'/synthetic/missing'});assert.equal(result.status,0);assert.deepEqual(result.args.slice(1),['--ipc-only']);
 result=f.run(['--python','/synthetic/explicit','--port','/dev/cu.usbmodemTEST']);assert.equal(result.status,0);assert.deepEqual(result.args.slice(1),['--python','/synthetic/explicit','--port','/dev/cu.usbmodemTEST']);
});
test('host launcher forwards an explicitly configured Python and selectors unchanged',t=>{
 const f=fixture(t),args=['--device-serial',Array(6).fill('ab').join(':'),'--state-dir','/synthetic/state','--completion-chime'];const result=f.run(args);assert.equal(result.status,0);assert.deepEqual(result.args.slice(1),['--python',f.python,...args]);
});
test('host launcher missing Python and child context fail before invoking Node',t=>{
 const f=fixture(t);for(const env of [{WATCHER_PYTHON:''},{WATCHER_PYTHON:'/synthetic/missing'},{PI_SUBAGENT_CHILD:'1'}]){const result=f.run(['--port','/dev/cu.usbmodemTEST'],env);assert.equal(result.status,1);assert.equal(result.args,null);}
});
test('host launcher help is local and describes supported identity options',t=>{
 const f=fixture(t),result=f.run(['--help'],{WATCHER_PYTHON:'/synthetic/missing'});assert.equal(result.status,0);assert.equal(result.args,null);for(const flag of ['--device-serial','--state-dir','--lan-config'])assert.ok(result.stdout.includes(flag));
});
