import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {encodeProvision} from '../host/lan-provision.mjs';
import {crc32} from '../host/wire.mjs';
const config=()=>({ssid:'café',password:'test password',address:'192.168.1.2',port:4444,identity:'1'.repeat(64),key:Buffer.alloc(32,0x22)});
const generation={boot:42,transaction:99};
test('USB RAM bootstrap validates inputs, has bounded chunks and never mutates caller key',()=>{
 const value=config(),lines=encodeProvision(value,generation);assert.equal(lines.length,6);assert.ok(lines.every(line=>line.length<192));assert.deepEqual(value.key,Buffer.alloc(32,0x22));
 for(const patch of [{ssid:''},{ssid:'a\n'},{ssid:'\ud800'},{ssid:'🐺'.repeat(9)},{password:'short'},{password:'x'.repeat(64)},{password:'abc\ndefghi'},{address:'0.0.0.0'},{address:'localhost'},{address:'127.0.0.1'},{address:'192.168.01.2'},{address:'8.8.8.8'},{port:0},{port:65536},{identity:'0'.repeat(64)},{key:Buffer.alloc(32)}])assert.throws(()=>encodeProvision({...config(),...patch},generation));
 for(const patch of [{boot:0},{transaction:0},{boot:2**32}])assert.throws(()=>encodeProvision(config(),{...generation,...patch}));
});
test('host provisioning packets cross the firmware assembler under sanitizers',()=>{
 const dir=mkdtempSync(join(tmpdir(),'watcher-provision-'));
 try{
  const binary=join(dir,'test'),build=spawnSync('clang++',['-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',fileURLToPath(new URL('lan-provision.cpp',import.meta.url)),'-o',binary],{encoding:'utf8',timeout:60000});
  assert.equal(build.status,0,build.stderr);
  const lines=encodeProvision(config(),generation);
  const run=(mode,packets=lines)=>{const result=spawnSync(binary,[mode],{input:packets.join(''),encoding:'utf8',timeout:10000});assert.equal(result.status,0,`${mode}: ${result.stderr}`);assert.equal(result.stdout,'provision-ok\n');};
  run('valid');run('ingress');run('busy');
  for(const mode of ['boot','slow','clear','expire','take-boot','take-expired','negative','backwards'])run(mode);
  run('restart',[...lines.slice(0,3),...encodeProvision(config(),{boot:42,transaction:100})]);
  run('max',encodeProvision({...config(),ssid:'x'.repeat(32),password:'y'.repeat(63),port:65535},{boot:0xffffffff,transaction:0xffffffff}));
  run('reject',[lines[0],encodeProvision(config(),{boot:42,transaction:100})[1],...lines.slice(2)]);
  run('reject',[...lines,lines[5]]);
  run('reject',[lines[0].replace('\n','x\n'),...lines.slice(1)]);
  run('reject',['P1 '+'a'.repeat(189)+'\n',...lines.slice(1)]);
  const upper=lines[0].slice(0,lines[0].lastIndexOf(' ')+1).replace('c3a9','C3A9');
  run('reject',[upper+crc32(upper)+'\n',...lines.slice(1)]);
  run('reject',lines.slice(0,5));run('reject',[lines[0],lines[0],...lines.slice(2)]);
  run('reject',[lines[0],...lines.slice(2)]);run('reject',[...lines].reverse());
  run('reject',[lines[0].replace(/ [0-9]+\n$/,' 0\n'),...lines.slice(1)]);
  const payload=Buffer.concat(lines.map(line=>Buffer.from(line.split(' ')[4],'hex')));
  const mutated=change=>{const data=Buffer.from(payload);change(data);return Array.from({length:6},(_,i)=>{const prefix=`P1 42 99 ${i} ${data.subarray(i*32,Math.min(168,(i+1)*32)).toString('hex')} `;return `${prefix}${crc32(prefix)}\n`;});};
  for(const change of [d=>d[0]=2,d=>d[1]=33,d=>d[2]=7,d=>d[34]=1,d=>d[97]=1,d=>d[98]=127,d=>d.fill(0,102,104),d=>d.fill(0,104,136),d=>d.fill(0,136,168)])run('reject',mutated(change));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
