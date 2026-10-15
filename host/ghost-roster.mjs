// Display-only, bounded per-host sprite assignments. Never session authority.
import {randomInt} from 'node:crypto';
const valid=id=>typeof id==='string'&&/^[a-f0-9]{64}$/.test(id);
export class GhostRoster {
 #assigned=new Map();#pick;
 constructor(pick=randomInt){if(typeof pick!=='function')throw new TypeError('Invalid random source');this.#pick=pick;}
 get(id){return this.#assigned.get(id);}
 sync(ids,selected){
  if(!Array.isArray(ids)||ids.length>16||ids.some(id=>!valid(id))||new Set(ids).size!==ids.length)throw new TypeError('Invalid sprite roster');
  const live=new Set(ids),next=new Map([...this.#assigned].filter(([id])=>live.has(id))),counts=Array(6).fill(0);
  for(const character of next.values())counts[character]++;
  const choose=()=>{
   const least=Math.min(...counts),choices=counts.flatMap((count,index)=>count===least?[index]:[]),index=this.#pick(choices.length);
   if(!Number.isInteger(index)||index<0||index>=choices.length)throw new RangeError('Invalid random choice');
   return choices[index];
  };
  for(const id of ids)if(!next.has(id)){const character=choose();next.set(id,character);counts[character]++;}
  // Removing sessions can leave duplicates while other characters are free.
  // Reassign only excess copies, preferring to preserve the selected session.
  while(Math.max(...counts)-Math.min(...counts)>1){
   const most=Math.max(...counts),candidates=[...next].reverse().filter(([,character])=>counts[character]===most);
   const [id,old]=candidates.find(([id])=>id!==selected)??candidates[0];
   const replacement=choose();counts[old]--;counts[replacement]++;next.set(id,replacement);
  }
  this.#assigned=next;
 }
}
