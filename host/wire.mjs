import { screenView } from '../preview/screen.mjs';
export const states=['idle','thinking','working','needs-input','child-help','completed','failed','unavailable','offline','empty'];
export function crc32(text){let crc=0xffffffff;for(const byte of Buffer.from(text,'ascii')){crc^=byte;for(let b=0;b<8;b++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return(crc^0xffffffff)>>>0;}
const u32=n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff;
export function encodeFrame(snapshot,{boot,seq,alert=0,eventAck=0,reduced=false}){
 if(!Array.isArray(snapshot?.sessions)||snapshot.sessions.length>16)throw new Error('Invalid session frame');
 const view=screenView(snapshot),row=snapshot.sessions.find(item=>item.id===snapshot.selectedSessionId);
 if(snapshot.sessions.length && !row)throw new Error('Selected session missing');
 const flags=(snapshot.muted?1:0)|(view.acknowledged?2:0)|(row?.incomplete?4:0)|(reduced?8:0)|((row?.attentionCount??0)>0?16:0);
 const values=[boot,seq,Math.max(0,view.index),snapshot.sessions.length,states.indexOf(view.state),row?.pendingCount??0,view.otherWaiting,flags,alert,eventAck];
 if(!values.every(u32)||!boot||!seq||values[3]>16||values[4]>9||values[4]<0||values[5]>256)throw new Error('Invalid device frame');
 const prefix=`W1 ${values.join(' ')} `;return `${prefix}${crc32(prefix)}\n`;
}
// One-shot sound tied to an already accepted display frame; never retransmitted.
export function encodeChime(boot,seq){
 if(!u32(boot)||!u32(seq)||!boot||!seq)throw new Error('Invalid chime frame');
 const prefix=`C1 ${boot} ${seq} `;return `${prefix}${crc32(prefix)}\n`;
}
export function encodeAudioInit(boot,seq){
 if(!u32(boot)||!u32(seq)||!boot||!seq)throw new Error('Invalid audio-init frame');
 const prefix=`A1 ${boot} ${seq} `;return `${prefix}${crc32(prefix)}\n`;
}
export function encodeAudioChime(boot,seq){
 if(!u32(boot)||!u32(seq)||!boot||!seq)throw new Error('Invalid audible-probe frame');
 const prefix=`B1 ${boot} ${seq} `;return `${prefix}${crc32(prefix)}\n`;
}
// Read-only diagnostics; never count these as link liveness or device commands.
export function audioPathStatus(line){
 if(typeof line!=='string'||line.length>191||!/^E1 (?:\d{1,10} ){7}[0-9a-f]{32} \d{1,10}$/.test(line))return null;
 const last=line.lastIndexOf(' '),parts=line.slice(3,last).split(' '),hex=parts.pop();
 const values=parts.map(Number),checksum=Number(line.slice(last+1));
 if(!values.every(u32)||!u32(checksum)||crc32(line.slice(0,last+1))!==checksum)return null;
 const [boot,seq,stage,validMask,mismatchMask,dmaBlocks,errors]=values;
 if(!boot||!seq||stage<1||stage>6||validMask>65535||mismatchMask>65535||(mismatchMask&~validMask)||errors>1023)return null;
 return Object.freeze({boot,seq,stage,validMask,mismatchMask,dmaBlocks,errors,registers:Object.freeze(hex.match(/../g).map(v=>parseInt(v,16)))});
}
// Logical transfer/cleanup counters only; never acoustic or electrical proof.
export function factoryFeedbackStatus(line){
 if(typeof line!=='string'||line.length>191||!/^F1 (?:\d{1,10} ){7}\d{1,10}$/.test(line))return null;
 const last=line.lastIndexOf(' '),values=line.slice(3,last).split(' ').map(Number),checksum=Number(line.slice(last+1));
 if(!values.every(u32)||!u32(checksum)||crc32(line.slice(0,last+1))!==checksum)return null;
 const [boot,seq,errors,audioDrains,motorStops,dmaBlocks,closed]=values;
 if(!boot||!seq||errors>127||closed>1||(audioDrains&&!dmaBlocks))return null;
 return Object.freeze({boot,seq,errors,audioDrains,motorStops,dmaBlocks,closed:closed===1,physicalConfirmed:false});
}
export function deviceStatus(line){
 if(typeof line!=='string'||line.length>191||!/^D2 (?:\d{1,10} ){8}\d{1,10}$/.test(line))return null;
 const last=line.lastIndexOf(' '),values=line.slice(3,last).split(' ').map(Number),checksum=Number(line.slice(last+1));
 if(!values.every(u32)||!u32(checksum)||crc32(line.slice(0,last+1))!==checksum||!values[0]||values[5]>4||values[7]>255)return null;
 const [boot,seq,rendered,alert,buttonId,buttonKind,buttonFrame,capabilities]=values;
 // Master mute is global and may originate from a local Settings scene
 // (rendered=0). Paging/ACK still require an actually displayed sequence.
 if(rendered>seq || (!seq && alert) || (buttonKind && (!buttonId || !buttonFrame || buttonFrame>(buttonKind===4?seq:rendered))))return null;
 return {boot,seq,rendered,alert,buttonId,buttonKind,buttonFrame,capabilities};
}
