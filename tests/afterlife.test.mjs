import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const root=new URL('../',import.meta.url),sha=b=>createHash('sha256').update(b).digest('hex');
const manifest=JSON.parse(readFileSync(new URL('firmware/art/afterlife.manifest.json',root)));
test('Afterlife immutable artwork and source pins',()=>{
 for(const [path,pin] of [['preview/afterlife/disconnected.svg',manifest.sourceSha256],['preview/export-afterlife.mjs',manifest.exporterSha256],['firmware/art/afterlife.generated.h',manifest.headerSha256]])assert.equal(sha(readFileSync(new URL(path,root))),pin);
 assert.ok(manifest.binaryBytes<=150000);assert.equal(manifest.prompt.text,'Connect to Pi');assert.equal(manifest.width,466);assert.equal(manifest.height,466);
});
for(const enabled of [false,true])test(`Afterlife ${enabled?'enabled':'disabled'} native scene behavior`,()=>{
 const dir=mkdtempSync(join(tmpdir(),'afterlife-'));
 try{const binary=join(dir,'test'),pixels=join(dir,'decoded.rgb565');const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',...(enabled?['-DWATCHER_AFTERLIFE=1','-DWATCHER_GHOST_FAMILY=1']:[]),fileURLToPath(new URL('afterlife.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);const run=spawnSync(binary,[pixels],{encoding:'utf8',timeout:60000});assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);if(enabled)assert.equal(sha(readFileSync(pixels)),manifest.decodedSha256);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
