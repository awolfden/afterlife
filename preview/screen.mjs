// Original preview presentation. Pure helpers never emit haptics or control agents.
const labels = Object.freeze({ idle:'All quiet', thinking:'Thinking', working:'Working', 'needs-input':'Needs you', 'child-help':'Child needs help', completed:'All done', failed:'Failed', unavailable:'Status unknown', offline:'Disconnected', empty:'Connect to Pi' });
export function screenView(frame) {
  const sessions = Array.isArray(frame?.sessions) ? frame.sessions.slice(0,16) : [];
  const index = sessions.findIndex(row => row.id === frame.selectedSessionId);
  const row = sessions[index];
  const state = !row ? 'empty' : !frame.connected ? 'offline' : Object.hasOwn(labels,row.state) ? row.state : 'unavailable';
  const count = value => Number.isSafeInteger(value) && value >= 0 ? Math.min(value,256) : 0;
  const pending = count(row?.pendingCount), unknown = count(row?.unknownCount), children = count(row?.activeChildren);
  const acknowledged = row?.selected?.acknowledged === true;
  const otherWaiting = sessions.filter((item,i) => i!==index && count(item.attentionCount)>0).length;
  let detail = 'Nothing needs you yet';
  if (state==='empty') detail='Your sessions will appear here';
  else if (state==='offline') detail=count(row?.attentionCount) ? 'Request retained' : 'Reconnect to your Mac';
  else if (state==='unavailable') detail=unknown ? `${unknown} unknown request${unknown===1?'':'s'}` : 'Waiting for fresh status';
  else if (state==='needs-input') detail=acknowledged ? 'Acknowledged · still waiting' : 'Waiting for your input';
  else if (state==='child-help') detail=acknowledged ? 'Acknowledged · still waiting' : `${pending} pending request${pending===1?'':'s'}`;
  else if (state==='failed') detail=acknowledged ? 'Acknowledged · failure retained' : 'Check this session';
  else if (children) detail=`${children} child${children===1?'':'ren'} active`;
  else if (state==='completed') detail='Finished without an alert';
  if (frame?.connected && row && (row.incomplete || !row.available) && !['unavailable','empty','offline'].includes(state)) detail='Source status incomplete';
  const footer = `${index<0?0:index+1} / ${sessions.length} · ${frame?.muted?'Muted':'Alerts on'}`;
  return Object.freeze({ state, label:labels[state], session:index<0?'Pi watcher':`Session ${index+1}`, detail, footer,
    otherWaiting, acknowledged, attention:['needs-input','child-help','failed'].includes(state), index,
    pages:Object.freeze(sessions.map((item,i) => Object.freeze({selected:i===index,attention:count(item.attentionCount)>0}))) });
}

export function motionPose(state, milliseconds, reduced = false) {
  if (reduced || !Number.isFinite(milliseconds) || ['offline','unavailable','empty'].includes(state)) return {x:0,y:0,tilt:0,gazeX:0,gazeY:0};
  const t=Math.max(0,milliseconds)/1000;
  if (state==='failed') {
    // A small, slow huff, not a flashing/shaking alarm. No notification side effects.
    const huff=Math.pow(Math.max(0,Math.sin(t*1.3)),4);
    return {x:Math.sin(t*.7)*2,y:-huff*5,tilt:-3+huff*4,gazeX:0,gazeY:1};
  }
  const working=state==='working', thinking=state==='thinking', attention=['needs-input','child-help','failed'].includes(state);
  const speed = working ? 1.15 : thinking ? 0.64 : 0.42;
  return {x:Math.sin(t*speed)*(working?21:attention?4:12),y:Math.sin(t*speed*1.37)*(working?7:5),
    tilt:Math.sin(t*speed*.81)*(working?7:attention?1.5:3),
    gazeX:thinking?4:Math.sin(t*.4)*2,gazeY:thinking?-5:0};
}

export const expressions = Object.freeze({
  idle:{mouth:'M-16 27 Q0 38 16 27',brows:'M-49 -52 Q-33 -60 -17 -53 M17 -53 Q33 -60 49 -52'},
  thinking:{mouth:'M3 32 Q14 23 23 29',brows:'M-49 -49 Q-33 -55 -17 -49 M17 -62 Q33 -73 49 -62',eyeRy:25},
  working:{mouth:'M-22 23 Q0 31 22 23 Q18 48 0 48 Q-18 48 -22 23Z',brows:'M-49 -55 Q-32 -66 -17 -55 M17 -55 Q32 -66 49 -55',open:true},
  'needs-input':{mouth:'M-9 30 C-9 15 9 15 9 30 C9 45 -9 45 -9 30Z',brows:'M-51 -59 Q-33 -73 -16 -60 M16 -60 Q33 -73 51 -59',eyeRx:23,eyeRy:30,open:true},
  'child-help':{mouth:'M-14 30 Q0 40 14 28',brows:'M-49 -51 Q-30 -57 -17 -65 M17 -65 Q30 -57 49 -51',eyeRy:29},
  completed:{mouth:'M-25 23 Q0 33 25 23 Q19 54 0 54 Q-19 54 -25 23Z',brows:'M-49 -50 Q-31 -59 -17 -51 M17 -51 Q31 -59 49 -50',closed:true,open:true},
  failed:{mouth:'M-20 29 Q0 18 20 29 L18 43 Q0 38 -18 43Z',brows:'M-52 -51 Q-34 -47 -16 -31 M16 -31 Q34 -47 52 -51',eyeRx:19,eyeRy:16,pupilRy:10,teeth:true},
  unavailable:{mouth:'M-12 32 L12 32',brows:'M-46 -49 L-18 -49 M18 -49 L46 -49'},
  offline:{mouth:'M-12 30 Q0 35 12 30',brows:'M-46 -48 L-18 -48 M18 -48 L46 -48',closed:true},
  empty:{mouth:'M-12 30 Q0 35 12 30',brows:'M-46 -48 L-18 -48 M18 -48 L46 -48',closed:true},
});
