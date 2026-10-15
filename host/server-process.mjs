// Session-owned host, never adopted by PID and never launched on extension load.
import {fork,execFile} from 'node:child_process';
import {lstat} from 'node:fs/promises';
import {homedir,networkInterfaces} from 'node:os';
import {join,isAbsolute} from 'node:path';
import {fileURLToPath} from 'node:url';
import {promisify} from 'node:util';
const execute=promisify(execFile);
export const serverDirectory=()=>join(homedir(),'.local/share/pi-watcher-pet/run');
export async function serverLockPresent(directory){
 try{await lstat(join(directory,'delivery.lock'));return true;}
 catch(error){if(error.code==='ENOENT')return false;throw Error('Host ownership unavailable');}
}
export function privateAddresses(interfaces=networkInterfaces()){
 const addresses=[];
 for(const entries of Object.values(interfaces))for(const entry of entries??[]){
  if(entry.internal||entry.family!=='IPv4')continue;
  const parts=entry.address.split('.');
  if(parts.length!==4||parts.some(p=>!/^\d+$/.test(p)||String(Number(p))!==p||Number(p)>255))continue;
  const [a,b]=parts.map(Number);
  if(a===10||(a===172&&b>=16&&b<=31)||(a===192&&b===168))addresses.push(entry.address);
 }
 return [...new Set(addresses)].sort();
}
export async function discoverServerSetup({python=process.env.WATCHER_PYTHON,platform=process.platform,executePython=execute,interfaces=networkInterfaces}={}){
 if(platform!=='darwin')throw Error('This launcher currently requires macOS');
 // Explicit trusted interpreter only: never search PATH or assume a private SDK.
 if(typeof python!=='string'||!isAbsolute(python)||python.length>4096||/[\x00-\x1f\x7f]/.test(python))throw Error('Set WATCHER_PYTHON to an absolute Python path');
 const script='import sys,json,serial; serial.__version__ == "3.5" or sys.exit("pyserial 3.5 required"); from serial.tools import list_ports; print(json.dumps([p.device for p in list_ports.comports() if p.vid == 0x303a and p.pid == 0x1001]))';
 const {stdout}=await executePython(python,['-I','-c',script],{timeout:3000,maxBuffer:8192});
 const ports=JSON.parse(stdout);
 if(!Array.isArray(ports)||ports.length>32||ports.some(p=>typeof p!=='string'||!/^\/dev\/cu\.usbmodem[0-9A-Za-z]+$/.test(p)))throw Error('Invalid USB discovery');
 return {python,ports:[...new Set(ports)].sort(),addresses:privateAddresses(interfaces())};
}
export const linkFields=['paired','deviceHasIp','connected','live','deviceFault','provisionError'];
export function validServerLink(value){
 return value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).sort().join(',')===[...linkFields].sort().join(',')&&linkFields.every(key=>typeof value[key]==='boolean');
}
export class OwnedServer {
 #child=null;#link=null;#ready=false;#closing=null;#launch;#inspection=null;#request=0;
 constructor({launch=()=>fork(fileURLToPath(new URL('./server-worker.mjs',import.meta.url)),[],{execArgv:[],stdio:['ignore','ignore','ignore','ipc']})}={}){this.#launch=launch;}
 get running(){return this.#child!==null;}
 get ready(){return this.#ready;}
 get link(){return this.#link?{...this.#link}:null;}
 inspectLink(){
  if(!this.#ready||!this.#child?.connected||this.#closing)return Promise.resolve(null);
  if(this.#inspection)return this.#inspection.promise;
  const request=++this.#request;let resolve;
  const promise=new Promise(done=>{resolve=done;});
  const finish=value=>{if(this.#inspection?.request!==request)return;clearTimeout(timer);this.#inspection=null;resolve(value);};
  const timer=setTimeout(()=>finish(null),750);
  this.#inspection={request,promise,finish};
  try{this.#child.send({type:'inspect',request},error=>{if(error)finish(null);});}catch{finish(null);}
  return promise;
 }
 async start(options){
  if(this.#child||this.#closing)throw Error('Server already owned');
  const child=this.#launch();this.#child=child;this.#ready=false;this.#link=null;
  const closeCurrent=()=>this.#child===child?this.close():Promise.resolve();
  let finish,timer;
  const ready=new Promise((resolve,reject)=>{finish=ok=>{clearTimeout(timer);ok?resolve():reject(Error('Server startup unavailable'));};});
  child.on('message',message=>{
   if(this.#child!==child||this.#closing)return;
   if(message?.type==='ready'){this.#ready=true;finish(true);}
   else if(message?.type==='link'&&validServerLink(message.value)){
    this.#link={...message.value};
    if(message.request===this.#inspection?.request)this.#inspection?.finish({...message.value});
   }
   else if(message?.type==='failed'){finish(false);void closeCurrent();}
  });
  child.on('error',()=>{finish(false);void closeCurrent();});
  const exited=()=>{if(this.#child===child){this.#inspection?.finish(null);this.#child=null;this.#link=null;this.#ready=false;}finish(false);};
  child.once('exit',exited);child.once('close',exited);
  timer=setTimeout(()=>{finish(false);void closeCurrent();},10000);
  try{child.send({type:'start',options},error=>{if(error){finish(false);void closeCurrent();}});}
  catch{finish(false);void closeCurrent();}
  try{await ready;}catch{await closeCurrent();throw Error('Server startup unavailable');}
 }
 close(){
  if(this.#closing)return this.#closing;
  this.#inspection?.finish(null);
  const child=this.#child;if(!child)return Promise.resolve();
  // Only this directly owned ChildProcess is controlled. Never read/kill a lock PID.
  this.#closing=new Promise(resolve=>{
   const alive=()=>child.exitCode===null&&child.signalCode===null;
   const timer=setTimeout(()=>{if(alive())child.kill('SIGTERM');},2000);
   const hardTimer=setTimeout(()=>{if(alive())child.kill('SIGKILL');},4000);
   timer.unref?.();hardTimer.unref?.();
   const finish=()=>{clearTimeout(timer);clearTimeout(hardTimer);resolve();};
   // With ignored stdio and an explicitly disconnected IPC channel, Node may
   // emit exit without close. No captured output remains to drain here.
   child.once('exit',finish);child.once('close',finish);
   if(child.connected)child.disconnect(); // Worker also sees this on owner crash.
   else if(!alive())finish();
  }).finally(()=>{this.#closing=null;});
  return this.#closing;
 }
}
