import {crc32} from './wire.mjs';
// Cumulative application/DMA measurements, not optical panel FPS. No authority.
export const motionMetricKeys=Object.freeze(['boot','sequence','uptimeMs','frames','reused','discarded','frameMaxUs','intervalMaxUs','prepareMaxUs','paintMaxUs','submitMaxUs','fenceMaxUs','dirtyPixels','visitedRuns','fault']);
const u32=n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff;
export function validMotionMetrics(value){
 return !!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===motionMetricKeys.length&&motionMetricKeys.every(k=>Object.hasOwn(value,k)&&u32(value[k]))&&value.boot>0&&value.sequence>0&&value.fault<=7;
}
export function motionMetrics(line){
 if(typeof line!=='string'||line.length>=192||!line.startsWith('R3 '))return null;
 const fields=line.slice(3).split(' ');
 if(fields.length!==motionMetricKeys.length+1||!fields.every(s=>/^(0|[1-9][0-9]{0,9})$/.test(s)))return null;
 const values=fields.map(Number);if(!values.every(u32)||crc32(line.slice(0,line.lastIndexOf(' ')+1))!==values.at(-1))return null;
 const result=Object.fromEntries(motionMetricKeys.map((key,i)=>[key,values[i]]));
 return validMotionMetrics(result)?Object.freeze(result):null;
}
// Kept separate from host liveness and session/delivery state. One record only.
export class MotionMetricsReader {
 #value=null;#at=0;
 reset(){this.#value=null;this.#at=0;}
 receive(line,boot,now){
  const value=motionMetrics(line);
  if(!value||value.boot!==boot||!Number.isFinite(now)||now<0||now<this.#at||(this.#value&&(this.#value.boot!==boot||value.sequence<=this.#value.sequence)))return false;
  this.#value=value;this.#at=now;return true;
 }
 snapshot(boot,now){
  if(!this.#value||this.#value.boot!==boot||!Number.isFinite(now)||now<this.#at||now-this.#at>10000)return null;
  return Object.freeze({...this.#value,ageMs:Math.floor(now-this.#at)});
 }
}
