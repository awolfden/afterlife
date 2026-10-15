import { AttentionModel } from '/attention.mjs';
import { screenView, motionPose, expressions } from '/screen.mjs';
const $ = id => document.getElementById(id);
const model = new AttentionModel();
const sessionIds = [1,2,3].map(n=>n.toString(16).padStart(64,'0'));
const scenes = new Map(), items = new Map();
let sequence=100, totalIntents=0, previousView='', lastUi=0;
const epoch=performance.now();
for (const id of sessionIds) { model.register(id); model.supervisorSync(id,true); model.status(id,{state:'idle'}); scenes.set(id,'idle'); }
model.connected(true);
function now() { model.time(Math.floor(performance.now()-epoch)); }
function selectedId() { return model.snapshot().selectedSessionId; }
function scenario(state) {
  if (!Object.hasOwn(expressions,state) || ['empty','offline'].includes(state)) return;
  const id=selectedId();
  for (const item of items.get(id)??[]) model.resolve(id,item);
  items.set(id,[]); scenes.set(id,state); model.supervisorSync(id,state!=='unavailable');
  model.status(id,{state:['needs-input','child-help','unavailable'].includes(state)?'working':state,
    activeChildren:['working','child-help'].includes(state)?2:0,available:state!=='unavailable'});
  if (['needs-input','child-help','failed','unavailable'].includes(state)) {
    const item=(++sequence).toString(16).padStart(64,'0'); items.get(id).push(item);
    if (state==='unavailable') model.uncertain(id,item);
    else model.attention(id,{itemId:item,kind:state==='needs-input'?'input':state==='child-help'?'child':'failure'});
  }
}
function paint() {
  const frame=model.snapshot(), view=screenView(frame);
  const emitted=model.flush();
  if (emitted.length) {
    totalIntents+=emitted.length;
    $('events').textContent=`${totalIntents} issued · latest: Session ${sessionIds.indexOf(emitted.at(-1).sessionId)+1}`;
  }
  const key=JSON.stringify(view);
  if (key!==previousView) {
    previousView=key;
    $('screen').dataset.state=view.state;
    $('session-label').textContent=view.session;
    $('status-label').textContent=view.label;
    $('status-label').classList.toggle('status-attention',view.attention);
    $('detail-label').textContent=view.detail;
    $('footer-label').textContent=view.otherWaiting?`${view.otherWaiting} elsewhere · ${frame.muted?'Muted':'Alerts on'}`:view.footer;
    $('screen-desc').textContent=`${view.session}: ${view.label}. ${view.detail}. ${view.footer}.`;
    $('announcement').textContent=$('screen-desc').textContent;
    const expression=expressions[view.state];
    $('mouth').setAttribute('d',expression.mouth); $('brows').setAttribute('d',expression.brows);
    $('mouth').classList.toggle('open-mouth',!!expression.open);
    $('mouth').classList.toggle('gritted-mouth',!!expression.teeth);
    for (const eye of [$('eye-l'),$('eye-r')]) {
      eye.setAttribute('rx',String(expression.eyeRx??21)); eye.setAttribute('ry',String(expression.eyeRy??27));
    }
    for (const pupil of $('pupils').querySelectorAll('ellipse')) pupil.setAttribute('ry',String(expression.pupilRy??16));
    $('tongue').setAttribute('visibility',['working','completed'].includes(view.state)?'visible':'hidden');
    $('teeth').setAttribute('visibility',expression.teeth?'visible':'hidden');
    $('frustration').setAttribute('visibility',view.state==='failed'?'visible':'hidden');
    $('page-dots').replaceChildren(...view.pages.map((page,i)=>{
      const dot=document.createElementNS('http://www.w3.org/2000/svg','circle');
      dot.setAttribute('cx',String(233+(i-(view.pages.length-1)/2)*18));dot.setAttribute('cy','91');dot.setAttribute('r',page.selected?'4':'3');
      dot.setAttribute('class',`page-dot${page.selected?' active':''}`);
      if (page.attention) { dot.setAttribute('stroke','oklch(87% .115 83)');dot.setAttribute('stroke-width','2'); }
      return dot;
    }));
  }
  const row=frame.sessions.find(row=>row.id===frame.selectedSessionId);
  $('ack').disabled=!row?.selected || row.selected.acknowledged;
  $('resolve').disabled=!(items.get(selectedId())?.length);
  $('mute').setAttribute('aria-pressed',String(frame.muted)); $('mute').textContent=frame.muted?'Unmute alerts':'Mute alerts';
  $('scene').value=scenes.get(selectedId());
}
function act(callback) { now(); callback(); paint(); }
$('scene').addEventListener('change',()=>act(()=>scenario($('scene').value)));
$('previous').addEventListener('click',()=>act(()=>model.page(-1)));
$('next').addEventListener('click',()=>act(()=>model.page(1)));
$('ack').addEventListener('click',()=>act(()=>{
  const frame=model.snapshot(), row=frame.sessions.find(row=>row.id===frame.selectedSessionId);
  if (row?.selected) model.acknowledge(row.id,row.selected.id);
}));
$('mute').addEventListener('click',()=>act(()=>model.mute(!model.snapshot().muted)));
$('resolve').addEventListener('click',()=>act(()=>{
  const id=selectedId(); for (const item of items.get(id)??[]) model.resolve(id,item); items.set(id,[]);
}));
$('link').addEventListener('change',()=>act(()=>model.connected($('link').checked)));
$('brightness').addEventListener('input',()=>{
  const value=Math.max(35,Math.min(100,Number($('brightness').value)||85));
  $('screen').style.filter=`brightness(${value/100})`; $('brightness-value').textContent=`${value}%`;
});
$('ambient').addEventListener('change',()=>document.body.classList.toggle('dim',$('ambient').checked));
const motionPreference=matchMedia('(prefers-reduced-motion: reduce)');
$('reduced').checked=motionPreference.matches;
motionPreference.addEventListener('change',event=>{ $('reduced').checked=event.matches; });
act(()=>scenario('thinking'));
function animate(timestamp) {
  if (timestamp-lastUi>=100) { now();paint();lastUi=timestamp; }
  const state=$('screen').dataset.state, reduced=$('reduced').checked;
  const pose=motionPose(state,timestamp-epoch,reduced);
  $('ghost').setAttribute('transform',`translate(${233+pose.x} ${215+pose.y}) rotate(${pose.tilt})`);
  $('pupils').setAttribute('transform',`translate(${pose.gazeX} ${pose.gazeY})`);
  $('shadow').setAttribute('rx',String(61-pose.y*.7));
  $('right-arm').setAttribute('transform',state==='failed'?'rotate(42 80 18)':state==='completed'?'rotate(-48 80 18)':state==='needs-input'?'rotate(-35 80 18)':state==='working'?`rotate(${-12+pose.tilt*2} 80 18)`:'');
  $('left-arm').setAttribute('transform',state==='failed'?'rotate(-42 -80 18)':state==='completed'?'rotate(48 -80 18)':state==='thinking'?'rotate(-36 -80 18)':'');
  const blink=!reduced && ((timestamp-epoch)%5700)>5520;
  const closed=expressions[state].closed || blink;
  $('eyes').setAttribute('visibility',closed?'hidden':'visible');
  $('closed-eyes').setAttribute('visibility',closed?'visible':'hidden');
  $('closed-eyes').setAttribute('d',state==='completed'?'M-44 -14 Q-31 -28 -18 -14 M18 -14 Q31 -28 44 -14':'M-44 -17 Q-31 -8 -18 -17 M18 -17 Q31 -8 44 -17');
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
// Read-only inspection of synthetic state for local browser validation.
Object.defineProperty(window,'petPreview',{value:Object.freeze({snapshot:()=>model.snapshot(),view:()=>screenView(model.snapshot()),intentCount:()=>totalIntents})});
