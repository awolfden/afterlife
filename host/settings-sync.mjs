// S0-S3 only. Caller supplies active transport and independent W1/D2 freshness.
// See docs/settings-protocol.md. These messages never establish delivery authority.
import {randomBytes} from 'node:crypto';
import {crc32} from './wire.mjs';
import {validSettings,settingsValues} from './desk-settings.mjs';
const uint=s=>/^(0|[1-9][0-9]{0,9})$/.test(s)&&Number(s)<=0xffffffff;
const token=s=>typeof s==='string'&&/^[0-9a-f]{32}$/.test(s);
export function settingsFlags(value){if(!validSettings(value))throw new TypeError('Invalid settings');return Number(value.vibration)|Number(value.sounds)<<1|Number(value.animations)<<2|Number(value.cycleSessions)<<3;}
export function settingsFromFlags(flags,brightness){return {vibration:!!(flags&1),sounds:!!(flags&2),animations:!!(flags&4),cycleSessions:!!(flags&8),brightness};}
export function settingsPacket(line){
 if(typeof line!=='string'||line.length>191||!/^[\x20-\x7e]+$/.test(line))return null;
 const parts=line.split(' '),kind=parts.shift(),checksum=parts.pop();
 const sizes={S0:3,S1:9,S2:7,S3:5};if(parts.length!==sizes[kind]||!uint(checksum??''))return null;
 const end=line.lastIndexOf(' ');if(crc32(line.slice(0,end+1))!==Number(checksum))return null;
 if(parts.some((s,i)=>kind!=='S0'&&i===2?!token(s):!uint(s)))return null;
 const values=parts.map((s,i)=>kind!=='S0'&&i===2?s:Number(s));
 const [boot,seq]=values;if(!boot||!seq)return null;
 if(kind==='S0')return values[2]===1?{kind,boot,seq,version:1}:null;
 const sync=values[2];
 if(kind==='S1'){
  const [,,,revision,ack,status,flags,brightness,forced]=values;
  if(!revision||status>1||flags>15||![80,160,240].includes(brightness)||forced>1)return null;
  return {kind,boot,seq,token:sync,revision,ack,status,values:settingsFromFlags(flags,brightness),forced:!!forced};
 }
 if(kind==='S2'){
  const [,,,edit,baseRevision,flags,brightness]=values;
  if(!edit||!baseRevision||flags>15||![80,160,240].includes(brightness))return null;
  return {kind,boot,seq,token:sync,edit,baseRevision,values:settingsFromFlags(flags,brightness)};
 }
 const [,,,stateId,activity]=values;if(!stateId||activity>7)return null;
 return {kind,boot,seq,token:sync,stateId,activity};
}
export function settingsLine(kind,fields){
 const prefix=`${kind} ${fields.join(' ')} `,line=prefix+crc32(prefix);
 if(!settingsPacket(line))throw new TypeError('Invalid settings packet');return line+'\n';
}
export class SettingsSync {
 #store;#tokenFactory;#boot=null;#token=null;#advertised=null;#ack=0;#status=0;#lastEdit=null;#lastSend=-Infinity;
 #interaction=null;#now=-1;#lastFrame=null;
 constructor(store,{tokenFactory=()=>randomBytes(16).toString('hex')}={}){this.#store=store;this.#tokenFactory=tokenFactory;}
 reset(boot=null){
  if(boot!==null&&(!Number.isInteger(boot)||boot<1||boot>0xffffffff))throw new TypeError('Invalid settings boot');
  const next=boot===null?null:this.#tokenFactory();if(boot!==null&&!token(next))throw new Error('Invalid synchronization token');
  this.#boot=boot;this.#token=next;this.#advertised=null;this.#ack=0;this.#status=0;this.#lastEdit=null;this.#lastSend=-Infinity;this.#interaction=null;
 }
 #context(c){return c&&c.ready===true&&c.boot===this.#boot&&this.#boot!==null&&Number.isInteger(c.seq)&&c.seq>0&&c.seq<=0xffffffff&&Number.isSafeInteger(c.now)&&c.now>=0&&c.now>=this.#now;}
 #supported(now){return this.#advertised!==null&&now>=this.#advertised&&now-this.#advertised<=1500;}
 receive(line,context){
  if(!this.#context(context))return false;
  this.#now=context.now;const packet=settingsPacket(line);
  if(!packet||packet.boot!==this.#boot||packet.seq!==context.seq)return false;
  if(packet.kind==='S0'){this.#advertised=context.now;return true;}
  if(!this.#supported(context.now)||packet.token!==this.#token)return false;
  if(packet.kind==='S3'){
   const old=this.#interaction;
   if(old&&(packet.stateId<old.stateId||(packet.stateId===old.stateId&&packet.activity!==old.activity)))return false;
   this.#interaction={stateId:packet.stateId,activity:packet.activity,at:context.now};return true;
  }
  if(packet.kind!=='S2')return false;
  const signature=JSON.stringify([packet.edit,packet.baseRevision,settingsFlags(packet.values),packet.values.brightness]);
  if(packet.edit===this.#ack)return signature===this.#lastEdit;
  if(this.#status||packet.edit!==this.#ack+1||packet.baseRevision!==this.#store.snapshot().revision)return false;
  this.#ack=packet.edit;this.#lastEdit=signature;
  try{this.#store.update(packet.values);}catch{this.#status=1;}
  return true; // Accepted request, not a claim of a successful save.
 }
 frame(context,{forced=false}={}){
  if(!this.#context(context)||typeof forced!=='boolean')return '';
  this.#now=context.now;
  if(!this.#supported(context.now)||context.now-this.#lastSend<250||
    (this.#lastFrame?.boot===context.boot&&context.seq<=this.#lastFrame.seq))return '';
  const value=this.#store.snapshot();if(!value)return '';
  this.#lastSend=context.now;this.#lastFrame={boot:context.boot,seq:context.seq};
  return settingsLine('S1',[this.#boot,context.seq,this.#token,value.revision,this.#ack,this.#status||Number(this.#store.failed),settingsFlags(settingsValues(value)),value.brightness,Number(forced)]);
 }
 state(now){
  const supported=Number.isSafeInteger(now)&&now>=this.#now&&this.#supported(now);
  const interactionFresh=supported&&this.#interaction!==null&&now>=this.#interaction.at&&now-this.#interaction.at<=1500;
  return Object.freeze({supported,interactionFresh,held:!interactionFresh||this.#interaction.activity!==0,failed:!!this.#status||this.#store.failed});
 }
}
