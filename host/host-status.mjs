// Explicit read-only inspection over the existing private hub socket. No device
// access, session registration, agent control, content or unsolicited messages.
import {createConnection} from 'node:net';
import {lstatSync} from 'node:fs';
import {dirname} from 'node:path';
const phases=['ipc-only','waiting-device','handshaking','confirmed'];
const sessions=['none','unwatched','selected','other'];
const keys=['v','op','phase','transport','sessions','session','serialRestarts'];
export function inspectionRequest(value){
 return !!value&&typeof value==='object'&&!Array.isArray(value)&&
  Object.keys(value).length===4&&value.v===1&&value.op==='inspect'&&value.seq===1&&
  (value.id===null||(typeof value.id==='string'&&/^[a-f0-9]{64}$/.test(value.id)));
}
export function validHostStatus(value){
 return !!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===keys.length&&keys.every(k=>Object.hasOwn(value,k))&&
  value.v===1&&value.op==='inspection'&&phases.includes(value.phase)&&['usb','lan','none'].includes(value.transport)&&
  Number.isInteger(value.sessions)&&value.sessions>=0&&value.sessions<=16&&sessions.includes(value.session)&&
  Number.isSafeInteger(value.serialRestarts)&&value.serialRestarts>=0&&
  (value.phase==='ipc-only'?value.transport==='none':value.transport!=='none');
}
export function inspectHost(path,id=null,{timeoutMs=750}={}){
 if(!inspectionRequest({v:1,op:'inspect',seq:1,id})||!Number.isInteger(timeoutMs)||timeoutMs<1||timeoutMs>5000)return Promise.resolve(null);
 try{
  const parent=lstatSync(dirname(path)),socket=lstatSync(path);
  if(!parent.isDirectory()||parent.uid!==process.getuid()||(parent.mode&0o777)!==0o700||!socket.isSocket()||socket.uid!==process.getuid()||(socket.mode&0o777)!==0o600)return Promise.resolve(null);
 }catch{return Promise.resolve(null);}
 return new Promise(resolve=>{
  let socket,timer,settled=false,line='';
  const finish=value=>{if(settled)return;settled=true;clearTimeout(timer);socket?.destroy();resolve(value);};
  try{
   socket=createConnection(path);timer=setTimeout(()=>finish(null),timeoutMs);
   socket.on('error',()=>finish(null));socket.on('end',()=>finish(null));socket.on('close',()=>finish(null));
   socket.on('connect',()=>socket.write(JSON.stringify({v:1,op:'inspect',seq:1,id})+'\n'));
   socket.on('data',bytes=>{
    if(bytes.length>2048){finish(null);return;}
    for(const byte of bytes){
     if(byte===10){try{const status=JSON.parse(line);finish(validHostStatus(status)?Object.freeze(status):null);}catch{finish(null);}return;}
     if(byte<32||byte>126||line.length>=2048){finish(null);return;}
     line+=String.fromCharCode(byte);
    }
   });
  }catch{finish(null);}
 });
}
export function describeHostStatus(status){
 if(!validHostStatus(status))return 'device delivery not confirmed; host status unavailable';
 const link=status.phase==='ipc-only'?'host is IPC-only; no device transport':status.phase==='waiting-device'?`${status.transport.toUpperCase()} bridge waiting for device status`:status.phase==='handshaking'?`${status.transport.toUpperCase()} device detected; handshake not confirmed`:`${status.transport.toUpperCase()} device link confirmed`;
 const session=status.session==='selected'?'; this session selected':status.session==='other'?'; another session selected':status.session==='unwatched'?'; this session not registered yet':'';
 return link+session;
}
