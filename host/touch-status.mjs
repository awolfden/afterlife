import {createConnection} from 'node:net';
import {lstatSync} from 'node:fs';
import {dirname} from 'node:path';
import {validTouchSnapshot,validRangeSnapshot} from './touch-usb.mjs';
import {validTimingSnapshot} from './touch-timing.mjs';
export function timingInspectionRequest(v){return !!v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===3&&v.v===1&&v.op==='touch-timing'&&v.seq===1;}
export function validTimingStatus(v){return validStatus(v,'touch-timing-inspection',validTimingSnapshot);}
export function inspectTouchTiming(path,{timeoutMs=2000}={}){return inspectDiagnostic(path,timeoutMs,{v:1,op:'touch-timing',seq:1},validTimingStatus);}
export function touchInspectionRequest(v){return !!v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===3&&v.v===1&&v.op==='touch-inspect'&&v.seq===1;}
export function rangeInspectionRequest(v){return !!v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===4&&v.v===1&&v.op==='touch-range'&&v.seq===1&&['read','arm'].includes(v.mode);}
export function validTouchStatus(v){return validStatus(v,'touch-inspection',validTouchSnapshot);}
export function validRangeStatus(v){return validStatus(v,'touch-range-inspection',validRangeSnapshot);}
function validStatus(v,op,validate){
 if(!v||typeof v!=='object'||Array.isArray(v)||v.v!==1||v.op!==op||!['ok','disabled','busy','unavailable'].includes(v.phase))return false;
 return v.phase==='ok'?Object.keys(v).length===4&&validate(v.snapshot):Object.keys(v).length===3;
}
// Explicit one-shot read, no registration, arbitrary USB command, or automatic retry.
export function inspectTouch(path,{timeoutMs=2000}={}){return inspectDiagnostic(path,timeoutMs,{v:1,op:'touch-inspect',seq:1},validTouchStatus);}
export function inspectTouchRange(path,{timeoutMs=2000,mode='read'}={}){
 if(!['read','arm'].includes(mode))return Promise.resolve(null);
 return inspectDiagnostic(path,timeoutMs,{v:1,op:'touch-range',seq:1,mode},validRangeStatus);
}
function inspectDiagnostic(path,timeoutMs,request,validate){
 if(!Number.isInteger(timeoutMs)||timeoutMs<1||timeoutMs>5000)return Promise.resolve(null);
 try{const parent=lstatSync(dirname(path)),socket=lstatSync(path);if(!parent.isDirectory()||parent.uid!==process.getuid()||(parent.mode&0o777)!==0o700||!socket.isSocket()||socket.uid!==process.getuid()||(socket.mode&0o777)!==0o600)return Promise.resolve(null);}catch{return Promise.resolve(null);}
 return new Promise(resolve=>{
  let socket,timer,settled=false,line='';
  const finish=value=>{if(settled)return;settled=true;clearTimeout(timer);socket?.destroy();resolve(value);};
  try{
   socket=createConnection(path);timer=setTimeout(()=>finish(null),timeoutMs);
   socket.on('error',()=>finish(null));socket.on('end',()=>finish(null));socket.on('close',()=>finish(null));
   socket.on('connect',()=>socket.write(JSON.stringify(request)+'\n'));
   socket.on('data',bytes=>{
    if(bytes.length>2048){finish(null);return;}
    for(const byte of bytes){
     if(byte===10){try{const s=JSON.parse(line);finish(validate(s)?Object.freeze(s):null);}catch{finish(null);}return;}
     if(byte<32||byte>126||line.length>=2048){finish(null);return;}line+=String.fromCharCode(byte);
    }
   });
  }catch{finish(null);}
 });
}
