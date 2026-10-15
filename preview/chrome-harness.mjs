// Local validation/export utility, not served by the preview HTTP server.
import { spawn } from 'node:child_process';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
export async function withPreviewChrome(url, action, timeoutMs=60000) {
  if (!/^http:\/\/127\.0\.0\.1:\d+\/?$/.test(url??'')) throw new Error('Local preview URL required');
  const dir=await mkdtemp(join(tmpdir(),'pi-watcher-chrome-'));
  const chrome=spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',[
    '--headless=new',`--user-data-dir=${join(dir,'profile')}`,'--remote-debugging-port=0','--remote-debugging-address=127.0.0.1',
    '--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','about:blank',
  ],{stdio:['ignore','ignore','pipe']});
  let socket,timer;
  const pending=new Map(),events=new Map();let seq=0;
  const timeout=new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('Isolated Chrome deadline exceeded')),timeoutMs);});
  async function run() {
    const endpoint=await new Promise((resolve,reject)=>{
      let output='';chrome.once('error',reject);chrome.once('exit',()=>reject(new Error('Chrome exited before ready')));
      chrome.stderr.on('data',chunk=>{output=(output+chunk).slice(-16000);const match=output.match(/DevTools listening on (ws:\/\/127\.0\.0\.1:\d+\/\S+)/);if(match)resolve(match[1]);});
    });
    socket=new WebSocket(endpoint);
    await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
    socket.addEventListener('message',event=>{
      const data=JSON.parse(event.data);
      if(data.id){const entry=pending.get(data.id);pending.delete(data.id);if(entry)(data.error?entry.reject(new Error(JSON.stringify(data.error))):entry.resolve(data.result));}
      else if(events.has(data.method)){events.get(data.method)(data);events.delete(data.method);}
    });
    socket.addEventListener('close',()=>{for(const entry of pending.values())entry.reject(new Error('Chrome disconnected'));pending.clear();});
    const call=(method,params={},sessionId)=>new Promise((resolve,reject)=>{const id=++seq;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})}));});
    const {targetId}=await call('Target.createTarget',{url:'about:blank'});
    const {sessionId}=await call('Target.attachToTarget',{targetId,flatten:true});
    const page=(method,params={})=>call(method,params,sessionId);
    await page('Page.enable');await page('Runtime.enable');
    await page('Emulation.setDeviceMetricsOverride',{width:1100,height:1000,deviceScaleFactor:1,mobile:false});
    const loaded=new Promise(resolve=>events.set('Page.loadEventFired',resolve));
    await page('Page.navigate',{url});await loaded;
    const result=await action({page,dir});
    await call('Browser.close');return result;
  }
  try { return await Promise.race([run(),timeout]); }
  finally {clearTimeout(timer);socket?.close();chrome.kill('SIGTERM');}
}
