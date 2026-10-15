import {join} from 'node:path';
import {OwnedServer,serverDirectory,serverLockPresent,discoverServerSetup} from './server-process.mjs';
import {inspectHost,describeHostStatus} from './host-status.mjs';
import {promptSecret} from './server-secret.mjs';
const ssidValid=value=>typeof value==='string'&&value.length>0&&Buffer.byteLength(value)<=32&&!/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}]/u.test(value);
const passwordValid=value=>typeof value==='string'&&/^[\x20-\x7e]{8,63}$/.test(value);
const notify=(ctx,text,level='info')=>{try{ctx.ui.notify(text,level);}catch{}};
export function createServerCommand(pi,{server=new OwnedServer(),directory=serverDirectory(),locked=()=>serverLockPresent(directory),discover=discoverServerSetup,secret=promptSecret,inspect=()=>inspectHost(join(directory,'watcher.sock')),isChild=process.env.PI_SUBAGENT_CHILD==='1'}={}){
 if(isChild)return async()=>{};
 let closed=false,busy=false,abort=null;
 pi.on('session_shutdown',async()=>{closed=true;abort?.abort();await server.close();});
 async function status(ctx){
  const observation=await inspect();
  if(closed)return;
  const ownership=server.running?'Owned by this Pi session':await locked()?'Existing server/lock (not owned by this Pi)':'No server owned or locked';
  const link=server.running?await server.inspectLink():null;
  if(closed)return;
  let text=`${ownership}. ${observation?describeHostStatus(observation):'Device delivery status unavailable'}.`;
  if(link){
   if(link.deviceFault||link.provisionError)text+=' Provisioning fault. Keep USB connected and investigate before retrying.';
   else if(link.live&&observation?.phase==='confirmed'&&observation.transport==='lan')text+=' Watch connected wirelessly.';
   else if(link.paired&&link.deviceHasIp&&link.connected&&observation?.phase==='confirmed')text+=' RAM pairing, Wi-Fi IP and TLS peer ready. You can unplug USB, then check status for wireless delivery.';
   else text+=` Keep USB connected. RAM pairing: ${link.paired?'ready':'waiting'}; Wi-Fi IP: ${link.deviceHasIp?'ready':'waiting'}; TLS peer: ${link.connected?'ready':'waiting'}.`;
  }
  notify(ctx,text);
 }
 return async(args,ctx)=>{
  if(closed)return;
  if(ctx.mode!=='tui'){notify(ctx,'Use /watch server in an interactive Pi terminal.','warning');return;}
  if(busy){notify(ctx,'A server command is already in progress. Finish or cancel its prompt.','warning');return;}
  const requested=args.trim();
  if(requested&&!['start','stop','status'].includes(requested)){notify(ctx,'Usage: /watch server [start|stop|status]. Enter Wi-Fi details only in the hidden prompts.','warning');return;}
  busy=true;abort=new AbortController();const signal=abort.signal;
  const current=()=>!closed&&!signal.aborted;
  let ssid,password;
  try{
   let action=requested;
   if(!action){const choice=await ctx.ui.select('Watch server',['Start wireless','Status','Stop'],{signal});action={'Start wireless':'start',Status:'status',Stop:'stop'}[choice];}
   if(!action||!current())return;
   if(action==='status'){await status(ctx);return;}
   if(action==='stop'){
    if(!server.running){notify(ctx,'This Pi does not own a server. Stop an existing server in the Terminal or Pi session that launched it.');return;}
    if(!await ctx.ui.confirm('Stop watch server?','All connected Pi sessions will lose watch delivery. Restarting requires USB and Wi-Fi entry again.',{signal})||!current())return;
    await server.close();if(current())notify(ctx,'Watch server stopped.');return;
   }
   if(server.running){await status(ctx);return;}
   if(await locked()){
    if(current())notify(ctx,'An existing server or lock is present. No duplicate was started. Stop the server in its original Terminal/Pi before starting here; locks are never removed automatically.','warning');return;
   }
   if(!current())return;
   const setup=await discover();if(!current())return;
   if(!setup.ports.length){notify(ctx,'Connect the watch over USB, then run /watch server start again.','warning');return;}
   if(!setup.addresses.length){notify(ctx,'No private local IPv4 address found. Connect your Mac to the watch\'s network first.','warning');return;}
   const port=setup.ports.length===1?setup.ports[0]:await ctx.ui.select('Choose the watch USB port',setup.ports,{signal});
   if(!port||!current())return;
   const address=setup.addresses.length===1?setup.addresses[0]:await ctx.ui.select('Choose this Mac\'s address on the watch network',setup.addresses,{signal});
   if(!address||!current())return;
   if(!await ctx.ui.confirm('Start wireless watch server?',`USB: ${port}\nMac address: ${address}\nStops on Pi exit, reload or session replacement. Other sessions also lose delivery. Wi-Fi details stay in RAM; observation is metadata-only.`,{signal})||!current())return;
   ssid=await secret(ctx,'Wi-Fi network name (hidden)',{maxBytes:32,valid:ssidValid,signal});
   if(!current()||ssid===undefined)return;
   if(!ssidValid(ssid)){notify(ctx,'Invalid Wi-Fi network name. Nothing started.','warning');return;}
   password=await secret(ctx,'Wi-Fi password (hidden, 8-63 ASCII characters)',{maxBytes:63,valid:passwordValid,signal});
   if(!current()||password===undefined)return;
   if(!passwordValid(password)){notify(ctx,'Invalid Wi-Fi password format. Nothing started.','warning');return;}
   // Recheck after prompts. runHost's exclusive delivery lock is the final race gate.
   if(await locked()){if(current())notify(ctx,'Another server or lock appeared. Nothing started.','warning');return;}
   if(!current())return;
   await server.start({directory,python:setup.python,port,lan:{ssid,password,address,port:0}});
   if(!current()){await server.close();return;}
   notify(ctx,'Watch server started, not yet confirmed wirelessly. Keep USB connected; /watch server status shows when you can unplug. Use /watch to opt this session into watching.');
  }catch{if(current())notify(ctx,'Server command unavailable. Check the installed Python/pyserial toolchain, USB, local address and host ownership. No automatic takeover or retry.','error');}
  finally{ssid=undefined;password=undefined;abort=null;busy=false;}
 };
}
