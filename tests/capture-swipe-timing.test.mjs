import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,readFileSync,statSync,chmodSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {captureSwipeTiming,phases} from '../tools/capture-swipe-timing.mjs';
import {motionMetricKeys} from '../host/motion-metrics.mjs';
// Synthetic numeric USB pathname; injected startHost never opens this device.
function setup(t,mode='ok'){
 const root=mkdtempSync(join(tmpdir(),'timing-capture-'));t.after(()=>rmSync(root,{recursive:true,force:true}));
 let at=0,closed=0,started=0,queries=0,resolve,changed=false;
 const done=new Promise(r=>{resolve=r;}),seen=[];
 const values=new Array(192).fill(0);values[17]=8;
 const snapshot={format:4,boot:123,seq:42,rendered:40,values};
 const motion={...Object.fromEntries(motionMetricKeys.map(k=>[k,k==='fault'?0:k==='boot'?(mode==='motion-wrong'?124:123):1])),ageMs:mode==='motion-stale'?10001:10,secret:'do-not-log'};
 const host={done,socket:'fake',status:()=>({boot:mode==='boot'||(mode==='boot-during'&&changed)?124:123,ready:true,serialRestarts:mode==='restart'?1:0,fault:null,transport:mode==='transport'?'lan':'usb',secret:'do-not-log'}),timingStatus:()=>mode==='diagnostics'?null:({fresh:!(mode==='stale'&&at>2000),linkGeneration:mode==='link'&&at>2000?2:1,rosterGeneration:(mode==='same-count'&&at>2000)||(mode==='roster-during'&&changed)?4:3,observed:0,applied:0,noChange:0,unknownFrame:0,selectedMismatch:0,selectionEpoch:0,rosterEpoch:0,rosterIdentity:0,motion:mode.startsWith('motion')?motion:null,secret:'do-not-log'}),snapshot:()=>({sessions:Array.from({length:mode==='missing'||(mode==='roster'&&at>2000)?2:3},()=>({content:'do-not-log'}))}),close:async()=>{closed++;resolve();}};
 const dependencies={startHost:async options=>{started++;assert.deepEqual(options,{directory:'fake',port:'/dev/cu.usbmodem00000',python:'/fake/python',touchDiagnostics:true,touchTiming:true});if(mode==='start')throw Error('private-error-do-not-log');return host;},now:()=>mode==='clock'&&at>2000?at-5000:at,wait:async ms=>{assert.ok(ms===100||ms===1100);at+=mode==='cap'?1:ms;},timing:async()=>{
  queries++;if(mode==='slow')at+=1700;if(mode==='roster-during'||mode==='boot-during')changed=true;
  if(mode==='disabled')return {phase:'disabled'};
  if(mode==='unavailable'||(mode==='one'&&queries>1)||(mode==='brief'&&at>=18000&&at<=20500)||(mode==='final'&&at>=110000))return {phase:'unavailable'};
  const observed=values.slice();
  if(mode==='unready-baseline'||(mode==='unready'&&queries>1)||(mode==='unready-phase'&&at>=15000&&at<35000)||(mode==='health-recovery'&&at>=18000&&at<=20500))observed[17]=0;
  if((mode==='halted'&&queries>1)||(mode==='final-unhealthy'&&at>=110000))observed[17]=16;
  if(mode==='firmware-clock'&&queries>1)observed[95]=1;
  observed[160]=observed[17]&16?9:observed[17]&8?4:0;
  return {phase:'ok',snapshot:{...snapshot,values:observed}};
 }};
 const options={directory:'fake',port:'/dev/cu.usbmodem00000',python:'/fake/python',output:join(root,'capture'),expectedBoot:123,onPhase:p=>seen.push(p)};
 return {root,options,dependencies,seen,get closed(){return closed;},get started(){return started;},get queries(){return queries;}};
}
const rows=f=>readFileSync(join(f.options.output,'samples.jsonl'),'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
for(const mode of ['motion','motion-wrong','motion-stale'])test(`optional retained-frame metrics remain anonymous and boot/freshness scoped: ${mode}`,async t=>{
 const f=setup(t,mode),result=await captureSwipeTiming(f.options,f.dependencies);assert.equal(result.outcome,'completed');
 for(const record of rows(f))for(const side of ['before','after']){const m=record[side].motion;if(mode==='motion'){assert.equal(m.boot,123);assert.equal(m.frames,1);assert.equal(m.ageMs,10);assert.equal(Object.keys(m).length,motionMetricKeys.length+1);}else assert.equal(m,null);}
 assert.ok(!readFileSync(join(f.options.output,'samples.jsonl'),'utf8').includes('do-not-log'));
});
test('bounded staged collector owns/closes only its host; complete private anonymous phase coverage',async t=>{
 const f=setup(t),r=await captureSwipeTiming(f.options,f.dependencies);
 assert.equal(r.outcome,'completed');assert.equal(r.lifecycle,'completed');assert.ok(r.coverage.adequate);assert.equal(r.symptomReproduced,'not-assessed');
 assert.equal(f.closed,1);assert.equal(f.started,1);assert.deepEqual(f.seen.map(p=>p.name),[...phases.map(p=>p.name),'finished']);assert.ok(r.records<128&&r.timingOk>50);assert.equal(r.unavailable,0);
 const text=readFileSync(join(f.options.output,'samples.jsonl'),'utf8');assert.ok(!text.includes('do-not-log'));assert.ok(Buffer.byteLength(text)<512*1024);
 const records=rows(f);assert.equal(records.length,r.records);assert.ok(records.at(-1).final&&records.at(-1).requestStartMs>=110000);assert.ok(r.coverage.boundaries.every(b=>b.before&&b.after));
 assert.equal(statSync(f.options.output).mode&0o777,0o700);for(const file of ['samples.jsonl','summary.json'])assert.equal(statSync(join(f.options.output,file)).mode&0o777,0o600);
 assert.deepEqual(JSON.parse(readFileSync(join(f.options.output,'summary.json'))),r);
 await assert.rejects(captureSwipeTiming(f.options,f.dependencies));assert.equal(f.started,1);
});
for(const [mode,reason] of [['boot','unexpected-device-boot'],['restart','host-fault-or-restart'],['roster','roster-changed'],['same-count','roster-generation-changed'],['roster-during','roster-generation-changed'],['boot-during','unexpected-device-boot'],['link','link-generation-changed'],['missing','three-session-readiness-timeout'],['unavailable','no-valid-ready-baseline'],['disabled','diagnostic-disabled'],['start','capture-error'],['clock','clock'],['cap','capture-cap'],['transport','unexpected-transport'],['diagnostics','anonymous-diagnostics-unavailable'],['unready-baseline','no-valid-ready-baseline']])test(`collector fails closed: ${mode}`,async t=>{
 const f=setup(t,mode),r=await captureSwipeTiming(f.options,f.dependencies);assert.equal(r.outcome,'failed');assert.equal(r.reason,reason);assert.equal(f.closed,mode==='start'?0:1);assert.ok(!JSON.stringify(r).includes('private-error'));assert.ok(r.records<=128);
 if(mode==='roster-during'||mode==='boot-during'){const last=rows(f).at(-1);assert.equal(last.change,reason);assert.equal(last.usable,false);assert.equal(last.before.sessions,last.after.sessions);assert.equal(f.seen.at(-1).name,'stopped');assert.ok(f.seen.at(-1).instruction.startsWith('STOP touching'));assert.deepEqual(f.seen.at(-1).reasons,[reason]);}
});
for(const mode of ['one','stale','final'])test(`lifecycle completion is NOT adequate evidence: ${mode}`,async t=>{
 const f=setup(t,mode),r=await captureSwipeTiming(f.options,f.dependencies);assert.equal(r.lifecycle,'completed');assert.equal(r.outcome,'incomplete');assert.ok(!r.coverage.adequate);assert.ok(r.coverage.issues.includes('incomplete-tail'));
 if(mode==='one'){assert.equal(r.timingOk,1);assert.ok(r.coverage.longestGapMs>100000);assert.ok(!f.seen.some(p=>p.name==='isolated'));}
 if(mode==='stale')assert.ok(rows(f).some(r=>r.before.ready&&!r.before.fresh));
 if(mode==='final')assert.ok(rows(f).at(-1).final);
 assert.ok(f.seen.some(p=>p.name==='hold'));
});
test('brief outage has bounds/recovery and HOLD/resume prompts, not a hidden retry',async t=>{
 const f=setup(t,'brief'),r=await captureSwipeTiming(f.options,f.dependencies);assert.equal(r.lifecycle,'completed');assert.ok(r.coverage.outages.length);assert.ok(r.coverage.outages[0].recovered);assert.ok(r.unavailable>0);
 const records=rows(f);for(let i=1;i<records.length;i++)assert.ok(records[i].requestStartMs-records[i-1].replyEndMs>=1100);
 assert.ok(f.seen.some(p=>p.name==='hold'));assert.equal(f.seen.filter(p=>p.name==='isolated').length,2);
});
test('slow awaited inspections retain crossing-phase bounds and actual prompt times',async t=>{
 const f=setup(t,'slow'),r=await captureSwipeTiming(f.options,f.dependencies);assert.equal(r.lifecycle,'completed');const records=rows(f);assert.ok(records.some(r=>r.phases.length===2));assert.ok(records.every(r=>r.requestMs===1700));assert.ok(f.seen.find(p=>p.name==='isolated').elapsedMs>15000);assert.equal(r.coverage.adequate,false);assert.ok(r.coverage.telemetry.issues.some(s=>s.startsWith('boundary-gap:')));
});
for(const mode of ['unready','halted','unready-phase','final-unhealthy','firmware-clock'])test(`fault telemetry is retained without certifying held exercises: ${mode}`,async t=>{
 const f=setup(t,mode),r=await captureSwipeTiming(f.options,f.dependencies),records=rows(f);
 assert.equal(r.lifecycle,'completed');assert.equal(r.outcome,'incomplete');assert.ok(r.coverage.telemetry.adequate);assert.ok(!r.coverage.guided.adequate);assert.equal(r.unavailable,0);assert.equal(r.timingOk,r.records);
 assert.ok(records.every(r=>r.usable&&r.result.phase==='ok'));assert.ok(records.some(r=>!r.interactionReady));assert.ok(r.coverage.guided.holds.length);assert.ok(r.coverage.guided.healthIntervals.length);
 if(mode==='unready'||mode==='halted'||mode==='unready-phase'){
  const p=r.coverage.guided.perPhase.find(p=>p.name==='isolated');assert.equal(p.instructedMs,0);assert.equal(p.successes,0);assert.ok(r.coverage.issues.includes('incomplete-guided-phase:isolated'));
 }
 if(mode==='halted')assert.ok(r.coverage.guided.holds.some(h=>h.reasons.includes('sampler-halted')));
 if(mode==='final-unhealthy'){assert.equal(r.coverage.final,true);assert.equal(r.coverage.guided.finalReady,false);assert.ok(r.coverage.guided.perPhase.every(p=>p.adequate));}
 if(mode==='firmware-clock')assert.ok(r.coverage.issues.includes('firmware-clock-fault'));
});
test('sampler health loss/recovery has separate telemetry, health and actual HOLD/resume bounds',async t=>{
 const f=setup(t,'health-recovery'),r=await captureSwipeTiming(f.options,f.dependencies);
 assert.equal(r.outcome,'completed');assert.equal(r.unavailable,0);assert.deepEqual(r.coverage.telemetry.outages,[]);assert.equal(r.coverage.guided.ownerPerformance,'not-assessed');
 const health=r.coverage.guided.healthIntervals[0],hold=r.coverage.guided.holds[0];assert.ok(health.recovered);assert.ok(health.reasons.includes('sampler-unready'));assert.ok(hold.reasons.includes('sampler-unready'));assert.ok(hold.endMs>health.endMs);assert.ok(hold.endMs>hold.startMs);
 const p=r.coverage.guided.perPhase.find(p=>p.name==='isolated');assert.equal(p.intervals.length,2);assert.ok(p.instructedMs>=15000&&p.instructedMs<20000);assert.equal(f.seen.filter(p=>p.name==='isolated').length,2);
 // Existing fixture deliberately leaves touch eligibility bit4 clear: gating
 // remains diagnostic evidence, not an excuse to fail every physical phase.
 assert.ok(rows(f).filter(r=>r.interactionReady).every(r=>!(r.result.snapshot.values[17]&4)));
});
test('pre-abort and unsafe arguments/output never open a host or adopt ownership',async t=>{
 const f=setup(t),abort=new AbortController();abort.abort();const r=await captureSwipeTiming({...f.options,signal:abort.signal},f.dependencies);assert.equal(r.reason,'aborted');assert.equal(f.started,0);
 await assert.rejects(captureSwipeTiming({...f.options,expectedBoot:0},f.dependencies));assert.equal(f.started,0);
 chmodSync(f.root,0o755);await assert.rejects(captureSwipeTiming({...f.options,output:join(f.root,'second')},f.dependencies));assert.equal(f.started,0);chmodSync(f.root,0o700);
});
