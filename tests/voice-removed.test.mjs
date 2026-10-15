import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,existsSync,mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawnSync,execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {AttentionModel} from '../host/attention.mjs';
import {SessionHub} from '../host/session-hub.mjs';
import {SourceClient} from '../host/source-client.mjs';
const root=fileURLToPath(new URL('../',import.meta.url));
const id='a'.repeat(64),item='b'.repeat(64);
test('metadata ingress remains functional and rejects former voice operations',()=>{
 const model=new AttentionModel(),hub=new SessionHub(model),peer=hub.attach(0);
 for(const op of ['voice-ready','voice-result'])assert.equal(peer.receive({v:1,seq:1,op},0),false);
 assert.equal(model.snapshot().sessions.length,0);
 assert.equal(peer.receive({v:1,seq:1,op:'hello',id},0),true);
 assert.equal(peer.receive({v:1,seq:2,op:'status',state:'working',children:0,available:true},1),true);
 for(const op of ['voice-ready','voice-result'])assert.equal(peer.receive({v:1,seq:3,op},2),false);
 assert.equal(peer.receive({v:1,seq:3,op:'attention',item,kind:'input'},3),true);
 assert.equal(model.snapshot().sessions[0].state,'needs-input');
 assert.equal(typeof hub.voiceOwner,'undefined');
 const client=new SourceClient(id,'/unused-test-socket');
 assert.equal(typeof client.voice,'undefined');client.close();
});
test('removed helper flags reject before any host or helper starts',()=>{
 for(const flag of ['--voice-helper','--voice-sha256']){
  const result=spawnSync(process.execPath,['host/run.mjs',flag,'unused'],{cwd:root,encoding:'utf8',timeout:5000});
  assert.equal(result.status,1);assert.match(result.stderr,/Unknown option/);assert.doesNotMatch(result.stdout,/Watcher host:/);
 }
});
test('output-only native adapter, retired chord and preserved LAN budgets',()=>{
 const work=mkdtempSync(join(tmpdir(),'watcher-no-capture-'));
 try{
  const includes=['tests/fixtures/factory','firmware/pet/components/watcher_codec/upstream/include','firmware/pet/components/watcher_codec/upstream/interface'].flatMap(p=>['-I',join(root,p)]);
  const binary=join(work,'checks');
  execFileSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',...includes,join(root,'tests/voice-removed.cpp'),'-o',binary],{timeout:30000});
  assert.match(execFileSync(binary,[],{encoding:'utf8',timeout:10000}),/checks passed/);
  const main=readFileSync(join(root,'firmware/pet/main/main.cpp'),'utf8');
  const start=main.indexOf('static void accept(const char* line,size_t length){');
  const end=main.indexOf('\n#ifdef WATCHER_LAN\nstatic void dispatch',start);
  assert.ok(start>=0&&end>start);
  writeFileSync(join(work,'actual-accept.inc'),main.slice(start,end));
  const dispatch=join(work,'dispatch');
  for(const defines of [[],['-DWATCHER_AFTERLIFE=1']]){
   execFileSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',...defines,'-I',work,join(root,'tests/retired-dispatch.cpp'),'-o',dispatch],{timeout:30000});
   assert.match(execFileSync(dispatch,[],{encoding:'utf8',timeout:10000}),/retired packets preserve authority/);
  }
 }finally{rmSync(work,{recursive:true,force:true});}
});
test('owned voice modules are absent and positive metadata imports remain',()=>{
 for(const path of ['host/voice-rpc.mjs','host/voice-pipeline.mjs','host/local-transcriber.mjs','host/local-speech.swift','firmware/pet/main/voice-device.h','firmware/pet/main/capture-read-guard.h'])assert.equal(existsSync(root+path),false,path);
 const extension=readFileSync(root+'extensions/watch-command.ts','utf8');
 assert.match(extension,/registerWatcher/);assert.doesNotMatch(extension,/voiceFactory|createVoiceUiSession/);
});
