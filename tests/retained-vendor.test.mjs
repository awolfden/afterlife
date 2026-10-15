import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
const vendor=process.env.WATCHER_M5GFX_PATH;
test('pinned framebuffer transactions are RAM-only; the inherited wrappers do not close SPI', {skip:!vendor},()=>{
 const base=readFileSync(join(vendor,'src/lgfx/v1/panel/Panel_FrameBufferBase.hpp'),'utf8');
 assert.match(base,/void beginTransaction\(void\) override \{\}/);assert.match(base,/void endTransaction\(void\) override \{\}/);
 const oled=readFileSync(join(vendor,'src/lgfx/v1/panel/Panel_AMOLED.hpp'),'utf8');
 const begin=oled.indexOf('struct Panel_AMOLED_Framebuffer : Panel_FrameBufferBase'),end=oled.indexOf('struct Panel_AMOLED :',begin);
 assert.ok(begin>=0&&end>begin);assert.doesNotMatch(oled.slice(begin,end),/\b(?:beginTransaction|endTransaction)\s*\(/);
});
test('actual pinned FlipBuffer reservation survives later allocator starvation for every legal line width', {skip:!vendor},()=>{
 const header=readFileSync(join(vendor,'src/lgfx/v1/platforms/common.hpp'),'utf8');
 const begin=header.indexOf('class FlipBuffer\n'),end=header.indexOf('\n  };',begin);
 assert.ok(begin>=0&&end>begin);const definition=header.slice(begin,end+5);
 const directory=mkdtempSync(join(tmpdir(),'watcher-retained-vendor-'));
 try{
  const source=join(directory,'test.cpp'),binary=join(directory,'test');
  writeFileSync(source,`#include <cstdint>\n#include <cstddef>\n#include <cstdlib>\n#include <cassert>\nstatic bool deny=false;static unsigned allocations=0;\nstatic void* heap_alloc_dma(size_t n){++allocations;return deny?nullptr:std::malloc(n);}\nstatic void heap_free(void* p){std::free(p);}\n${definition}\nint main(){FlipBuffer b;assert(b.reserve(936));assert(allocations==2);deny=true;auto a=b.getBuffer(936);auto c=b.getBuffer(936);assert(a&&c&&a!=c);for(unsigned i=0;i<100000;++i){auto p=b.getBuffer((i%468+1)*2);assert(p==a||p==c);}assert(allocations==2);}\n`);
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',source,'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.error?.message??build.stdout+build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.error?.message??run.stdout+run.stderr);
 }finally{rmSync(directory,{recursive:true,force:true});}
});
