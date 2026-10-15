import test from 'node:test';
import assert from 'node:assert/strict';
import tls from 'node:tls';
import net from 'node:net';
import {once} from 'node:events';
import {PairedLan,pairing} from '../host/paired-lan.mjs';
const client=(port,credentials)=>tls.connect({host:'127.0.0.1',port,minVersion:'TLSv1.2',maxVersion:'TLSv1.2',ciphers:'ECDHE-PSK-CHACHA20-POLY1305',
 // PSK-only cipher has no X.509 hostname: authenticate with the key, reject any certificate.
 // rejectUnauthorized is NOT disabled; bad PSKs must fail the TLS Finished verification.
 checkServerIdentity:(_host,cert)=>cert&&Object.keys(cert).length?new Error('Unexpected certificate in PSK-only test'):undefined,pskCallback:()=>({identity:credentials.identity,psk:credentials.key})});
test('pairing uses fresh random identity and 256-bit key; addresses fail closed',()=>{
 const a=pairing(),b=pairing();assert.equal(a.key.length,32);assert.notEqual(a.identity,b.identity);assert.notDeepEqual(a.key,b.key);
 for(const address of ['0.0.0.0','8.8.8.8','::','localhost','192.168.01.1','172.32.0.1','10.0.0.256'])assert.throws(()=>new PairedLan({address,...a}));
});
test('startup cancellation and repeated close cannot leave a listener alive',async()=>{
 const server=new PairedLan({address:'127.0.0.1',...pairing()});
 const started=assert.rejects(server.start(),/closed during startup/),closed=server.close();
 assert.equal(server.close(),closed);await Promise.all([started,closed]);await assert.rejects(server.start());
 const neverStarted=new PairedLan({address:'127.0.0.1',...pairing()});await neverStarted.close();await assert.rejects(neverStarted.start());
});
test('PSK TLS authenticates, transfers bytes, rejects wrong keys/identities and cannot replace an active peer',{timeout:10000},async t=>{
 const credentials=pairing(),server=new PairedLan({address:'127.0.0.1',...credentials});
 t.after(()=>server.close());const {port}=await server.start();let peers=0;server.on('peer',socket=>{peers++;socket.on('error',()=>{});});
 const peerReady=once(server,'peer'),first=client(port,credentials);t.after(()=>first.destroy());
 await once(first,'secureConnect');const [peer]=await peerReady;
 assert.equal(first.getCipher().standardName,'TLS_ECDHE_PSK_WITH_CHACHA20_POLY1305_SHA256');
 const received=once(first,'data');peer.write('Q1\n');assert.equal((await received)[0].toString(),'Q1\n');
 for(const bad of [{...credentials,key:Buffer.alloc(32)},{...credentials,identity:'0'.repeat(64)}]){
  const socket=client(port,bad);t.after(()=>socket.destroy());let authenticated=false;socket.on('secureConnect',()=>authenticated=true);
  socket.on('error',()=>{});await new Promise(resolve=>socket.once('close',resolve));assert.equal(authenticated,false);
 }
 const second=client(port,credentials);t.after(()=>second.destroy());second.on('error',()=>{});
 await new Promise(resolve=>second.once('close',resolve));assert.equal(peers,1);assert.equal(first.destroyed,false);
 const disconnected=once(server,'disconnect');first.destroy();await disconnected;
 const nextPeer=once(server,'peer'),third=client(port,credentials);t.after(()=>third.destroy());await once(third,'secureConnect');await nextPeer;assert.equal(peers,2);
 await server.close();await assert.rejects(server.start());
});
test('destroyed peer retains its slot until disconnect is delivered',{timeout:10000},async t=>{
 const credentials=pairing(),server=new PairedLan({address:'127.0.0.1',...credentials});t.after(()=>server.close());
 const {port}=await server.start(),events=[];server.on('peer',()=>events.push('peer'));server.on('disconnect',()=>events.push('disconnect'));
 const ready=once(server,'peer'),first=client(port,credentials);first.on('error',()=>{});t.after(()=>first.destroy());
 const firstClosed=new Promise(resolve=>first.once('close',resolve));
 const connected=once(first,'secureConnect');const [peer]=await ready;await connected;
 // Real destroy followed immediately by a new connection, with no await of old close.
 peer.destroy();assert.equal(peer.destroyed,true);
 const next=once(server,'peer'),replacement=client(port,credentials);replacement.on('error',()=>{});t.after(()=>replacement.destroy());
 const replacementClosed=new Promise(resolve=>replacement.once('close',resolve));
 const connectedAgain=once(replacement,'secureConnect');await next;await connectedAgain;
 assert.deepEqual(events,['peer','disconnect','peer']);
 await server.close();await Promise.all([firstClosed,replacementClosed]);
});
test('failed bind closes itself, key input is copied, and shutdown removes listener',{timeout:10000},async t=>{
 const credentials=pairing(),saved={...credentials,key:Buffer.from(credentials.key)};
 const server=new PairedLan({address:'127.0.0.1',...credentials});t.after(()=>server.close());credentials.key.fill(0);
 const {port}=await server.start(),duplicate=new PairedLan({address:'127.0.0.1',port,...saved});t.after(()=>duplicate.close());
 let errors=0;duplicate.on('transportError',()=>errors++);
 await assert.rejects(duplicate.start(),{code:'EADDRINUSE'});assert.equal(errors,1);await duplicate.close();await assert.rejects(duplicate.start());
 const ready=once(server,'peer'),socket=client(port,saved);socket.on('error',()=>{});t.after(()=>socket.destroy());await ready;
 await server.close();const refused=client(port,saved);t.after(()=>refused.destroy());
 const [error]=await once(refused,'error');assert.equal(error.code,'ECONNREFUSED');
});
test('non-PSK and wrong-version clients cannot become peers; rejection events have no payload',{timeout:10000},async t=>{
 const server=new PairedLan({address:'127.0.0.1',...pairing()});t.after(()=>server.close());const {port}=await server.start();
 const closed=[];let peers=0,rejected=0;server.on('peer',()=>peers++);server.on('handshakeRejected',(...args)=>{assert.equal(args.length,0);rejected++;});
 for(const socket of [tls.connect({host:'127.0.0.1',port,maxVersion:'TLSv1.2'}),tls.connect({host:'127.0.0.1',port,minVersion:'TLSv1.3'}),net.connect({host:'127.0.0.1',port})]){
  // Install listeners immediately for all three before they can emit asynchronous errors.
  socket.on('error',()=>{});t.after(()=>socket.destroy());
  if(!(socket instanceof tls.TLSSocket))socket.write('not TLS\n');
  closed.push(new Promise(resolve=>socket.once('close',resolve)));
 }
 await Promise.all(closed);assert.equal(peers,0);assert.equal(rejected,3);
});
test('pending handshakes have a connection cap and timeout',{timeout:8000},async t=>{
 const server=new PairedLan({address:'127.0.0.1',...pairing()});t.after(()=>server.close());const {port}=await server.start();
 const sockets=[],closed=[];
 for(let i=0;i<4;i++){
  const socket=net.connect({host:'127.0.0.1',port});socket.on('error',()=>{});sockets.push(socket);t.after(()=>socket.destroy());
  closed.push(new Promise(resolve=>socket.once('close',resolve)));await once(socket,'connect');
 }
 const excess=net.connect({host:'127.0.0.1',port});excess.on('error',()=>{});t.after(()=>excess.destroy());
 await new Promise(resolve=>excess.once('close',resolve));assert.ok(sockets.every(s=>!s.destroyed));
 await Promise.all(closed);assert.ok(sockets.every(s=>s.destroyed));
});
