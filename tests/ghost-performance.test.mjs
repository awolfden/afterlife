import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

test('render memoization preserves exact ordered spans across states, scales, traits, hue boundaries and clipping',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-ghost-performance-'));
 try{
  const binary=join(dir,'test');
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('ghost-performance.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:60000});
  assert.equal(run.status,0,run.stdout+run.stderr);
  const summary=JSON.parse(run.stdout);assert.equal(summary.cases,45000);assert.ok(summary.exactSpansCompared>40_000_000);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('render optimization retains validation and bounded per-call memoization without shared state',()=>{
 const source=readFileSync(new URL('../firmware/render/ghost.h',import.meta.url),'utf8');
 const body=source.split('unsigned hue,unsigned archetype,Sink&& sink){')[1].split('// Existing callers')[0];
 assert.match(body,/if\(scale<32\|\|scale>256\|\|hue>359\|\|archetype>3\)return false/);
 assert.match(body,/return draw_ghost\(state,blink/);
 assert.match(body,/uint16_t colors\[64\]\{\},tinted\[64\]\{\};bool occupied\[64\]\{\}/);
 assert.match(body,/!occupied\[slot\]\|\|colors\[slot\]!=color/);
 const code=body.replace(/\/\/[^\n]*/g,'');assert.doesNotMatch(code,/\b(?:static|new|malloc|calloc|realloc)\b/);
});
