import {randomBytes} from 'node:crypto';
import {crc32} from './wire.mjs';
import {timingQuery,timingReply,timingFullQuery,timingFullPage,validFullTimingSnapshot} from './touch-timing.mjs';
export const touchFields=Object.freeze(['boot','request','seq','rendered','stage','setupFailures','ioErrors','rejected','gapMs','countRejected','xRejected','yRejected','contacts','releases','armed','tracking','eligible','swipes','blocked','maxGapMs']);
const u32=n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff;
export function touchQuery(boot,request){
 if(!u32(boot)||!boot||!u32(request)||!request)throw new TypeError('Invalid touch query identity');
 const prefix=`TQ1 ${boot} ${request} `;return `${prefix}${crc32(prefix)}\n`;
}
export const rangeFields=Object.freeze(['rangeId','rangeState','rangeSamples','rangeXMin','rangeXMax','rangeYMin','rangeYMax','rangeElapsedMs','rangeWindows']);
export function touchRangeQuery(boot,request,mode=0){
 touchQuery(boot,request);if(mode!==0&&mode!==1)throw new TypeError('Invalid range mode');
 const prefix=`TQ2 ${boot} ${request} ${mode} `;return `${prefix}${crc32(prefix)}\n`;
}
export function touchReply(line){return parseReply(line,'TD1 ',touchFields,validTouchSnapshot);}
export function touchRangeReply(line){return parseReply(line,'TD2 ',[...touchFields,...rangeFields],validRangeSnapshot);}
function parseReply(line,prefix,fields,validate){
 if(typeof line!=='string'||line.length>=192||!line.startsWith(prefix))return null;
 const parts=line.slice(4).split(' ');if(parts.length!==fields.length+1||parts.some(s=>!/^\d{1,10}$/.test(s)))return null;
 const numbers=parts.map(Number);if(!numbers.every(u32))return null;
 const sum=numbers.pop();if(crc32(line.slice(0,line.lastIndexOf(' ')+1))!==sum)return null;
 const s=Object.fromEntries(fields.map((key,i)=>[key,numbers[i]]));
 const {request,...snapshot}=s;if(!request||!validate(snapshot))return null;
 return Object.freeze(s);
}
export function validTouchSnapshot(s){
 const keys=touchFields.filter(k=>k!=='request');
 if(!s||typeof s!=='object'||Array.isArray(s)||Object.keys(s).length!==keys.length||!keys.every(k=>Object.hasOwn(s,k)&&u32(s[k])))return false;
 if(!s.boot||!s.seq||!s.rendered||s.rendered>s.seq||![0,1,2,3,4,9].includes(s.stage)||s.blocked>255||s.armed>1||s.tracking>1||s.eligible>1)return false;
 for(const key of ['setupFailures','ioErrors','rejected','gapMs','countRejected','xRejected','yRejected','contacts','releases','swipes','maxGapMs'])if(s[key]>65535)return false;
 return true;
}
export function validRangeSnapshot(s){
 if(!s||typeof s!=='object'||Array.isArray(s)||!rangeFields.every(k=>Object.hasOwn(s,k)&&u32(s[k])))return false;
 const base=Object.fromEntries(Object.entries(s).filter(([k])=>!rangeFields.includes(k)));
 if(!validTouchSnapshot(base)||s.rangeState>5||s.rangeSamples>50||s.rangeElapsedMs>1000||s.rangeWindows>10)return false;
 if(['rangeXMin','rangeXMax','rangeYMin','rangeYMax'].some(k=>s[k]>4095))return false;
 if(s.rangeXMin>s.rangeXMax||s.rangeYMin>s.rangeYMax)return false;
 if(s.rangeSamples===0&&(s.rangeXMin||s.rangeXMax||s.rangeYMin||s.rangeYMax||s.rangeElapsedMs))return false;
 if(s.rangeState===0)return s.rangeId===0&&s.rangeSamples===0&&s.rangeWindows===0;
 if(!s.rangeId||!s.rangeWindows)return false;
 return [2,3].includes(s.rangeState)?s.rangeSamples>0:s.rangeSamples===0;
}
// One pending request, no queue/cache/replay; diagnostics cannot establish link health.
export class TouchUsbReader {
 #now;#context;#send;#id;#pending=null;#last=-Infinity;#clock=-1;#halted=false;
 constructor({now,context,send,seed=randomBytes(4).readUInt32BE()}){
  if(!u32(seed))throw new TypeError('Invalid diagnostic seed');this.#now=now;this.#context=context;this.#send=send;this.#id=seed;
 }
 #time(){const n=this.#now();if(this.#halted||!Number.isSafeInteger(n)||n<0||n<this.#clock){this.#halted=true;this.reset();return null;}this.#clock=n;return n;}
 request({version=1,mode=0}={}){
  if(![1,2,3,4].includes(version)||![0,1].includes(mode)||(version!==2&&mode!==0))return Promise.resolve({phase:'unavailable'});
  const at=this.#time(),context=this.#context();
  if(at===null||!context||this.#id===0xffffffff)return Promise.resolve({phase:'unavailable'});
  if(this.#pending||at-this.#last<1000)return Promise.resolve({phase:'busy'});
  this.#last=at;const id=++this.#id;
  return new Promise(resolve=>{
   const timer=setTimeout(()=>this.reset(),1500);
   this.#pending={id,at,context,resolve,timer,version,mode,pages:[],pageIdentity:null};
   try{if(this.#send(version===1?touchQuery(context.boot,id):version===3?timingQuery(context.boot,id):version===4?timingFullQuery(context.boot,id):touchRangeQuery(context.boot,id,mode))!==true)this.reset();}catch{this.reset();}
  });
 }
 receive(line){
  const at=this.#time(),p=this.#pending;if(at===null||!p)return false;
  const current=this.#context();
  if(!current||current.epoch!==p.context.epoch||current.boot!==p.context.boot||at-p.at>1500){this.reset();return false;}
  const s=p.version===1?touchReply(line):p.version===3?timingReply(line):p.version===4?timingFullPage(line):touchRangeReply(line);if(!s||s.boot!==current.boot||s.request!==p.id||s.seq<p.context.seq||s.seq>current.seq)return false;
  let snapshot;
  if(p.version===4){
   const identity=`${s.seq}/${s.rendered}`;
   if(p.pages[s.page]||(p.pageIdentity!==null&&p.pageIdentity!==identity)){this.reset();return false;}
   p.pageIdentity=identity;p.pages[s.page]=s.values;
   if(p.pages.filter(Boolean).length!==4)return true; // Recognized fragment; never a successful inspection yet.
   snapshot={format:4,boot:s.boot,seq:s.seq,rendered:s.rendered,values:Object.freeze(p.pages.flat())};
   if(!validFullTimingSnapshot(snapshot)){this.reset();return false;}
  }else{const {request,...rest}=s;snapshot=rest;}
  clearTimeout(p.timer);this.#pending=null;
  p.resolve(p.mode===1&&(s.rangeId!==p.id||s.rangeState!==1)?{phase:'unavailable'}:{phase:'ok',snapshot:Object.freeze(snapshot)});return true;
 }
 reset(){const p=this.#pending;if(!p)return;this.#pending=null;clearTimeout(p.timer);p.resolve({phase:'unavailable'});}
}
