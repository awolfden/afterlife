// Private IPC child. No credential arguments, environment variables, files or logs.
import {pathToFileURL} from 'node:url';
import {runHost} from './run.mjs';
export function serveOwner(channel,startHost=runHost,exit=code=>process.exit(code)){
 let host=null,starting=null,stopping=null,stopped=false,started=false;
 const deadline=setTimeout(()=>void stop(),5000);
 function stop(){
  if(stopping)return stopping;
  stopped=true;clearTimeout(deadline);
  stopping=(async()=>{try{await starting;await host?.close();}catch{}finally{exit(0);}})();
  return stopping;
 }
 function send(message){
  if(!channel.connected){void stop();return;}
  try{channel.send(message,error=>{if(error)void stop();});}catch{void stop();}
 }
 function publish(request){
  if(stopped||!host)return;
  const status=host.status(),lan=status.lan;
  const value={paired:!!lan?.paired,deviceHasIp:!!lan?.deviceHasIp,connected:!!lan?.connected,live:!!lan?.live,deviceFault:!!lan?.deviceFault,provisionError:!!lan?.error};
  send({type:'link',request,value});
 }
 channel.once('disconnect',()=>void stop());
 channel.once('SIGTERM',()=>void stop());
 channel.once('SIGINT',()=>void stop());
 channel.on('message',message=>{
  if(stopped)return;
  if(message?.type==='inspect'&&Number.isSafeInteger(message.request)&&message.request>0&&host){
   try{publish(message.request);}catch{void stop();}return;
  }
  if(started||message?.type!=='start'){void stop();return;}
  started=true;clearTimeout(deadline);
  starting=(async()=>{
   try{
    const options=message.options;
    if(!options||Object.keys(options).sort().join(',')!=='directory,lan,port,python'||
       !options.lan||Object.keys(options.lan).sort().join(',')!=='address,password,port,ssid')throw Error('Invalid options');
    // No opt-in voice, diagnostics, factory probes or reduced safeguards here.
    host=await startHost(options);
    if(stopped)return;
    send({type:'ready'});publish();
    void host.done.then(()=>stop(),()=>stop());
   }catch{send({type:'failed'});queueMicrotask(()=>void stop());}
   finally{message.options=null;}
  })();
 });
 if(!channel.connected)void stop();
 return {close:stop};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href)serveOwner(process);
