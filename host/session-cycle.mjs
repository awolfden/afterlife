// One clock, no per-session timers or catch-up. Never marks navigation manual.
export class SessionCycle {
 #model;#since=null;#selection=null;#roster=null;#now=-1;
 constructor(model){this.#model=model;}
 reset(){this.#since=null;this.#selection=null;this.#roster=null;}
 tick(now,enabled){
  if(!Number.isSafeInteger(now)||now<0||now<this.#now){this.reset();return false;}
  this.#now=now;const snapshot=this.#model.snapshot();
  const selection=this.#model.selection().epoch,roster=this.#model.rosterEpoch();
  const eligible=enabled===true&&snapshot.connected&&snapshot.sessions.length>1&&snapshot.sessions.every(s=>s.available&&s.state!=='unavailable'&&s.attentionCount===0);
  if(!eligible){this.reset();return false;}
  if(this.#since===null||this.#selection!==selection||this.#roster!==roster){this.#since=now;this.#selection=selection;this.#roster=roster;return false;}
  if(now-this.#since<10000)return false;
  const changed=this.#model.cycle();this.#since=now;this.#selection=this.#model.selection().epoch;return changed;
 }
}
