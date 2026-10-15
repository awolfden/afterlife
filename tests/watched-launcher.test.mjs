import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync,realpathSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const launcher=fileURLToPath(new URL('../tools/pi-watched.sh',import.meta.url));
function fake(run){
 const dir=mkdtempSync(join(tmpdir(),'watcher-launcher-'));
 try{
  writeFileSync(join(dir,'pi'),'#!/usr/bin/env node\nconsole.log(JSON.stringify({args:process.argv.slice(2),cwd:process.cwd(),cap:process.env.PI_SUBAGENT_MAX_SPAWNS_PER_SESSION}));\n',{mode:0o700});
  return run(dir,{...process.env,PATH:dir+':'+process.env.PATH,PI_SUBAGENT_CHILD:'0',WATCHER_SUBAGENT_PROVIDER:'',PI_SUBAGENT_MAX_SPAWNS_PER_SESSION:'12'});
 }finally{rmSync(dir,{recursive:true,force:true});}
}
test('watched launcher explicitly loads one isolated provider and observer, preserving cwd/arguments',()=>fake((dir,env)=>{
 const provider=join(dir,'provider.ts');writeFileSync(provider,'// explicit fixture provider');
 const result=spawnSync('sh',[launcher,'--model','example/model','a spaced prompt'],{cwd:dir,env:{...env,WATCHER_SUBAGENT_PROVIDER:provider},encoding:'utf8',timeout:5000});
 assert.equal(result.status,0,result.stderr);const row=JSON.parse(result.stdout);
 assert.deepEqual(row.args.slice(0,2),['--no-extensions','-e']);
 assert.equal(row.args[2],provider);
 assert.equal(row.args[3],'-e');assert.match(row.args[4],/\/extensions\/watcher\.ts$/);
 assert.deepEqual(row.args.slice(5),['--model','example/model','a spaced prompt']);
 assert.equal(realpathSync(row.cwd),realpathSync(dir));assert.equal(row.cap,'12');
}));
test('root-only launcher does not depend on a private provider or erase configured budgets',()=>fake((dir,env)=>{
 const result=spawnSync('sh',[launcher,'hello'],{cwd:dir,env,encoding:'utf8',timeout:5000});
 assert.equal(result.status,0,result.stderr);const row=JSON.parse(result.stdout);
 assert.deepEqual(row.args.slice(0,2),['--no-extensions','-e']);assert.match(row.args[2],/extensions\/watcher\.ts$/);assert.deepEqual(row.args.slice(3),['hello']);assert.equal(row.cap,'12');assert.match(result.stderr,/child telemetry remains unknown/);
}));
test('watched launcher refuses child activation',()=>fake((dir,env)=>{
 const result=spawnSync('sh',[launcher],{cwd:dir,env:{...env,PI_SUBAGENT_CHILD:'1'},encoding:'utf8',timeout:5000});
 assert.equal(result.status,1);assert.equal(result.stdout,'');assert.match(result.stderr,/not a child/);
}));
