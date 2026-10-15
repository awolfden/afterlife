import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';

test('board reset retains vendor waits and all fatal PA-off checks before display setup',()=>{
 const source=readFileSync(new URL('../firmware/pet/main/main.cpp',import.meta.url),'utf8');
 const init=source.slice(source.indexOf('static bool init_board(){'),source.indexOf(' auto config=bus.config();'));
 assert.match(init,/board_stage=61;[\s\S]*lgfx::delay\(10\);if\(!reg_mask\(ioe,0x05,0x18,0\)\)return false;/);
 assert.match(init,/board_stage=62;\s*lgfx::delay\(8\);if\(!reg_mask\(ioe,0x05,0,0x18\)\)return false;\s*lgfx::delay\(2\);/);
 assert.match(init,/board_stage=63;\s*if\(!reg_mask\(ioe,0x14,2,0\)\)return false;\s*board_stage=64;\s*if\(!reg_mask\(ioe,0x04,0,2\)\)return false;\s*board_stage=65;\s*if\(!reg_mask\(ioe,0x06,2,0\)\)return false;/);
 assert.doesNotMatch(init,/vTaskDelay\(pdMS_TO_TICKS\((?:10|8|2)\)\)/);
 assert.match(init,/if\(!motor_disable\(\)\)return false;/);
 assert.match(source,/if\(!init_board\(\)\)\{\s*motor_off\(nullptr\);/);
});

test('actual configured vendor delay preserves 10/8/2 ms at every 100 Hz tick phase and clock wrap', {skip:!process.env.WATCHER_M5GFX_PATH},()=>{
 // Read the SAME installed helper used by the firmware build, not a hand-written substitute.
 const header=readFileSync(join(process.env.WATCHER_M5GFX_PATH,'src/lgfx/v1/platforms/esp32/common.hpp'),'utf8');
 const begin=header.indexOf('static inline void delay(uint32_t ms)'),end=header.indexOf('\n  }\n',begin);
 assert.ok(begin>=0&&end>begin);
 const helper=header.slice(begin,end+5);
 assert.ok(helper.includes('delayMicroseconds(ms - time)'));
 const dir=mkdtempSync(join(tmpdir(),'watcher-board-reset-'));
 try{
  const cpp=join(dir,'test.cpp'),binary=join(dir,'test');
  writeFileSync(cpp,`#include <algorithm>\n#include <cstdint>\n#include <cassert>\n#include <cstdio>\n#include <initializer_list>\nstatic constexpr uint32_t portTICK_PERIOD_MS=10;\nstatic uint32_t clock_us=0,elapsed=0,phase=0,yields=0;\nstatic uint32_t micros(){return clock_us;}\nstatic void advance(uint32_t us){clock_us+=us;elapsed+=us;}\nstatic void vTaskDelay(uint32_t ticks){assert(ticks>0);++yields;advance(ticks*10000-phase);}\nstatic void delayMicroseconds(uint32_t us){advance(us);}\n${helper}\nint main(){\n for(uint32_t start:{0u,0xfffffff0u})for(uint32_t ms:{10u,8u,2u})for(uint32_t p=0;p<10000;p++){\n  clock_us=start;elapsed=0;phase=p;yields=0;delay(ms);\n  assert(yields==1&&elapsed>=ms*1000&&elapsed<=10000);\n }\n // Original pdMS_TO_TICKS floors both required short delays to zero.\n assert(8*100/1000==0&&2*100/1000==0);\n std::puts("60000 tick-phase/wrap cases pass; original short waits truncate to zero");\n}\n`);
  const build=spawnSync('clang++',['-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',cpp,'-o',binary],{encoding:'utf8',timeout:60000});assert.equal(build.status,0,build.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stdout+run.stderr);
 }finally{rmSync(dir,{recursive:true,force:true});}
});
