// Supplemental display-only rows. W1 remains the liveness/control authority.
import {crc32,states} from './wire.mjs';
import {GhostRoster} from './ghost-roster.mjs';
import {displayTitle,ghostPage,personality,ghostCharacter} from './session-identity.mjs';
const uint=n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff;
// Ordered opaque identities stay on the host. Only lifetime-scoped counters go
// to the badge; equal captions/personalities never establish continuity.
export const rosterIdentity=snapshot=>JSON.stringify(snapshot.sessions.map(row=>row.id));
export class GhostContext {
 #characters=new GhostRoster();
 rows(snapshot,options){this.#characters.sync(snapshot.sessions.map(row=>row.id),snapshot.selectedSessionId);return encodeGhostRows(snapshot,{...options,characters:this.#characters});}
 #roster=null;#lifetime=null;#selection=null;#rosterRevision=0;#selectionRevision=0;#boot=0;#seq=0;
 encode(snapshot,selection,{boot,seq},lifetime){
  if(!uint(boot)||!boot||!uint(seq)||!seq)throw new TypeError('Invalid focus generation');
  const roster=rosterIdentity(snapshot);
  if(typeof selection!=='symbol'||typeof lifetime!=='symbol')throw new TypeError('Missing focus identity');
  if(boot===this.#boot&&seq<=this.#seq)throw new RangeError('Stale focus sequence');
  // Use the W1 sequence as a logical clock. A restarted host resumes above the
  // badge's accepted sequence, so its first revisions cannot alias prior ones.
  if(boot!==this.#boot||roster!==this.#roster||lifetime!==this.#lifetime){this.#roster=roster;this.#lifetime=lifetime;this.#rosterRevision=seq;}
  if(boot!==this.#boot||selection!==this.#selection){this.#selection=selection;this.#selectionRevision=seq;}
  this.#boot=boot;this.#seq=seq;
  // K1 is host-to-device display context, distinct from device F1 diagnostics.
  const prefix=`K1 ${boot} ${seq} ${this.#rosterRevision} ${this.#selectionRevision} `;
  return `${prefix}${crc32(prefix)}\n`;
 }
}
// Font0 is not a Unicode font. Preserve Unicode in the host; make badge captions readable.
export const displayCaption=value=>displayTitle(displayTitle(value).normalize('NFKD').replace(/\p{M}/gu,'').replace(/[^\x20-\x7e]/gu,'?'),32);
// Supplemental format advertisement; never a device-status/liveness packet.
export function ghostFamilyStatus(line){
 if(typeof line!=='string'||line.length>96||!/^G0 (?:\d{1,10} ){4}\d{1,10}$/.test(line))return null;
 const parts=line.slice(3).split(' '),values=parts.map(Number),[boot,seq,version,count,checksum]=values;
 if(!values.every((v,i)=>uint(v)&&String(v)===parts[i])||!boot||version!==2||count!==6||crc32(line.slice(0,line.lastIndexOf(' ')+1))!==checksum)return null;
 return Object.freeze({boot,seq});
}
export function encodeGhostRows(snapshot,{boot,seq,family=false,characters}){
 if(typeof family!=='boolean'||!uint(boot)||!boot||!uint(seq)||!seq||snapshot.sessions.length>16)throw new TypeError('Invalid ghost generation');
 const page=ghostPage(snapshot),count=snapshot.sessions.length;
 if(count&&page.index<0)throw new TypeError('Missing selected ghost');
 // Send the rolling neighborhood first, then any remaining legacy page rows.
 // At most six rows: no duplicate ghost identities and no whole-roster stream.
 const indices=new Set();
 if(count)for(const delta of [0,-1,1,-2,2])indices.add((page.index+delta+count)%count);
 for(let i=page.start;i<Math.min(page.start+4,count);i++)indices.add(i);
 return [...indices].map(index=>snapshot.sessions[index]).map(row=>{
  const index=snapshot.sessions.indexOf(row),persona=row.personality??personality(row.id);
  const title=Buffer.from(displayCaption(row.label)||'?','ascii').toString('hex');
  const values=[boot,seq,index,states.indexOf(row.state),row.unacknowledgedCount,row.attentionCount,persona.hue,persona.rhythm,row.spaceCount??1];
  if(!values.every(uint)||values[3]>7||values[4]>64||values[5]>64||values[6]>359||values[7]>3||values[8]<1||values[8]>16)throw new TypeError('Invalid ghost row');
  // G1 is byte-for-byte unchanged for old peers. Family is not a hue/rhythm alias.
  if(family){
   const character=characters?characters.get(row.id):ghostCharacter(row.id);
   if(!Number.isInteger(character)||character<0||character>=6)throw new TypeError('Invalid ghost character');
   values.push(character);
  }
  const prefix=`${family?'G2':'G1'} ${values.join(' ')} ${title} `,line=`${prefix}${crc32(prefix)}\n`;
  if(line.length>=192)throw new RangeError('Ghost row too long');return line;
 }).join('');
}
