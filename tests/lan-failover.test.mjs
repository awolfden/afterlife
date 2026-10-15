import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import tls from 'node:tls';
import {once} from 'node:events';
import {runHost} from '../host/run.mjs';
import {PairedLan} from '../host/paired-lan.mjs';
import {LanBootstrap} from '../host/lan-bootstrap.mjs';
import {AttentionModel} from '../host/attention.mjs';
import {DeliveryLedger} from '../host/delivery-ledger.mjs';
import {UsbController} from '../host/usb-controller.mjs';
import {crc32} from '../host/wire.mjs';
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(predicate){const deadline=Date.now()+7000;while(!predicate()){if(Date.now()>deadline)throw new Error('Test deadline');await delay(20);}}
for(const mode of ['silent','unproven'])test(`${mode} authenticated standby cannot block USB recovery`,{timeout:12000},async t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-lan-failover-')),helper=join(directory,'fake-python'),trigger=join(directory,'disconnect'),attempts=join(directory,'attempts');
 const wire=new URL('../host/wire.mjs',import.meta.url).href;
 writeFileSync(helper,`#!/usr/bin/env node
(async()=>{const fs=require('node:fs');const {crc32}=await import(${JSON.stringify(wire)});const attempts=${JSON.stringify(attempts)},trigger=${JSON.stringify(trigger)};const attempt=fs.existsSync(attempts)?Number(fs.readFileSync(attempts,'utf8'))+1:1;fs.writeFileSync(attempts,String(attempt));let seq=0,buffer='';function report(){const p='D2 '+(122+attempt)+' '+seq+' '+seq+' 0 0 0 0 0 ';process.stdout.write(p+crc32(p)+'\\n');}setInterval(()=>{if(attempt===1&&fs.existsSync(trigger))process.exit(1);report();},40);process.stdin.on('data',d=>{buffer+=d;let i;while((i=buffer.indexOf('\\n'))>=0){const line=buffer.slice(0,i);buffer=buffer.slice(i+1);if(line.startsWith('W1 '))seq=Number(line.split(' ')[2]);}});})();
`,{mode:0o700});
 let credentials,port;const host=await runHost({directory,port:'/dev/cu.usbmodemTEST',python:helper,lan:{ssid:'test',password:'test password',address:'192.168.1.2'},lanFactory:options=>{
  credentials={identity:options.identity,key:Buffer.from(options.key)};const listener=new PairedLan({...options,address:'127.0.0.1'}),start=listener.start.bind(listener);
  listener.start=async()=>{const value=await start();port=value.port;return value;};return listener;
 }});
 let peer;t.after(async()=>{peer?.destroy();await host.close();credentials.key.fill(0);rmSync(directory,{recursive:true,force:true});});
 await until(()=>host.status().ready);
 peer=tls.connect({host:'127.0.0.1',port,minVersion:'TLSv1.2',maxVersion:'TLSv1.2',ciphers:'ECDHE-PSK-CHACHA20-POLY1305',checkServerIdentity:(_host,cert)=>cert&&Object.keys(cert).length?new Error('Unexpected certificate'):undefined,pskCallback:()=>({identity:credentials.identity,psk:credentials.key})});
 peer.on('error',()=>{});await once(peer,'secureConnect');
 peer.on('data',()=>{if(mode==='unproven'){const prefix='D2 456 0 0 0 0 0 0 16 ';peer.write(prefix+crc32(prefix)+'\n');}});
 writeFileSync(trigger,'disconnect fake USB once');await until(()=>host.status().transport==='lan');
 assert.equal(host.status().lan.live,false);
 await until(()=>host.status().transport==='usb'&&host.status().ready&&host.status().boot===124);
 assert.equal(host.status().fault,null);assert.equal(host.status().lan.connected,false);
});
test('Wi-Fi flaps retain replay tombstones beyond the old 16-generation cap',t=>{
 const directory=mkdtempSync(join(tmpdir(),'watcher-generations-')),ledger=new DeliveryLedger(directory);
 t.after(()=>{ledger.close();rmSync(directory,{recursive:true,force:true});});
 const controller=new UsbController(new AttentionModel(),ledger);const status=boot=>{const prefix=`D2 ${boot} 0 0 0 0 0 0 0 `;return prefix+crc32(prefix);};
 for(let boot=1;boot<=40;boot++)assert.ok(controller.observe(status(boot),boot));
 assert.equal(controller.observe(status(1),41),false);assert.equal(controller.status().boot,40);assert.equal(controller.status().fault,null);
});
test('unacknowledged provisioning cannot reset its aggregate budget by rotating boot IDs',()=>{
 const bootstrap=new LanBootstrap({ssid:'test',password:'test password',address:'192.168.1.2',port:4444,key:Buffer.alloc(32,0x22),identity:'1'.repeat(64)});let writes=0;
 for(let boot=1;boot<=100;boot++)bootstrap.usbStatus({boot,capabilities:16},boot*100,()=>writes++);
 assert.equal(writes,9);assert.match(bootstrap.status().error,/Repeated RAM/);bootstrap.close();
});
