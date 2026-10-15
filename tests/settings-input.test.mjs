import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
test('portable Settings scenes and release-authoritative input under sanitizers',()=>{
 const work=mkdtempSync(join(tmpdir(),'settings-input-'));
 try{
  const binary=join(work,'input');execFileSync('clang++',['-std=c++17','-DWATCHER_GHOST_FAMILY=1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',fileURLToPath(new URL('../firmware/pet/main/',import.meta.url)),fileURLToPath(new URL('settings-input.cpp',import.meta.url)),'-o',binary],{timeout:30000});
  assert.match(execFileSync(binary,[],{encoding:'utf8',timeout:10000}),/settings scenes: bounded text/);
 }finally{rmSync(work,{recursive:true,force:true});}
});
