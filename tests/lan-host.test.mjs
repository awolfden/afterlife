import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync,chmodSync,symlinkSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import tls from 'node:tls';
import {once} from 'node:events';
import {runHost} from '../host/run.mjs';
import {PairedLan} from '../host/paired-lan.mjs';
import {LanBootstrap} from '../host/lan-bootstrap.mjs';
import {readLanConfig} from '../host/lan-config.mjs';
import {crc32} from '../host/wire.mjs';
const config=()=>({ssid:'test',password:'test password',address:'192.168.1.2',port:4444,key:Buffer.alloc(32,0x22),identity:'1'.repeat(64)});
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(predicate){const deadline=Date.now()+7000;while(!predicate()){if(Date.now()>deadline)throw new Error('Test deadline');await delay(20);}}
test('RAM bootstrap requires matching USB acknowledgement, retries boundedly, and reprovisions after RAM loss',()=>{
 const bootstrap=new LanBootstrap(config()),writes=[];const send=bytes=>writes.push(bytes);
 bootstrap.usbStatus({boot:1,capabilities:48},0,send);assert.equal(writes.length,1); // Previous daemon pairing is not ours.
 const transaction=Number(writes[0].split(' ')[2]);let prefix=`P2 2 ${transaction} 1 `;
 assert.equal(bootstrap.usbAck(prefix+crc32(prefix)),false);prefix=`P2 1 ${transaction} 1 `;
 assert.ok(bootstrap.usbAck(prefix+crc32(prefix)));assert.equal(bootstrap.status().paired,true);
 bootstrap.usbStatus({boot:2,capabilities:48},100,send);assert.equal(writes.length,1); // Link nonce != physical power cycle.
 bootstrap.usbStatus({boot:3,capabilities:16},200,send);assert.equal(writes.length,2);
 for(const at of [5200,10200,15200,20200])bootstrap.usbStatus({boot:3,capabilities:16},at,send);
 assert.equal(writes.length,4);assert.match(bootstrap.status().error,/not acknowledged/);bootstrap.close();
});
test('explicit credential config rejects broad permissions, symlinks and malformed JSON without echoing secrets',t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-lan-config-'));t.after(()=>rmSync(directory,{recursive:true,force:true}));
 const path=join(directory,'config.json'),value={ssid:'test',password:'secret-test-value',address:'192.168.1.2'};
 writeFileSync(path,JSON.stringify(value),{mode:0o600});assert.deepEqual(readLanConfig(path),value);
 const link=join(directory,'link');symlinkSync(path,link);assert.throws(()=>readLanConfig(link),/private/);
 chmodSync(path,0o644);assert.throws(()=>readLanConfig(path),/private/);chmodSync(path,0o600);
 writeFileSync(path,'secret-test-value NOT JSON');try{readLanConfig(path);assert.fail();}catch(error){assert.doesNotMatch(error.message,/secret-test-value/);}
});
for(const holdMs of [0,1500])test(`USB-only provisioning and real TLS failover after ${holdMs}ms badge hold`,{timeout:15000},async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-lan-host-')),trigger=join(directory,'disconnect'),helper=join(directory,'fake-python');
 const wire=new URL('../host/wire.mjs',import.meta.url).href;
 writeFileSync(helper,`#!/usr/bin/env node
(async()=>{const fs=require('node:fs');const {crc32}=await import(${JSON.stringify(wire)});let seq=0,paired=false,next=0,transaction=0,buffer='';const trigger=${JSON.stringify(trigger)};
function report(){const p='D2 123 '+seq+' '+seq+' 0 0 0 0 '+(paired?48:16)+' ';process.stdout.write(p+crc32(p)+'\\n');}
setInterval(()=>{if(fs.existsSync(trigger))process.exit(1);report();},40);
process.stdin.on('data',d=>{buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const line=buffer.slice(0,i);buffer=buffer.slice(i+1);const f=line.split(' ');if(f[0]==='W1')seq=Number(f[2]);if(f[0]==='P1'){const end=line.lastIndexOf(' ');if(crc32(line.slice(0,end+1))!==Number(f[5]))process.exit(2);if(f[3]==='0'){next=0;transaction=f[2];}if(Number(f[3])!==next++||f[2]!==transaction)process.exit(3);if(next===6){paired=true;const p='P2 123 '+transaction+' 1 ';process.stdout.write(p+crc32(p)+'\\n');}}}});})();
`,{mode:0o700});
 let listener,credentials,listenPort;const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper,lan:{ssid:'test',password:'test password',address:'192.168.1.2'},lanFactory:options=>{
  credentials={identity:options.identity,key:Buffer.from(options.key)};listener=new PairedLan({...options,address:'127.0.0.1'});
  const start=listener.start.bind(listener);listener.start=async()=>{const address=await start();listenPort=address.port;return address;};return listener;
 }});
 const sockets=[];t.after(async()=>{for(const socket of sockets)socket.destroy();await host.close();credentials.key.fill(0);rmSync(directory,{recursive:true,force:true});});
 await until(()=>host.status().ready&&host.status().lan.paired);
 const lines=[];let boot=456,respondAfter=Infinity;
 const connect=()=>{
  const socket=tls.connect({host:'127.0.0.1',port:listenPort,minVersion:'TLSv1.2',maxVersion:'TLSv1.2',ciphers:'ECDHE-PSK-CHACHA20-POLY1305',checkServerIdentity:(_host,cert)=>cert&&Object.keys(cert).length?new Error('Unexpected certificate'):undefined,pskCallback:()=>({identity:credentials.identity,psk:credentials.key})});
  sockets.push(socket);socket.on('error',()=>{});let seq=0,buffer='';
  const report=()=>{const prefix=`D2 ${boot} ${seq} ${seq} 0 0 0 0 16 `;socket.write(prefix+crc32(prefix)+'\n');};
  socket.on('data',data=>{buffer+=data;let i;while((i=buffer.indexOf('\n'))>=0){const line=buffer.slice(0,i);buffer=buffer.slice(i+1);lines.push(line);if(Date.now()<respondAfter)continue;if(line.startsWith('W1 '))seq=Number(line.split(' ')[2]);report();}});
  return socket;
 };
 const first=connect();await once(first,'secureConnect');assert.equal(host.status().transport,'usb');
 respondAfter=Date.now()+holdMs;writeFileSync(trigger,'disconnect synthetic USB');let selected=false;
 await until(()=>{const status=host.status();if(status.transport==='lan')selected=true;if(selected)assert.equal(status.transport,'lan','selected LAN must not bounce to USB during the badge hold');return status.lan.live&&status.boot===456;});
 assert.ok(lines.some(line=>line.startsWith('W1 ')));assert.ok(lines.every(line=>!line.startsWith('P1 ')));assert.equal(host.status().lan.paired,true);
 first.destroy();await until(()=>host.status().transport==='usb');boot=457;const second=connect();await once(second,'secureConnect');
 await until(()=>host.status().ready&&host.status().boot===457);assert.equal(host.status().fault,null);await host.close();assert.equal(await host.done,null);
});
