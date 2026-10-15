// Explicit bounded USB-only diagnostic host. No credentials/adoption/reset,
// firmware writes, synthetic gestures, motor tests or session-content logging.
import {runHost} from '../host/run.mjs';
import {inspectTouchTiming} from '../host/touch-status.mjs';
import {validFullTimingSnapshot} from '../host/touch-timing.mjs';
import {motionMetricKeys,validMotionMetrics} from '../host/motion-metrics.mjs';
import {mkdir,lstat,open} from 'node:fs/promises';
import {dirname,join} from 'node:path';
import {homedir} from 'node:os';
import {pathToFileURL} from 'node:url';
import {performance} from 'node:perf_hooks';
export const phases=Object.freeze([
 {name:'baseline',ms:15000,instruction:'Hands off: leave the badge idle.'},
 {name:'isolated',ms:20000,instruction:'Swipe once every 3 seconds; fully lift your finger each time.'},
 {name:'rapid',ms:20000,instruction:'Alternate left/right as each ghost settles; fully lift between swipes.'},
 {name:'reversal',ms:20000,instruction:'Drag partway, reverse home, then lift. Alternate with a completed swipe.'},
 {name:'wrap',ms:20000,instruction:'Swipe repeatedly through wrap in one direction, then the other; fully lift each time.'},
 {name:'recovery',ms:15000,instruction:'Hands off again: observe whether the display remains alive.'},
]);
export const coveragePolicy=Object.freeze({querySpacingMs:1100,maxGapMs:5000,boundaryWindowMs:3000,minGuidedMs:15000,windowMs:110000,maxRecords:128,maxBytes:512*1024});
const pagingKeys=['observed','applied','noChange','unknownFrame','selectedMismatch','selectionEpoch','rosterEpoch','rosterIdentity'];
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const u32=n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff;
const spans=[];let edge=0;for(const p of phases){spans.push({...p,start:edge,end:edge+p.ms});edge+=p.ms;}
class CaptureStop extends Error {constructor(reason){super(reason);this.reason=reason;}}
const stop=reason=>{throw new CaptureStop(reason);};
export function assessCoverage(records,prompts=[]){
 const good=records.filter(r=>r.usable),issues=[];
 const perPhase=spans.map(p=>({name:p.name,successes:good.filter(r=>r.requestStartMs>=p.start&&r.replyEndMs<=p.end).length,unavailable:records.filter(r=>!r.usable&&r.requestStartMs<p.end&&r.replyEndMs>=p.start).length}));
 for(const p of perPhase)if(!p.successes)issues.push(`missing-phase:${p.name}`);
 const boundaries=spans.slice(1).map(p=>({atMs:p.start,before:good.some(r=>r.replyEndMs<=p.start&&r.replyEndMs>=p.start-coveragePolicy.boundaryWindowMs),after:good.some(r=>r.requestStartMs>=p.start&&r.replyEndMs<=p.start+coveragePolicy.boundaryWindowMs)}));
 for(const b of boundaries)if(!b.before||!b.after)issues.push(`boundary-gap:${b.atMs}`);
 const outages=[];let outage=null,previousGood=0;
 for(const r of records){
  if(!r.usable){if(!outage)outage={lastGoodEndMs:previousGood,firstUnavailableRequestMs:r.requestStartMs,endMs:r.replyEndMs,recovered:false,count:0};outage.endMs=r.replyEndMs;outage.count++;}
  else{if(outage){outages.push({...outage,endMs:r.replyEndMs,recovered:true});outage=null;}previousGood=r.replyEndMs;}
 }
 if(outage)outages.push(outage);
 let last=0,longestGapMs=0;const gaps=[];
 for(const r of good){const n=r.replyEndMs-last;longestGapMs=Math.max(longestGapMs,n);if(n>coveragePolicy.maxGapMs)gaps.push({startMs:last,endMs:r.replyEndMs,recovered:true});last=r.replyEndMs;}
 const end=Math.max(coveragePolicy.windowMs,records.at(-1)?.replyEndMs??0),tail=end-last;longestGapMs=Math.max(longestGapMs,tail);
 if(tail>coveragePolicy.maxGapMs)gaps.push({startMs:last,endMs:end,recovered:false});
 if(gaps.length)issues.push('evidence-gap');
 const final=good.some(r=>r.final&&r.requestStartMs>=coveragePolicy.windowMs);if(!final)issues.push('incomplete-tail');
 if(records.some(r=>r.change))issues.push('generation-or-health-change');
 const telemetry={adequate:issues.length===0,issues,perPhase,boundaries,longestGapMs,gaps,outages,final};
 // Valid fault telemetry remains useful, but it is not proof that exercises
 // suppressed by HOLD were offered. Never require continuous touch eligibility.
 const captureEnd=Math.max(end,prompts.at(-1)?.elapsedMs??0);
 const instructions=prompts.map((p,i)=>({...p,endMs:prompts[i+1]?.elapsedMs??captureEnd}));
 const holds=instructions.filter(p=>p.name==='hold').map(p=>({startMs:p.elapsedMs,endMs:p.endMs,reasons:[...new Set([...(p.reasons??[]),...records.filter(r=>r.replyEndMs>=p.elapsedMs&&r.requestStartMs<=p.endMs).flatMap(r=>r.healthReasons??[])])]}));
 const healthIntervals=[];let unhealthy=null,lastReady=0;
 for(const r of records){
  if(!r.interactionReady){
   if(!unhealthy)unhealthy={lastReadyEndMs:lastReady,firstObservedMs:r.replyEndMs,endMs:r.replyEndMs,recovered:false,reasons:[]};
   unhealthy.endMs=r.replyEndMs;unhealthy.reasons=[...new Set([...unhealthy.reasons,...(r.healthReasons??[])])];
  }else{if(unhealthy){healthIntervals.push({...unhealthy,endMs:r.replyEndMs,recovered:true});unhealthy=null;}lastReady=r.replyEndMs;}
 }
 if(unhealthy)healthIntervals.push(unhealthy);
 const guidedPhases=spans.slice(1,-1).map(p=>{
  const intervals=instructions.filter(i=>i.name===p.name).map(i=>({startMs:Math.max(p.start,i.elapsedMs),endMs:Math.min(p.end,i.endMs)})).filter(i=>i.endMs>i.startMs);
  const instructedMs=intervals.reduce((sum,i)=>sum+i.endMs-i.startMs,0);
  const successes=records.filter(r=>r.interactionReady&&intervals.some(i=>r.requestStartMs>=i.startMs&&r.replyEndMs<=i.endMs)).length;
  return {name:p.name,instructedMs,successes,intervals,adequate:instructedMs>=coveragePolicy.minGuidedMs&&successes>0};
 });
 const guidedIssues=guidedPhases.filter(p=>!p.adequate).map(p=>`incomplete-guided-phase:${p.name}`);
 const finalReady=records.some(r=>r.final&&r.requestStartMs>=coveragePolicy.windowMs&&r.interactionReady);
 if(!finalReady)guidedIssues.push('final-not-interaction-ready');
 if(records.some(r=>r.result?.phase==='ok'&&r.result.snapshot?.values?.[95]))guidedIssues.push('firmware-clock-fault');
 const guided={adequate:guidedIssues.length===0,issues:guidedIssues,perPhase:guidedPhases,holds,healthIntervals,finalReady,ownerPerformance:'not-assessed'};
 return {...telemetry,adequate:telemetry.adequate&&guided.adequate,issues:[...issues,...guidedIssues],telemetry,guided};
}
export async function captureSwipeTiming({directory=join(homedir(),'.local/share/pi-watcher-pet/run'),port,python,output,expectedBoot,signal,onPhase=()=>{}},{startHost=runHost,timing=inspectTouchTiming,now=()=>performance.now(),wait=sleep}={}){
 if(typeof port!=='string'||!/^\/dev\/cu\.usbmodem[0-9]+$/.test(port)||typeof python!=='string'||!python.startsWith('/')||typeof output!=='string'||!output.startsWith('/')||!Number.isInteger(expectedBoot)||expectedBoot<1||expectedBoot>0xffffffff)throw new Error('Explicit port/python/private output/expected boot required');
 const parent=await lstat(dirname(output));if(!parent.isDirectory()||parent.isSymbolicLink()||parent.uid!==process.getuid()||(parent.mode&0o777)!==0o700)throw new Error('Output parent must be owned private directory');
 await mkdir(output,{mode:0o700}); // Existing evidence rejected, never overwritten.
 let file,host,watched,ended=false,bytes=0,emitStop=()=>{};
 const records=[],result={outcome:'failed',lifecycle:'failed',reason:'setup',records:0,timingOk:0,unavailable:0,expectedBoot,symptomReproduced:'not-assessed',policy:coveragePolicy,prompts:[]};
 try{
  file=await open(join(output,'samples.jsonl'),'wx',0o600);
  if(signal?.aborted)stop('aborted');
  host=await startHost({directory,port,python,touchDiagnostics:true,touchTiming:true});
  watched=host.done.then(()=>{ended=true;},()=>{ended=true;});
  let last=now();const clock=()=>{const n=now();if(!Number.isFinite(n)||n<last)stop('clock');last=n;return n;};
  const observe=()=>{
   if(signal?.aborted)stop('aborted');if(ended)stop('host-ended');
   const s=host.status(),d=host.timingStatus?.();
   if(!d||typeof d.fresh!=='boolean'||![...pagingKeys,'rosterGeneration','linkGeneration'].every(k=>u32(d[k])))stop('anonymous-diagnostics-unavailable');
   const fields=d.motion&&Object.fromEntries(motionMetricKeys.map(k=>[k,d.motion[k]]));
   const motion=validMotionMetrics(fields)&&fields.boot===s.boot&&u32(d.motion.ageMs)&&d.motion.ageMs<=10000?{...fields,ageMs:d.motion.ageMs}:null;
   return {ready:s.ready===true,fresh:d.fresh,boot:s.boot,transport:s.transport==='usb'?'usb':'other',fault:!!s.fault,serialRestarts:s.serialRestarts,sessions:host.snapshot().sessions.length,rosterGeneration:d.rosterGeneration,linkGeneration:d.linkGeneration,paging:Object.fromEntries(pagingKeys.map(k=>[k,d[k]])),motion};
  };
  const failure=(o,pin)=>o.fault||o.serialRestarts?'host-fault-or-restart':o.boot!==null&&o.boot!==expectedBoot?'unexpected-device-boot':o.transport!=='usb'?'unexpected-transport':pin&&o.sessions!==3?'roster-changed':pin&&o.rosterGeneration!==pin.rosterGeneration?'roster-generation-changed':pin&&o.linkGeneration!==pin.linkGeneration?'link-generation-changed':null;
  const started=clock();let pin;
  while(true){const o=observe(),bad=failure(o);if(bad)stop(bad);if(o.fresh&&o.ready&&o.sessions===3){pin=o;break;}if(clock()-started>=30000)stop('three-session-readiness-timeout');await wait(100);}
  const began=clock();let prompted=null,baseline=false,healthy=false;
  const prompt=(name,instruction,ms=0,reasons=[])=>{
   let elapsedMs;
   try{elapsedMs=Math.floor(clock()-began);}catch(error){if(name!=='stopped')throw error;elapsedMs=Math.max(0,Math.floor(last-began));}
   const p={name,instruction,ms,elapsedMs,reasons};result.prompts.push(p);onPhase(p);prompted=name;
  };
  emitStop=reason=>prompt('stopped','STOP touching. Capture ended; do not continue the exercises.',0,[reason]);
  prompt('baseline',phases[0].instruction,phases[0].ms);
  const sample=async(final=false)=>{
   const before=observe(),requestStartMs=Math.floor(clock()-began);
   const raw=await timing(host.socket,{timeoutMs:2000});
   const replyEndMs=Math.floor(clock()-began),after=observe();
   const reply=raw?.phase==='ok'&&validFullTimingSnapshot(raw.snapshot)?{phase:'ok',snapshot:raw.snapshot}:{phase:raw?.phase==='disabled'?'disabled':'unavailable'};
   const change=failure(before,pin)||failure(after,pin)||(reply.phase==='ok'&&reply.snapshot.boot!==expectedBoot?'unexpected-snapshot-boot':null);
   const usable=reply.phase==='ok'&&!change&&before.fresh&&after.fresh&&before.ready&&after.ready;
   const healthReasons=[];
   if(reply.phase!=='ok')healthReasons.push('telemetry-unavailable');
   if(!before.fresh||!after.fresh||!before.ready||!after.ready)healthReasons.push('host-unfresh');
   if(change)healthReasons.push(change);
   if(reply.phase==='ok'){
    if(!(reply.snapshot.values[17]&8))healthReasons.push('sampler-unready');
    if(reply.snapshot.values[17]&16)healthReasons.push('sampler-halted');
    if(reply.snapshot.values[95])healthReasons.push('firmware-clock-fault');
   }
   const interactionReady=usable&&healthReasons.length===0;
   const record={requestStartMs,replyEndMs,requestMs:replyEndMs-requestStartMs,phases:spans.filter(p=>requestStartMs<p.end&&replyEndMs>=p.start).map(p=>p.name),final,before,after,change,usable,interactionReady,healthReasons,result:reply};
   const line=JSON.stringify(record)+'\n';bytes+=Buffer.byteLength(line);
   if(bytes>coveragePolicy.maxBytes||records.length>=coveragePolicy.maxRecords)stop('capture-cap');
   await file.write(line);records.push(record);result.records++;
   if(usable)result.timingOk++;else result.unavailable++;
   if(change)stop(change);if(reply.phase==='disabled')stop('diagnostic-disabled');
   healthy=interactionReady;
   if(healthy)baseline=true;
   if(!healthy&&prompted!=='hold')prompt('hold','STOP touching. Measurement continuity or sampler readiness is uncertain; wait for a fresh phase prompt.',0,healthReasons);
   return record;
  };
  while(clock()-began<coveragePolicy.windowMs){
   const elapsed=clock()-began,p=spans.find(p=>elapsed>=p.start&&elapsed<p.end),live=observe();
   if(!live.fresh||!live.ready||failure(live,pin)){healthy=false;if(prompted!=='hold')prompt('hold','STOP touching. Current link/roster readiness is uncertain.',0,[failure(live,pin)??'host-unfresh']);}
   if(elapsed>=15000&&!baseline)stop('no-valid-ready-baseline');
   if(p&&p.name!==prompted&&healthy&&baseline)prompt(p.name,p.instruction,Math.max(0,Math.floor(p.end-elapsed)));
   await sample();
   await wait(coveragePolicy.querySpacingMs); // Explicit sampling, no catch-up/retry burst.
  }
  prompt('finished','Hands off. Taking one final snapshot; note which phases had missed swipes.');
  await sample(true); // Exactly one bounded final attempt, even if unavailable.
  result.lifecycle='completed';result.reason='bounded-window-finished';
 }catch(error){result.reason=error instanceof CaptureStop?error.reason:'capture-error';try{emitStop(result.reason);}catch{ /* Preserve the failure even if the prompt callback fails. */ }}
 finally{
  try{if(host)await host.close();if(watched)await watched;}catch{result.lifecycle='failed';result.reason='host-close-failed';try{emitStop(result.reason);}catch{}}
  result.coverage=assessCoverage(records,result.prompts);
  result.outcome=result.lifecycle==='completed'?(result.coverage.adequate?'completed':'incomplete'):'failed';
  if(file){await file.sync();await file.close();}
  const summary=await open(join(output,'summary.json'),'wx',0o600);try{await summary.writeFile(JSON.stringify(result,null,2)+'\n');}finally{await summary.close();}
 }
 return result;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
 const options={},allowed=new Set(['port','python','output','boot']);
 try{
  for(let i=2;i<process.argv.length;i+=2){const key=process.argv[i].slice(2);if(!process.argv[i].startsWith('--')||!allowed.has(key)||Object.hasOwn(options,key)||!process.argv[i+1])throw Error('arguments');options[key]=process.argv[i+1];}
  const abort=new AbortController();process.once('SIGINT',()=>abort.abort());process.once('SIGTERM',()=>abort.abort());
  console.log('Bounded USB-only capture. Existing server must be stopped; three sessions must reconnect. Note phase/time of missed swipes. STOP touching whenever HOLD appears.');
  const result=await captureSwipeTiming({...options,expectedBoot:Number(options.boot),signal:abort.signal,onPhase:p=>console.log(`\n[${p.elapsedMs/1000}s] ${p.name.toUpperCase()} — ${p.ms/1000}s: ${p.instruction}`)});
  console.log(JSON.stringify(result,null,2));console.log(result.reason==='host-close-failed'?'Host close failed. DO NOT start another bridge; preserve ownership for inspection.':'Capture ended. Coverage is separate from symptom reproduction.');process.exitCode=result.outcome==='completed'?0:1;
 }catch{console.error('Capture unavailable. Check arguments, private output and server ownership; no automatic takeover/retry.');process.exitCode=1;}
}
