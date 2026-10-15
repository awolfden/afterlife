// Opt-in Pi -> private hub client. Metadata-only; no reverse agent-control channel.
import {createConnection} from 'node:net';
import {lstatSync} from 'node:fs';
import {dirname} from 'node:path';
import {validIdentity} from './session-identity.mjs';
import {inspectHost} from './host-status.mjs';
const opaque=id=>typeof id==='string'&&/^[a-f0-9]{64}$/.test(id);
export class SourceClient {
 #identity=null;
 #id;#path;#socket;#queue=[];#seq=0;#open=new Map();#retired=new Set();#status={state:'idle',children:0,available:true};#synced=false;#closed=false;#ready=false;#blocked=false;#retry;#attempt=0;#heartbeat;#healthy;#delays;
 constructor(id,path,{retryDelays=[0,500,1500,3500,8000,15000,30000]}={}){
  if(!opaque(id)||!Array.isArray(retryDelays)||!retryDelays.length||retryDelays.length>16||retryDelays.some(n=>!Number.isSafeInteger(n)||n<0||n>60000)||retryDelays.at(-1)<1)throw new TypeError('Invalid owner/retry policy');
  this.#id=id;this.#path=path;this.#delays=[...retryDelays];
 }
 register(id){return id===this.#id&&!this.#closed;}
 #enqueue(op,fields={}){
  if(!this.#ready)return false;
  if(this.#queue.length>=384 || this.#seq===Number.MAX_SAFE_INTEGER){this.#socket?.destroy();return false;}
  this.#queue.push(JSON.stringify({v:1,seq:++this.#seq,op,...fields}).replace(/[^\x20-\x7e]/g,char=>'\\u'+char.charCodeAt(0).toString(16).padStart(4,'0'))+'\n');this.#pump();return true;
 }
 #pump(){while(this.#ready&&!this.#blocked&&this.#queue.length){if(!this.#socket.write(this.#queue.shift())){this.#blocked=true;return;}}}
 #retryLater(){if(this.#closed)return;clearTimeout(this.#retry);const delay=this.#delays[Math.min(this.#attempt++,this.#delays.length-1)];this.#attempt=Math.min(this.#attempt,this.#delays.length);this.#retry=setTimeout(()=>this.#connect(),delay);this.#retry.unref?.();}
 #connect(){
  if(this.#closed||this.#socket)return;
  try{
   const parent=lstatSync(dirname(this.#path)),socket=lstatSync(this.#path);
   if(!parent.isDirectory()||parent.uid!==process.getuid()||(parent.mode&0o777)!==0o700||!socket.isSocket()||socket.uid!==process.getuid()||(socket.mode&0o777)!==0o600)throw new Error('Unsafe watcher socket');
  }catch{this.#retryLater();return;}
  const socket=createConnection(this.#path);this.#socket=socket;
  // Observation sockets carry metadata outbound only. Inspection uses a separate socket.
  socket.on('data',()=>socket.destroy());
  socket.on('error',()=>socket.destroy());
  socket.on('close',()=>{if(this.#socket!==socket)return;this.#ready=false;this.#socket=undefined;this.#queue=[];clearInterval(this.#heartbeat);clearTimeout(this.#healthy);this.#retryLater();});
  socket.on('drain',()=>{this.#blocked=false;this.#pump();});
  socket.on('connect',()=>{
   if(this.#closed){socket.destroy();return;}
   this.#ready=true;this.#blocked=false;this.#seq=0;this.#queue=[];
   // A TCP/Unix connect alone is not successful ingress: rejected replay must back off.
   this.#healthy=setTimeout(()=>{this.#attempt=0;},5000);this.#healthy.unref?.();
   this.#enqueue('hello',{id:this.#id});
   this.#enqueue('status',this.#status);
   for(const item of this.#retired)this.#enqueue('resolve',{item});
   for(const [item,value] of this.#open){
    this.#enqueue(value.confirmed?'attention':'unknown',{item,kind:value.kind});
   }
   this.#enqueue('sync',{synced:this.#synced});
   if(this.#identity)this.#enqueue('identity',{identity:this.#identity});
   this.#heartbeat=setInterval(()=>this.#enqueue('heartbeat'),1000);this.#heartbeat.unref?.();
  });
 }
 get connected(){return this.#ready&&!this.#closed;}
 inspect(){return this.connected?inspectHost(this.#path,this.#id):Promise.resolve(null);}
 start(){this.#retryLater();}
 reconnect(){clearTimeout(this.#retry);this.#attempt=0;if(this.#socket)this.#socket.destroy();else this.#retryLater();}
 status(id,{state,activeChildren=0,available=true}){
  if(!this.register(id)||!['idle','thinking','working','completed','failed'].includes(state)||!Number.isInteger(activeChildren)||activeChildren<0||activeChildren>256||typeof available!=='boolean')return false;
  this.#status={state,children:activeChildren,available};this.#enqueue('status',this.#status);return true;
 }
 identity(id,value){
  if(!this.register(id)||(value!==null&&!validIdentity(value)))return false;
  if(value?.spaceId===this.#identity?.spaceId&&value?.spaceTitle===this.#identity?.spaceTitle)return true;
  this.#identity=value?{...value}:null;this.#enqueue('identity',{identity:this.#identity});return true;
 }
 supervisorSync(id,synced){if(!this.register(id)||typeof synced!=='boolean')return false;this.#synced=synced;this.#enqueue('sync',{synced});return true;}
 attention(id,{itemId,kind}){
  if(!this.register(id)||!opaque(itemId)||!['input','child','failure'].includes(kind)||this.#retired.has(itemId))return false;
  const prior=this.#open.get(itemId);if((prior&&prior.kind!==kind)||(!prior&&this.#open.size>=64))return false;
  this.#open.set(itemId,{kind,confirmed:true});this.#enqueue('attention',{item:itemId,kind});return true;
 }
 uncertain(id,item){
  if(!this.register(id)||!opaque(item)||this.#retired.has(item))return false;
  const prior=this.#open.get(item);if(!prior&&this.#open.size>=64)return false;
  const kind=prior?.kind??'child';
  this.#open.set(item,{kind,confirmed:false});this.#enqueue('unknown',{item,kind});return true;
 }
 resolve(id,item){
  if(!this.register(id)||!opaque(item))return false;
  this.#open.delete(item);this.#retired.add(item);if(this.#retired.size>256)this.#retired.delete(this.#retired.values().next().value);
  this.#enqueue('resolve',{item});return true;
 }
 close({unwatch=false}={}){if(this.#closed)return;this.#enqueue(unwatch?'unwatch':'end');this.#closed=true;clearTimeout(this.#retry);clearTimeout(this.#healthy);clearInterval(this.#heartbeat);this.#ready=false;this.#socket?.end();}
}
