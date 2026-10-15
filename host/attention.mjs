// Original, pure host attention model. No Pi tools, sockets, files, serial or timers.
// Adapters own source authority/ordering; transport owns durable haptic delivery.
import {validIdentity, allocatePersonality} from './session-identity.mjs';
const kinds = new Set(['input', 'child', 'failure']);
const statuses = new Set(['idle', 'thinking', 'working', 'completed', 'failed']);
const priority = { input: 0, child: 1, failure: 2 };
const opaque = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const time = value => Number.isSafeInteger(value) && value >= 0;
const oldest = (a, b) => (a.at ?? Infinity) - (b.at ?? Infinity) || a.id.localeCompare(b.id);
const select = (items, chronological=false) => {
  const open = [...items.values()];
  if(chronological){
    const confirmed=open.filter(item=>!item.acknowledged&&item.confirmed);
    const unseen=confirmed.length?confirmed:open.filter(item=>!item.acknowledged);
    return (unseen.length?unseen:open).sort(oldest)[0];
  }
  const confirmed = open.filter(item => !item.acknowledged && item.confirmed);
  const fresh = confirmed.length ? confirmed : open.filter(item => !item.acknowledged);
  return (fresh.length ? fresh.sort((a, b) => priority[a.kind] - priority[b.kind] || oldest(a, b)) : open.sort(oldest))[0];
};

// Pure bounded ordering primitive; exported for boundary tests, not a reset API.
export const advanceFocusOrdinal = value => Number.isSafeInteger(value)&&value>=0&&value<Number.MAX_SAFE_INTEGER?value+1:null;
export class AttentionModel {
  #sessions = new Map();
  #selected;
  #selectionEpoch=Symbol('selection');
  #rosterEpoch=Symbol('roster');
  rosterEpoch(){return this.#rosterEpoch;}
  #select(id){if(this.#selected!==id){this.#selected=id;this.#selectionEpoch=Symbol('selection');}}
  // In-process authority only; deliberately excluded from display snapshots and persistence.
  selection(){return Object.freeze({id:this.#selected??null,epoch:this.#selectionEpoch});}
  get muted(){return this.#muted;}
  #muted = false;
  #connected = false;
  #now = 0;
  #sessionOverflow = false;
  #autoFocus; #focusOrdinal=0; #focusWatermark=0; #focusExhausted=false; #ordinal = 0; #interactionHold=false;
  #nextFocus(){const next=advanceFocusOrdinal(this.#focusOrdinal);if(next===null){this.#focusExhausted=true;return null;}this.#focusOrdinal=next;return next;}
  constructor({autoFocus=false}={}) { this.#autoFocus=autoFocus; }
  identity(id,value) {
    const session=this.#sessions.get(id);
    if(!session || (value!==null&&!validIdentity(value)))return false;
    if((session.identity?.spaceId??null)!==(value?.spaceId??null)||(session.identity?.spaceTitle??null)!==(value?.spaceTitle??null))this.#rosterEpoch=Symbol('roster');
    session.identity=value?Object.freeze({...value}):null;return true;
  }
  #focus() {
    if(!this.#autoFocus||!this.#connected||this.#focusExhausted||this.#interactionHold)return;
    const current=this.#sessions.get(this.#selected);
    if(current?.available&&current.items.size)return;
    let target,order=Infinity;
    for(const session of this.#sessions.values())if(session.available){
      for(const item of session.items.values())if(item.confirmed&&item.focusOrder>this.#focusWatermark&&item.focusOrder<order){target=session.id;order=item.focusOrder;}
    }
    if(target!==undefined)this.#select(target);
  }

  register(id) {
    if (!opaque(id)) return false;
    if (this.#sessions.has(id)) return true;
    if (this.#sessions.size >= 16) { this.#sessionOverflow = true; return false; }
    const used=new Set([...this.#sessions.values()].map(s=>s.personality.hue));
    const traits=allocatePersonality(id,used);
    this.#sessions.set(id, { id, ordinal:++this.#ordinal, personality:Object.freeze(traits), status: 'idle', completions: 0, activeChildren: 0, available: false, supervisorSynced: null, overflow: false, items: new Map(), retired: new Set() });
    this.#rosterEpoch=Symbol('roster');
    if(this.#selected===undefined)this.#select(id);
    return true;
  }

  // Call only for an authoritative session closure, never a stale heartbeat.
  remove(id) {
    if (!this.#sessions.delete(id)) return false;
    this.#rosterEpoch=Symbol('roster');
    if (this.#selected === id) { this.#select(this.#sessions.keys().next().value); }
    this.#focus();return true;
  }

  status(id, value = {}) {
    const { state, activeChildren = 0, available = true } = value ?? {};
    const session = this.#sessions.get(id);
    if (!session || !statuses.has(state) || !Number.isSafeInteger(activeChildren) || activeChildren < 0 || activeChildren > 256 || typeof available !== 'boolean') return false;
    // Only a live, observed run -> completion edge. Cached status on attach/reconnect is silent.
    if (available && session.available && state === 'completed' && ['thinking', 'working'].includes(session.status)) {
      if (session.completions === Number.MAX_SAFE_INTEGER) return false;
      session.completions++;
    }
    const availabilityChanged=session.available!==available;
    Object.assign(session, { status: state, activeChildren, available });
    if(availabilityChanged)this.#focus();
    // Failed tools/idle/activity are not failure episodes. Only explicit attention() creates alerts.
    return true;
  }

  supervisorSync(id, synced) {
    const session = this.#sessions.get(id);
    if (!session || typeof synced !== 'boolean') return false;
    session.supervisorSynced = synced;
    return true;
  }

  attention(id, value = {}) {
    const { itemId, kind } = value ?? {};
    const session = this.#sessions.get(id);
    if (!session || !opaque(itemId) || !kinds.has(kind)) return false;
    if (session.retired.has(itemId)) return false;
    if (session.items.has(itemId)) {
      const item = session.items.get(itemId);
      if (item.kind !== kind) return false;
      item.confirmed = true;
      item.at ??= this.#now;
      if(item.focusOrder===undefined)item.focusOrder=this.#nextFocus();
      this.#focus();return true;
    }
    if (session.items.size >= 64) { session.overflow = true; return false; }
    session.items.set(itemId, { id: itemId, kind, at: this.#now, focusOrder:this.#nextFocus(), confirmed: true, acknowledged: false, notification: 'waiting' });
    this.#focus();return true;
  }

  // Source uncertainty preserves identity/ACK, but is neither pending proof nor resolution.
  uncertain(id, itemId, kind) {
    const session = this.#sessions.get(id);
    if (!session || !opaque(itemId) || session.retired.has(itemId) || (kind !== undefined && !kinds.has(kind))) return false;
    const existing = session.items.has(itemId);
    if (existing && kind !== undefined && session.items.get(itemId).kind !== kind) return false;
    if (!existing) {
      if (session.items.size >= 64) { session.overflow = true; return false; }
      // Create unknown directly: never expose even a transient confirmed observation.
      session.items.set(itemId, { id: itemId, kind: kind ?? 'child', at: null, confirmed: false, acknowledged: false, notification: 'waiting' });
    } else session.items.get(itemId).confirmed = false;
    return true;
  }

  resolve(id, itemId) {
    const session = this.#sessions.get(id);
    if (!session || !opaque(itemId)) return false;
    session.items.delete(itemId);
    session.retired.add(itemId); // A terminal event before a delayed creation cannot resurrect it.
    if (session.retired.size > 256) session.retired.delete(session.retired.values().next().value);
    this.#focus();return true;
  }

  // Recompute current candidates after interaction; never retain a focus queue.
  interaction(held){
    if(typeof held!=='boolean')return false;
    const changed=this.#interactionHold!==held;this.#interactionHold=held;
    if(changed&&!held)this.#focus();return true;
  }
  cycle(){
    if(!this.#connected||this.#interactionHold||[...this.#sessions.values()].some(s=>s.items.size||!s.available))return false;
    const ids=this.snapshot().sessions.map(s=>s.id);if(ids.length<2)return false;
    this.#select(ids[(ids.indexOf(this.#selected)+1)%ids.length]);return true;
  }
  page(direction) {
    if (direction !== 1 && direction !== -1) return false;
    const ids = this.snapshot().sessions.map(s=>s.id);
    if (!ids.length) return false;
    this.#select(ids[(ids.indexOf(this.#selected) + direction + ids.length) % ids.length]);
    this.#focusWatermark=this.#focusOrdinal;
    return true;
  }

  // A badge must echo both exact identities from the rendered frame; stale ACKs do nothing.
  acknowledge(id, itemId) {
    if (id !== this.#selected) return false;
    const item = select(this.#sessions.get(id)?.items ?? new Map(),this.#autoFocus);
    if (!item || item.id !== itemId || item.acknowledged) return false;
    item.acknowledged = true;
    if (item.notification === 'waiting') item.notification = 'suppressed';
    return true;
  }

  connected(value) {
    if(typeof value!=='boolean')return false;
    if(value&&!this.#connected){
      this.#focusWatermark=this.#focusOrdinal;
    }
    this.#connected=value;return true;
  }
  mute(value) {
    if (typeof value !== 'boolean') return false;
    // Consume currently qualifying notifications under the old mute state before unmuting.
    if (this.#muted && !value) this.#suppressMuted();
    this.#muted = value;
    if (value) this.#suppressMuted();
    return true;
  }

  #eligible(item) {
    // Strictly after the boundary gives a resolution observed at exactly 10 s precedence.
    return item.confirmed && item.notification === 'waiting' && !item.acknowledged && (item.kind !== 'child' || this.#now > item.at + 10000);
  }
  #suppressMuted() {
    for (const session of this.#sessions.values()) for (const item of session.items.values()) {
      if (this.#eligible(item)) item.notification = 'suppressed';
    }
  }

  // Set monotonic host time before applying a same-time batch, then flush after its resolutions.
  time(now) {
    if (!time(now) || now < this.#now) return false;
    this.#now = now;
    if (this.#muted) this.#suppressMuted();
    return true;
  }

  // Returns one-shot INTENTS, not proof of a physical vibration. Caller must persist/deliver them.
  flush(beforeIssue) {
    if (beforeIssue !== undefined && typeof beforeIssue !== 'function') throw new TypeError('Invalid intent commit callback');
    if (this.#muted) { this.#suppressMuted(); return Object.freeze([]); }
    if (!this.#connected) return Object.freeze([]);
    const intents = [];
    for (const session of this.#sessions.values()) {
      if (!session.available) continue; // Unknown integration is not proof of a live qualifying item.
      for (const item of session.items.values()) {
        if (!this.#eligible(item)) continue;
        intents.push(Object.freeze({ sessionId: session.id, itemId: item.id, kind: item.kind }));
      }
    }
    const frozen = Object.freeze(intents);
    // Synchronous durable claim before consuming intents. Callback must not mutate the model.
    // If it throws, every intent remains waiting; the transport must fail closed.
    beforeIssue?.(frozen);
    for (const intent of frozen) this.#sessions.get(intent.sessionId).items.get(intent.itemId).notification = 'issued';
    return frozen;
  }

  // Bounded read-only metadata for durable suppression. No delivery claim is implied.
  notificationReceipts() {
    const records = [];
    for (const session of this.#sessions.values()) {
      for (const item of session.items.values()) {
        if (item.notification !== 'waiting') records.push(Object.freeze({
          sessionId: session.id, itemId: item.id, kind: item.kind, notification: item.notification,
          confirmed: item.confirmed, acknowledged: item.acknowledged, available: session.available,
        }));
      }
    }
    return Object.freeze(records);
  }

  snapshot() {
    const counts=new Map();
    for(const session of this.#sessions.values())if(session.identity)counts.set(session.identity.spaceId,(counts.get(session.identity.spaceId)??0)+1);
    const groups=[...new Set([...this.#sessions.values()].map(s=>s.identity?.spaceId??s.id))];
    const ordered=[...this.#sessions.values()].sort((a,b)=>groups.indexOf(a.identity?.spaceId??a.id)-groups.indexOf(b.identity?.spaceId??b.id));
    const sessions = ordered.map(session => {
      const selected = select(session.items,this.#autoFocus);
      const state = selected ? (selected.confirmed ? ({ input: 'needs-input', child: 'child-help', failure: 'failed' })[selected.kind] : 'unavailable')
        // Missing child coverage does not invalidate directly observed root activity.
        // Keep incomplete metadata separate; unknown attention still wins above.
        : !session.available || session.overflow ? 'unavailable'
        : session.activeChildren && ['idle', 'completed'].includes(session.status) ? 'working' : session.status;
      return Object.freeze({
        id: session.id, label: session.identity?.spaceTitle??`Session ${session.ordinal}`,
        spaceId: session.identity?.spaceId??null, spaceCount: session.identity?counts.get(session.identity.spaceId):1,
        personality: session.personality, state, completions: session.completions, activeChildren: session.activeChildren,
        available: session.available, supervisorSynced: session.supervisorSynced,
        incomplete: session.overflow || session.supervisorSynced === false, attentionCount: session.items.size,
        pendingCount: [...session.items.values()].filter(item => item.confirmed && item.kind !== 'failure').length,
        unknownCount: [...session.items.values()].filter(item => !item.confirmed).length,
        unacknowledgedCount: [...session.items.values()].filter(item => !item.acknowledged).length,
        selected: selected ? Object.freeze({ id: selected.id, kind: selected.kind, confirmed: selected.confirmed, acknowledged: selected.acknowledged }) : null,
      });
    });
    return Object.freeze({ connected: this.#connected, muted: this.#muted, selectedSessionId: this.#selected ?? null,
      incomplete: this.#sessionOverflow, sessions: Object.freeze(sessions) });
  }
}
