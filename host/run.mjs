// Explicitly launched host daemon. Never auto-starts or opens an unselected badge.
import {mkdirSync} from 'node:fs';
import {homedir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {spawn} from 'node:child_process';
import {performance} from 'node:perf_hooks';
import {AttentionModel} from './attention.mjs';
import {DeliveryLedger} from './delivery-ledger.mjs';
import {DeskSettings} from './desk-settings.mjs';
import {UsbController} from './usb-controller.mjs';
import {audioPathStatus,factoryFeedbackStatus,deviceStatus} from './wire.mjs';
import {MotionMetricsReader} from './motion-metrics.mjs';
import {startHubServer} from './hub-server.mjs';
import {PairedLan,pairing} from './paired-lan.mjs';
import {LanBootstrap} from './lan-bootstrap.mjs';
import {readLanConfig} from './lan-config.mjs';
import {TouchUsbReader} from './touch-usb.mjs';
export async function runHost(options) {
 if(Object.hasOwn(options,'completionChime'))throw new Error('completionChime is retired; use watch Settings Sounds and Vibration');
 const {directory,port,deviceSerial,python,reduced=false,ipcOnly=false,audioInitProbe=false,audioChimeProbe=false,lan=null,lanFactory=options=>new PairedLan(options),touchDiagnostics=false,touchRanges=false,touchTiming=false}=options;
 if(typeof touchTiming!=='boolean'||(touchTiming&&!touchDiagnostics))throw new Error('Touch timing requires explicit USB touch diagnostics');
 if(typeof touchRanges!=='boolean'||(touchRanges&&!touchDiagnostics))throw new Error('Touch ranges requires explicit USB touch diagnostics');
 if(typeof touchDiagnostics!=='boolean'||(touchDiagnostics&&(ipcOnly||audioInitProbe||audioChimeProbe)))throw new Error('Touch diagnostics requires an explicit normal USB bridge');
 if(!ipcOnly && (typeof python!=='string'||(port&&deviceSerial)||!(deviceSerial?/^(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$/.test(deviceSerial):/^\/dev\/cu\.usbmodem[0-9A-Za-z]+$/.test(port??''))))throw new Error('Explicit badge port or USB serial (one only) and isolated Python executable required');
 if(ipcOnly && (port||deviceSerial||lan))throw new Error('IPC-only mode must not specify a device or LAN');
 if(audioChimeProbe && audioInitProbe)throw new Error('Audible probe must run alone');
 if(lan&&(audioInitProbe||audioChimeProbe))throw new Error('LAN cannot be combined with diagnostic probes');
 mkdirSync(directory,{recursive:true,mode:0o700});
 const ledger=new DeliveryLedger(directory),model=new AttentionModel({autoFocus:true});
 let settings,controller;
 try{
  settings=new DeskSettings(directory,ledger);
  controller=new UsbController(model,ledger,{reduced,audioInitProbe,audioChimeProbe,timingDiagnostics:touchTiming,settings:audioInitProbe||audioChimeProbe?null:settings});
 }catch(error){ledger.close();throw error;}
 const epoch=performance.now(),now=()=>Math.floor(performance.now()-epoch);
 let hub,child,timer,retryTimer,closed=false,settle,closing;
 let lanServer=null,lanPeer=null,bootstrap=null,secrets=null,active='usb',selectedAt=0,lastQuery=-1000;
 let audioPath=null,factoryFeedback=null,touchReader=null,linkEpoch=Symbol('device-link');
 const motionReader=touchTiming?new MotionMetricsReader():null;
 const clearDiagnostics=()=>{audioPath=null;factoryFeedback=null;touchReader?.reset();motionReader?.reset();};
 let lastDeviceAt=null,receiving=false,retryAttempt=0,serialRestarts=0,lastSerialExit=null,usbQueries=0;
 const done=new Promise(resolve=>{settle=resolve;});
 const select=source=>{linkEpoch=Symbol('device-link');active=source;selectedAt=now();receiving=false;lastDeviceAt=null;clearDiagnostics();controller.disconnected();lastQuery=-1000;};
 const close=error=>{
  if(closing)return closing;
  touchReader?.reset();
  closed=true;clearInterval(timer);clearTimeout(retryTimer);
  closing=(async()=>{
   let failure=error??null;
   try{if(child){child.stdin.end();child.kill('SIGTERM');}if(hub)await hub.close();}catch(problem){failure??=problem;}
   try{if(lanServer)await lanServer.close();}catch(problem){failure??=problem;}
   bootstrap?.close();secrets?.key.fill(0);
   try{ledger.close();}catch(problem){failure??=problem;}
   settle(failure);
  })();return closing;
 };
 const writeUsb=bytes=>{
  if(!child||child.stdin.destroyed)throw new Error('USB transport unavailable');
  if(child.stdin.writableLength>2048)throw new Error('Serial output stalled');
  child.stdin.write(bytes);
 };
 const writeLan=bytes=>{
  if(!lanPeer||lanPeer.destroyed)return;
  if(lanPeer.writableLength>2048){lanPeer.destroy();return;}
  lanPeer.write(bytes);
 };
 const touchContext=()=>{
  const at=now(),s=controller.status();
  if(closed||active!=='usb'||!child||!receiving||lastDeviceAt===null||at-lastDeviceAt>1500||!controller.readyAt(at)||!s.boot||!s.rendered)return null;
  return {boot:s.boot,seq:s.seq,epoch:linkEpoch};
 };
 if(touchDiagnostics)touchReader=new TouchUsbReader({now,context:touchContext,send:bytes=>{
  if(!child||child.stdin.destroyed||child.stdin.writableLength+Buffer.byteLength(bytes)>2048)return false;
  writeUsb(bytes);return true;
 }});
 const parser=(source,isCurrent)=>{
  let line='',discard=false;
  return bytes=>{
   if(closed||!isCurrent())return;
   try{
    if(bytes.length>65536)throw new Error('Transport input capacity exceeded');
    for(const byte of bytes){
     if(byte===10){
      if(!discard){
       if(source==='usb'&&line==='WATCHER_STORAGE_BLOCKED')throw new Error('Badge storage guard blocked an operation; supervised investigation required');
       // Provisioning is physically USB scoped even while application traffic uses LAN.
       if(source==='usb'&&bootstrap){
        if(line.startsWith('P2 '))bootstrap.usbAck(line);
        else if(line.startsWith('D2 ')){const status=deviceStatus(line);if(status)bootstrap.usbStatus(status,now(),writeUsb);}
       }
       if(source===active&&line.startsWith('D2 ')){
        const at=now(),priorBoot=controller.status().boot;if(controller.observe(line,at)){
         if(priorBoot!==controller.status().boot){touchReader?.reset();motionReader?.reset();linkEpoch=Symbol('device-boot');}
         lastDeviceAt=at;receiving=true;if(source==='usb'){retryAttempt=0;usbQueries=0;}
         if(audioPath&&audioPath.boot!==controller.status().boot)audioPath=null;
         if(factoryFeedback&&factoryFeedback.boot!==controller.status().boot)factoryFeedback=null;
        }
       }else if(source===active&&receiving&&/^S[023] /.test(line)){
        const at=now();if(lastDeviceAt!==null&&at>=lastDeviceAt&&at-lastDeviceAt<=1500)controller.observeSettings(line,at);
       }else if(source===active&&receiving&&line.startsWith('G0 ')){
        const at=now();if(lastDeviceAt!==null&&at>=lastDeviceAt&&at-lastDeviceAt<=1500)controller.observeFamily(line,at);
       }else if(source==='usb'&&active==='usb'&&receiving&&(line.startsWith('TD1 ')||(touchRanges&&line.startsWith('TD2 '))||(touchTiming&&(line.startsWith('TD3 ')||line.startsWith('TD4 '))))){
        touchReader?.receive(line); // Does not renew D2/W1 liveness or mutate delivery/session state.
       }else if(source==='usb'&&active==='usb'&&receiving&&touchTiming&&line.startsWith('R3 ')){
        motionReader.receive(line,controller.status().boot,now()); // Never renews liveness or changes control/session state.
       }else if(source===active&&receiving&&line.startsWith('E1 ')){
        const diagnostic=audioPathStatus(line),current=controller.status();
        if(diagnostic&&diagnostic.boot===current.boot&&diagnostic.seq<=current.seq&&(!audioPath||diagnostic.seq>=audioPath.seq))audioPath=diagnostic;
       }else if(source===active&&receiving&&line.startsWith('F1 ')){
        const diagnostic=factoryFeedbackStatus(line),current=controller.status();
        if(diagnostic&&diagnostic.boot===current.boot&&diagnostic.seq<=current.seq&&(!factoryFeedback||diagnostic.seq>=factoryFeedback.seq))factoryFeedback=diagnostic;
       }
      }
      line='';discard=false;
     }else if(byte===13){}else if(byte<32||byte>126||line.length>=191)discard=true;
     else if(!discard)line+=String.fromCharCode(byte);
    }
   }catch(error){if(source==='lan'){lanPeer?.destroy();}else void close(error);}
  };
 };
 try {
  hub=await startHubServer(model,directory,now,{inspectTouch:()=>touchReader?touchReader.request():{phase:'disabled'},inspectTiming:()=>touchTiming&&touchReader?touchReader.request({version:4}):{phase:'disabled'},inspectRange:mode=>touchRanges&&touchReader?touchReader.request({version:2,mode:mode==='arm'?1:0}):{phase:'disabled'},inspect:id=>{
   const at=now(),fresh=receiving&&lastDeviceAt!==null&&at-lastDeviceAt<=1500,snapshot=model.snapshot();
   return {v:1,op:'inspection',phase:ipcOnly?'ipc-only':!fresh?'waiting-device':controller.readyAt(at)?'confirmed':'handshaking',
    transport:ipcOnly?'none':active,sessions:snapshot.sessions.length,
    session:id===null?'none':!snapshot.sessions.some(s=>s.id===id)?'unwatched':snapshot.selectedSessionId===id?'selected':'other',
    serialRestarts};
  }});
  if(lan){
   secrets=pairing();
   // Validate before listening; errors deliberately contain no input values.
   new LanBootstrap({...lan,port:lan.port||1,...secrets}).close();
   lanServer=lanFactory({address:lan.address,port:lan.port??0,...secrets});
   lanServer.on('transportError',error=>void close(error));
   lanServer.on('peer',peer=>{
    if(closed){peer.destroy();return;}
    lanPeer=peer;peer.on('data',parser('lan',()=>lanPeer===peer));
    peer.on('close',()=>{if(lanPeer!==peer)return;lanPeer=null;if(!closed&&active==='lan')select('usb');});
    if(!receiving){select('lan');writeLan('Q1\n');lastQuery=now();}
   });
   const address=await lanServer.start();
   bootstrap=new LanBootstrap({...lan,port:address.port,...secrets});
  }
  if(closed)throw new Error('Host closed during startup');
  if(!ipcOnly){
   const connectSerial=()=>{
    if(closed)return;
    const transport=spawn(python,[fileURLToPath(new URL('./serial-pipe.py',import.meta.url)),...(deviceSerial?['--serial',deviceSerial]:[port])],{stdio:['pipe','pipe','pipe']});
    child=transport;
    transport.on('error',error=>void close(error));
    transport.stdin.on('error',()=>{if(!closed&&child===transport){if(active==='usb')select(lanPeer?'lan':'usb');transport.kill('SIGTERM');}});
    transport.on('close',(code,signal)=>{
     if(child!==transport)return;
     child=null;if(closed)return;
     if(active==='usb')select(lanPeer?'lan':'usb');
     lastSerialExit={code,signal};serialRestarts++;
     const delay=[500,1000,2000,5000][Math.min(retryAttempt++,3)];retryTimer=setTimeout(connectSerial,delay);
    });
    transport.stderr.on('data',()=>{}); // Never log arbitrary device/helper output.
    transport.stdout.on('data',parser('usb',()=>child===transport));
   };connectSerial();
  }
  timer=setInterval(()=>{
   try {
    const at=now();
    // A TCP/TLS connection and repeated D2 alone are not a two-way W1 proof.
    if(active==='lan'&&!controller.status().ready&&at-selectedAt>3000){lanPeer?.destroy();select('usb');}
    if(receiving&&at-lastDeviceAt>1500){
     if(active==='lan'){lanPeer?.destroy();select('usb');}
     else select(lanPeer?'lan':'usb');
    }
    hub.tick(at);const line=controller.tick(at);
    if(receiving&&controller.status().boot!==null){
     if(active==='usb'&&child)writeUsb(line);else if(active==='lan')writeLan(line);
    }else if(!ipcOnly&&at-lastQuery>=500){
     // A silent open port is not proof of connectivity. Query both transports
     // without resetting, toggling modem controls or replaying application work.
     if(active==='lan'&&lanPeer){writeLan('Q1\n');lastQuery=at;}
     // Keep the total bounded even across helper restarts. Only valid incoming
     // device status rearms probing; a persistently silent link stays quiet.
     else if(active==='usb'&&child&&!child.stdin.destroyed&&usbQueries<6){writeUsb('Q1\n');lastQuery=at;usbQueries++;}
    }
   }catch(error){void close(error);}
  },100);
  return Object.freeze({socket:hub.path,timingStatus:()=>touchTiming?{...controller.pagingDiagnostics(),fresh:!!touchContext(),motion:touchContext()?motionReader.snapshot(controller.status().boot,now()):null}:null,status:()=>({...controller.status(),audioPath,factoryFeedback,receiving,serialRestarts,lastSerialExit,transport:active,lan:bootstrap?{...bootstrap.status(),connected:!!lanPeer&&!lanPeer.destroyed,live:active==='lan'&&receiving&&controller.status().ready,deviceFault:!!(controller.status().capabilities&64),deviceHasIp:!!(controller.status().capabilities&128)}:null}),snapshot:()=>model.snapshot(),close:()=>close(),done});
 }catch(error){await close(error);throw error;}
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
 const args=process.argv.slice(2),options={directory:join(homedir(),'.local/share/pi-watcher-pet/run')};
 try{
  for(let i=0;i<args.length;i++){
   if(args[i]==='--ipc-only')options.ipcOnly=true;
   else if(args[i]==='--reduced-motion')options.reduced=true;
   else if(args[i]==='--touch-diagnostics')options.touchDiagnostics=true;
   else if(args[i]==='--touch-range-diagnostics')options.touchRanges=true;
   else if(args[i]==='--touch-timing-diagnostics')options.touchTiming=true;
   else if(args[i]==='--completion-chime')throw new Error('--completion-chime is retired; use watch Settings Sounds and Vibration');
   else if(args[i]==='--audio-init-probe')options.audioInitProbe=true;
   else if(args[i]==='--audio-chime-probe')options.audioChimeProbe=true;
   else if(['--state-dir','--port','--device-serial','--python','--lan-config'].includes(args[i])){
    const key={'--state-dir':'directory','--port':'port','--device-serial':'deviceSerial','--python':'python','--lan-config':'lanPath'}[args[i]];
    if(!args[i+1]||args[i+1].startsWith('--'))throw new Error(`Missing ${args[i]} value`);options[key]=args[++i];
   }else throw new Error('Unknown option; use --ipc-only or --python with --port or --device-serial; optional --lan-config');
  }
  if(options.lanPath)options.lan=readLanConfig(options.lanPath);
  const host=await runHost(options);console.log(`Watcher host: ${host.socket}${options.ipcOnly?' (IPC only; no device access)':''}`);
  for(const signal of ['SIGINT','SIGTERM'])process.once(signal,()=>void host.close());
  const error=await host.done;if(error)throw error;
 }catch(error){console.error(`Watcher stopped: ${error.message}`);process.exitCode=1;}
}
