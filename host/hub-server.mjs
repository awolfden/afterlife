import { createServer } from 'node:net';
import { lstatSync,chmodSync } from 'node:fs';
import { join } from 'node:path';
import { SessionHub } from './session-hub.mjs';
import {inspectionRequest,validHostStatus} from './host-status.mjs';
import {touchInspectionRequest,validTouchStatus,rangeInspectionRequest,validRangeStatus,timingInspectionRequest,validTimingStatus} from './touch-status.mjs';
// Private Unix socket. Metadata remains bounded to 2048 characters.
export async function startHubServer(model,directory,now,{inspect=null,inspectTouch=null,inspectRange=null,inspectTiming=null}={}) {
  if((inspect!==null&&typeof inspect!=='function')||(inspectTouch!==null&&typeof inspectTouch!=='function')||(inspectRange!==null&&typeof inspectRange!=='function')||(inspectTiming!==null&&typeof inspectTiming!=='function'))throw new TypeError('Invalid hub options');
  const stat=lstatSync(directory);
  if(!stat.isDirectory()||stat.uid!==process.getuid()||(stat.mode&0o777)!==0o700)throw new Error('Unsafe hub directory');
  const path=join(directory,'watcher.sock');
  try{lstatSync(path);throw new Error('Hub socket already exists; reconcile previous process first');}catch(error){if(error.code!=='ENOENT')throw error;}
  const hub=new SessionHub(model),clients=new Map();
  const server=createServer(socket=>{
    if(clients.size>=32){socket.destroy();return;}
    const ingress=hub.attach(now());clients.set(socket,ingress);let buffer='',messages=0,inspected=false;
    socket.on('error',()=>socket.destroy());
    socket.on('close',()=>{ingress.disconnect();clients.delete(socket);});
    socket.on('data',bytes=>{
      if(inspected||bytes.length>65536){socket.destroy();return;}
      for(const byte of bytes){
        if(byte===10){
          try{
            if(!buffer){socket.destroy();return;}
            const message=JSON.parse(buffer);
            const first=messages++===0;
            if(first&&inspectionRequest(message)){
              inspected=true;
              const status=inspect?.(message.id);
              if(!validHostStatus(status)){socket.destroy();return;}
              socket.end(JSON.stringify(status)+'\n');return;
            }
            if(first&&(touchInspectionRequest(message)||rangeInspectionRequest(message)||timingInspectionRequest(message))){
              const range=rangeInspectionRequest(message),timing=timingInspectionRequest(message),inspectDiagnostic=range?()=>inspectRange?.(message.mode):timing?inspectTiming:inspectTouch;
              inspected=true;
              const timeout=setTimeout(()=>socket.destroy(),1750);
              socket.once('close',()=>clearTimeout(timeout));
              void Promise.resolve().then(()=>inspectDiagnostic?.()??{phase:'disabled'}).then(result=>{
                const status={v:1,op:range?'touch-range-inspection':timing?'touch-timing-inspection':'touch-inspection',...result};
                if(!(range?validRangeStatus(status):timing?validTimingStatus(status):validTouchStatus(status))){socket.destroy();return;}
                if(!socket.destroyed)socket.end(JSON.stringify(status)+'\n');
              }).catch(()=>socket.destroy()).finally(()=>clearTimeout(timeout));
              return;
            }
            if(!ingress.receive(message,now())){socket.destroy();return;}
          }
          catch{socket.destroy();return;}finally{buffer='';}
          if(ingress.closed){socket.end();return;}
        }else if(byte<32 || byte>126 || buffer.length>=2048){socket.destroy();return;}
        else buffer+=String.fromCharCode(byte);
      }
    });
  });
  await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(path,resolve);});
  chmodSync(path,0o600);
  return Object.freeze({path,
    tick(at){for(const [socket,ingress] of clients){ingress.stale(at);if(ingress.closed)socket.destroy();}},
    close(){for(const socket of clients.keys())socket.destroy();return new Promise((resolve,reject)=>server.close(error=>error?reject(error):resolve()));},
  });
}
