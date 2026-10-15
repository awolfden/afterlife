import {crc32} from './wire.mjs';
// Exact little-endian uint16 slots shared with firmware touch-timing.h.
export const timingFields=Object.freeze([
 'uptimeLow','uptimeHigh','loopLastMs','loopMaxMs','loopOver100',
 'renderLastMs','renderMaxMs','renderOver80','touchReadMaxMs','sampleGapMaxMs','sampleGapOver100',
 'reads','ioErrors','invalid','contacts','releases','swipes','state','blocked','lastBlocked','blockedMaxMs',
 'gapCancels','contextCancels','packetCancels','durationCancels','verticalCancels','releaseRejects','unarmedSamples','bounceCancels',
 'navEvents','ackLastMs','ackMaxMs','ackToRenderLastMs','ackToRenderMaxMs','metadataWaits','metadataWaitMaxMs',
 'deniedEvents','frameAgeMaxMs','buttonsMaxMs','audioMaxMs','imuMaxMs','ingressMaxMs','voiceMaxMs',
 'reasonAndReleaseFlags','reasonTimeLow','reasonTimeHigh','blockedLastMs','renders',
]);
const u32=n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff;
export function timingQuery(boot,request){
 if(!u32(boot)||!boot||!u32(request)||!request)throw new TypeError('Invalid timing query identity');
 const prefix=`TQ3 ${boot} ${request} `;return `${prefix}${crc32(prefix)}\n`;
}
export function validTimingSnapshot(s){
 if(s?.format===4)return validFullTimingSnapshot(s);
 const keys=['boot','seq','rendered',...timingFields];
 if(!s||typeof s!=='object'||Array.isArray(s)||Object.keys(s).length!==keys.length||!keys.every(k=>Object.hasOwn(s,k)&&u32(s[k])))return false;
 return !!s.boot&&!!s.seq&&!!s.rendered&&s.rendered<=s.seq&&timingFields.every(k=>s[k]<=65535)&&s.state<=31&&s.blocked<=255&&s.lastBlocked<=255&&(s.reasonAndReleaseFlags&255)<=9&&(s.reasonAndReleaseFlags>>>8)<=15;
}
export function timingReply(line){
 if(typeof line!=='string'||line.length>=192||!line.startsWith('TD3 '))return null;
 const parts=line.slice(4).split(' ');
 if(parts.length!==6||![...parts.slice(0,4),parts[5]].every(s=>/^\d{1,10}$/.test(s))||!/^[A-Za-z0-9+/]{128}$/.test(parts[4]))return null;
 const [boot,request,seq,rendered]=parts.slice(0,4).map(Number),sum=Number(parts[5]);
 if(![boot,request,seq,rendered,sum].every(u32)||!request||crc32(line.slice(0,line.lastIndexOf(' ')+1))!==sum)return null;
 const bytes=Buffer.from(parts[4],'base64');if(bytes.length!==96||bytes.toString('base64')!==parts[4])return null;
 const snapshot={boot,seq,rendered,...Object.fromEntries(timingFields.map((key,i)=>[key,bytes.readUInt16LE(i*2)]))};
 return validTimingSnapshot(snapshot)?Object.freeze({request,...snapshot}):null;
}
export function timingFullQuery(boot,request){
 if(!u32(boot)||!boot||!u32(request)||!request)throw new TypeError('Invalid timing query identity');
 const prefix=`TQ4 ${boot} ${request} `;return `${prefix}${crc32(prefix)}\n`;
}
export function validFullTimingSnapshot(s){
 if(!s||typeof s!=='object'||Array.isArray(s)||Object.keys(s).length!==5||s.format!==4||!['boot','seq','rendered'].every(k=>u32(s[k])&&s[k]>0)||s.rendered>s.seq||!Array.isArray(s.values)||s.values.length!==192||!Array.from(s.values).every(n=>u32(n)&&n<=65535))return false;
 const v=s.values,core={boot:s.boot,seq:s.seq,rendered:s.rendered,...Object.fromEntries(timingFields.map((k,i)=>[k,v[i]]))};
 return validTimingSnapshot(core)&&v[51]<=511&&v[52]<=511&&v[80]<=7&&v[128]<=15&&v[156]<=511&&v[157]<=1&&v[158]<=1&&v[160]<=9;
}
export function timingFullPage(line){
 if(typeof line!=='string'||line.length>=192||/[\r\n]/.test(line)||!line.startsWith('TD4 '))return null;
 const p=line.slice(4).split(' ');
 if(p.length!==7||![...p.slice(0,4),p[6]].every(x=>/^\d{1,10}$/.test(x))||!/^\d$/.test(p[4])||Number(p[4])>3||!/^[A-Za-z0-9+/]{128}$/.test(p[5]))return null;
 const [boot,request,seq,rendered]=p.slice(0,4).map(Number),sum=Number(p[6]);
 if(![boot,request,seq,rendered,sum].every(u32)||!boot||!request||!seq||!rendered||rendered>seq||crc32(line.slice(0,line.lastIndexOf(' ')+1))!==sum)return null;
 const bytes=Buffer.from(p[5],'base64');if(bytes.length!==96||bytes.toString('base64')!==p[5])return null;
 return {boot,request,seq,rendered,page:Number(p[4]),values:Array.from({length:48},(_,i)=>bytes.readUInt16LE(i*2))};
}
export const gateCauses=Object.freeze(['staleFrame','pendingEvent','buttons','displayIdentity','voice','exit','singleSession','context','metadata']);
export const slowOperations=Object.freeze(['buttons','audio','imu','ingress','voice','touchRead','renderAttempt','loop']);
// Offline interpretation only. Raw compact snapshots stay below IPC size limits.
export function decodeFullTiming(s){
 if(!validFullTimingSnapshot(s))throw new TypeError('Invalid full timing snapshot');
 const v=s.values,word=i=>v[i]+65536*v[i+1],core=Object.fromEntries(timingFields.map((k,i)=>[k,v[i]]));
 core.renderAttempts=core.renders;delete core.renders;
 return {boot:s.boot,seq:s.seq,rendered:s.rendered,core,uptimeMs:word(0),clockFaults:v[95],
  sampler:{stage:v[160],setupFailures:v[161],invalidCount:v[162],invalidX:v[163],invalidY:v[164],lastGapMs:v[165],maxGapMs:v[166]},
  gating:{intervals:v[48],totalObservedMs:word(49),currentMask:v[51],lastIntervalMask:v[52],causes:Object.fromEntries(gateCauses.map((k,i)=>[k,{entries:v[53+3*i],observedMs:word(54+3*i)}]))},
  pending:{ack:!!(v[80]&1),ackAgeMs:word(81),paint:!!(v[80]&2),paintAgeMs:word(83),ackCompleted:v[85],ackRetired:v[86],paintCompleted:v[87],paintRetired:v[88]},
  paints:{count:word(89),known:!!(v[80]&4),lastAtMs:word(91),ageMs:word(93)},
  slow:Object.fromEntries(slowOperations.map((k,i)=>[k,{count:v[96+4*i],lastMs:v[97+4*i],atMs:word(98+4*i),thresholdMs:i===7?100:i===6?80:20}])),
  release:{historicalFlags:v[43]>>>8,lastFlags:v[128],lastAtMs:word(129),counts:v.slice(131,135)},
  slowLoop:{count:v[144],totalMs:v[145],atMs:word(146),phaseMs:Object.fromEntries(slowOperations.slice(0,7).map((k,i)=>[k,v[148+i]])),unattributedMs:v[155],blockedMask:v[156],touchRead:!!v[157],paint:!!v[158]}};
}
