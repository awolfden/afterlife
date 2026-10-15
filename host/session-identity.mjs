// Only explicitly opted-in workspace display metadata crosses the private hub.
import {createHash} from 'node:crypto';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const execute=promisify(execFile);
// Public entry points require a distinct opt-in before consulting workspace tools.
export function spaceIdentityEnabled(env=process.env){return env.WATCHER_SPACE_IDENTITY==='1';}
export const opaque=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
export function displayTitle(value,maxBytes=48){
 if(typeof value!=='string')return '';
 const clean=value.normalize('NFC').replace(/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}]/gu,' ').replace(/\s+/gu,' ').trim();
 let out='';for(const char of clean){if(Buffer.byteLength(out+char,'utf8')>maxBytes)break;out+=char;}return out.trim();
}
export function validIdentity(value){
 return !!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).sort().join(',')==='spaceId,spaceTitle'
  &&opaque(value.spaceId)&&typeof value.spaceTitle==='string'&&value.spaceTitle.length>0&&displayTitle(value.spaceTitle)===value.spaceTitle;
}
// No shell, no focus-dependent lookup, bounded output/time, no pane/session content reads.
export async function herdrIdentity({env=process.env,run=execute}={}){
 if(env.HERDR_ENV!=='1'||typeof env.HERDR_PANE_ID!=='string'||!env.HERDR_PANE_ID||typeof env.HERDR_SOCKET_PATH!=='string'||!env.HERDR_SOCKET_PATH)return null;
 const call=async args=>{const {stdout}=await run('herdr',args,{env,timeout:2000,maxBuffer:32768,encoding:'utf8'});return JSON.parse(stdout)?.result;};
 try{
  const pane=(await call(['pane','current','--current']))?.pane;
  if(typeof pane?.workspace_id!=='string'||!/^w[0-9]+$/.test(pane.workspace_id))return null;
  const workspace=(await call(['workspace','get',pane.workspace_id]))?.workspace;
  if(workspace?.workspace_id!==pane.workspace_id)return null;
  const spaceTitle=displayTitle(workspace.label);if(!spaceTitle)return null;
  // The opaque ID is scoped to inherited server context; never export paths or pane IDs.
  const scope=[env.HERDR_SOCKET_PATH,workspace.workspace_id];
  const spaceId=createHash('sha256').update(JSON.stringify(['watcher-space-v1',...scope])).digest('hex');
  return Object.freeze({spaceId,spaceTitle});
 }catch{return null;}
}
// Fixed wire IDs: purple, Rascal, Mallow, Patch, Wisp, Ember. Never reorder.
// Stateless compatibility fallback, stable across host restarts and rejoin.
// Production G2 uses GhostContext's randomized, collision-free live roster;
// those display assignments last for the controller lifetime, not persistence.
export function ghostCharacter(id){
 if(!opaque(id))throw new TypeError('Invalid ghost identity');
 return createHash('sha256').update(`watcher-character-v1:${id}`).digest().readUInt32BE(0)%6;
}
export function personality(id){
 if(!opaque(id))throw new TypeError('Invalid ghost identity');
 const digest=createHash('sha256').update(`watcher-ghost-v1:${id}`).digest();
 return Object.freeze({seed:digest.readUInt32BE(0),hue:digest.readUInt16BE(4)%360,rhythm:digest[6]%4});
}
// Allocate once per live registration, never by focus, page or workspace order.
// First slot preserves the original purple; released slots can be reused without
// recoloring surviving sessions. Bridge restart starts a new allocation lifetime.
export const ghostPalette=Object.freeze([270,165,45,205,10,315,95,235,135,345,65,185,295,25,115,250]);
export function allocatePersonality(id,usedHues){
 const base=personality(id),slot=ghostPalette.findIndex(hue=>!usedHues.has(hue));
 if(slot<0)throw new RangeError('Ghost palette exhausted');
 return Object.freeze({...base,hue:ghostPalette[slot],rhythm:slot%4});
}
export function ghostPage(snapshot,size=4){
 if(!Number.isInteger(size)||size<1||size>4)throw new TypeError('Invalid ghost page size');
 const sessions=snapshot.sessions,index=sessions.findIndex(s=>s.id===snapshot.selectedSessionId),start=Math.max(0,Math.floor(index/size)*size);
 const visible=sessions.slice(start,start+size),ids=new Set(visible.map(s=>s.id)),offscreen=sessions.filter(s=>!ids.has(s.id));
 return Object.freeze({start,index,visible:Object.freeze(visible),offscreen:offscreen.length,offscreenWaiting:offscreen.filter(s=>s.attentionCount>0).length});
}
