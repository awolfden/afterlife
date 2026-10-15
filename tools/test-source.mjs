#!/usr/bin/env node
// Explicit offline preview suite. No dependency installation or hardware access.
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const root=fileURLToPath(new URL('../',import.meta.url));
const suites=JSON.parse(readFileSync(new URL('../release/tests.json',import.meta.url),'utf8'));
if(Object.keys(suites).sort().join(',')!=='node,python')throw new Error('Unexpected test inventory');
for(const [kind,paths] of Object.entries(suites)){
 if(!Array.isArray(paths)||!paths.length||new Set(paths).size!==paths.length||paths.some(p=>typeof p!=='string'||!new RegExp(`^tests/[a-z0-9-]+${kind==='node'?'\\.test\\.mjs':'-test\\.py'}$`).test(p)))throw new Error('Invalid test inventory');
}
let failed=false;
const run=(command,args)=>{const result=spawnSync(command,args,{cwd:root,stdio:'inherit',env:{...process.env,PYTHONDONTWRITEBYTECODE:'1'}});if(result.error||result.status!==0){console.error(`${command}: suite failed${result.error?' to start':''}`);failed=true;}};
run(process.execPath,['--test','--test-concurrency=4',...suites.node]);
for(const path of suites.python)run(process.env.WATCHER_TEST_PYTHON??'python3',[path]);
process.exitCode=failed?1:0;
