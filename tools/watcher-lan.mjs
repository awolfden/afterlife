// Foreground LAN host. Credential JSON arrives only through a bounded stdin
// pipe from watcher-lan.py. No credential files, arbitrary diagnostics or flash.
import {runHost} from '../host/run.mjs';
import {homedir} from 'node:os';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
export async function readLanInput(stream,{timeoutMs=5000}={}){
 if(!Number.isInteger(timeoutMs)||timeoutMs<1||timeoutMs>5000)throw Error('Invalid input deadline');
 const chunks=[];let size=0,raw;
 const timer=setTimeout(()=>stream.destroy(Error('Input deadline')),timeoutMs);
 try{
  for await(const chunk of stream){
   if(!Buffer.isBuffer(chunk))throw Error('Binary input required');
   chunks.push(chunk);size+=chunk.length;if(size>1024)throw Error('Input bound');
  }
  raw=Buffer.concat(chunks);const value=JSON.parse(raw.toString('utf8'));
  if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).sort().join(',')!=='address,password,port,ssid')throw Error('Invalid fields');
  return value; // Existing LanBootstrap validates values before listening.
 }finally{clearTimeout(timer);raw?.fill(0);for(const chunk of chunks)chunk.fill(0);}
}
export function lanOptions(args){
 const options={directory:join(homedir(),'.local/share/pi-watcher-pet/run')};
 const keys={'--state-dir':'directory','--port':'port','--device-serial':'deviceSerial','--python':'python'};
 for(let i=0;i<args.length;i++){
  const key=Object.hasOwn(keys,args[i])?keys[args[i]]:null;
  if(!key||Object.hasOwn(options,key)&&key!=='directory'||!args[i+1]||args[i+1].startsWith('--'))throw Error('Invalid launcher options');
  options[key]=args[++i];
 }
 if(!options.python||!!options.port===!!options.deviceSerial)throw Error('Explicit Python and one USB selector required');
 return options;
}
async function main(){
 let host,timer;
 try{
  const options=lanOptions(process.argv.slice(2));let lan=await readLanInput(process.stdin);
  host=await runHost({...options,lan});lan=null;
  for(const signal of ['SIGINT','SIGTERM'])process.once(signal,()=>void host.close());
  console.log('LAN watcher running. Keep USB connected for RAM provisioning.');
  let previous='';const report=()=>{
   const s=host.status(),l=s.lan;
   const value=JSON.stringify({ramPaired:!!l?.paired,wifiHasIp:!!l?.deviceHasIp,tlsPeer:!!l?.connected,transport:s.transport,lanLive:!!l?.live,deviceFault:!!l?.deviceFault,provisionError:!!l?.error});
   if(value!==previous){previous=value;console.log(value);}
  };
  report();timer=setInterval(report,1000);if(await host.done)throw Error('Host stopped');
 }catch{console.error('LAN host unavailable. Check host ownership, USB selection, local IP and Wi-Fi input; do not share passwords or packet dumps.');process.exitCode=1;}
 finally{clearInterval(timer);if(host)await host.close();}
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href)await main();
