import tls from 'node:tls';
import {randomBytes,constants} from 'node:crypto';
import {EventEmitter} from 'node:events';

export const pairing = () => ({identity:randomBytes(32).toString('hex'),key:randomBytes(32)});
const cipher='ECDHE-PSK-CHACHA20-POLY1305';
const privateAddress = address => {
 if(typeof address!=='string'||!/^\d+\.\d+\.\d+\.\d+$/.test(address))return false;
 const parts=address.split('.');if(parts.some(n=>String(Number(n))!==n||Number(n)>255))return false;
 const [a,b]=parts.map(Number);return a===10||a===127||(a===172&&b>=16&&b<=31)||(a===192&&b===168);
};

// Transport primitive only: not yet wired into runHost or firmware provisioning.
// TLS authenticates both peers using a fresh 256-bit key supplied over physical USB.
// ECDHE adds ephemeral key exchange; do not fall back to static PSK ciphers.
// No certificate-verification bypass, plaintext fallback, storage or credential logging.
// Caller still owns W1/D2 liveness, frame validation, delivery lock and transport arbitration.
export class PairedLan extends EventEmitter {
 #server;#key;#address;#port;#peer=null;#sockets=new Set();#started=false;#closed=false;#starting=null;#closing=null;
 constructor({address,port=0,identity,key}){
  super();
  if(!privateAddress(address)||!Number.isInteger(port)||port<0||port>65535||
     !/^[a-f0-9]{64}$/.test(identity)||!Buffer.isBuffer(key)||key.length!==32)throw new Error('Invalid LAN pairing configuration');
  this.#address=address;this.#port=port;this.#key=Buffer.from(key);
  this.#server=tls.createServer({minVersion:'TLSv1.2',maxVersion:'TLSv1.2',ciphers:cipher,
   secureOptions:constants.SSL_OP_NO_TICKET,handshakeTimeout:3000,pskCallback:(_socket,claimed)=>claimed===identity&&!this.#closed?this.#key:null},socket=>{
   // Even a destroyed peer owns the slot until its close/disconnect event.
   // A competing handshake must retry; it cannot skip disconnect(old).
   if(this.#closed||this.#peer){socket.destroy();return;}
   this.#peer=socket;socket.setNoDelay(true);socket.on('error',()=>{});
   socket.on('close',()=>{if(this.#peer===socket){this.#peer=null;this.emit('disconnect');}});
   // The caller gets a normal duplex byte stream; no arbitrary callback is invoked pre-authentication.
   this.emit('peer',socket);
  });
  this.#server.maxConnections=4;
  this.#server.on('connection',socket=>{
   this.#sockets.add(socket);socket.on('error',()=>{});socket.on('close',()=>this.#sockets.delete(socket));
  });
  this.#server.on('tlsClientError',(_error,socket)=>{socket.destroy();this.emit('handshakeRejected');}); // Count-only; also enforce handshake timeout cleanup.
  this.#server.on('error',error=>{void this.close();this.emit('transportError',error);});
 }
 async start(){
  if(this.#started||this.#closed)throw new Error('LAN listener cannot be restarted');
  this.#started=true;
  this.#starting=new Promise((resolve,reject)=>{
   const fail=error=>{this.#server.off('listening',ready);reject(error);};
   const ready=()=>{this.#server.off('error',fail);resolve();};
   this.#server.once('error',fail);this.#server.once('listening',ready);
   this.#server.listen({host:this.#address,port:this.#port});
  });
  try{await this.#starting;}catch(error){await this.close();throw error;}
  if(this.#closed)throw new Error('LAN listener closed during startup');
  return this.#server.address();
 }
 close(){
  if(!this.#closing)this.#closing=this.#shutdown();
  return this.#closing;
 }
 async #shutdown(){
  this.#closed=true;
  if(this.#starting)await this.#starting.catch(()=>{});
  for(const socket of this.#sockets)socket.destroy();
  if(this.#peer)this.#peer.destroy();
  await new Promise(resolve=>this.#server.close(resolve));
  this.#key.fill(0);
 }
}
