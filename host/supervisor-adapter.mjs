import { createHash, randomUUID } from 'node:crypto';

// Public observation protocol only. No native channel creation, mailbox IO or control tools.
export const SUPERVISOR_EVENTS = Object.freeze({
  request: 'pi-subagents:supervisor-observe:request',
  response: 'pi-subagents:supervisor-observe:response',
  update: 'pi-subagents:supervisor-observe:update',
});
const validId = value => typeof value === 'string' && value.length > 0 && value.length <= 256;
const revision = value => Number.isSafeInteger(value) && value >= 0;
const terminal = value => ['replied', 'expired', 'cancelled'].includes(value);
const hash = (...parts) => createHash('sha256').update(JSON.stringify(['pi-watcher-v1', ...parts])).digest('hex');
export const sessionIdentity = owner => {
  if (!validId(owner)) throw new TypeError('Invalid session identity');
  return hash('session', owner);
};

function record(value, owner, maxRevision) {
  if (!value || typeof value !== 'object' || Array.isArray(value)
    || !validId(value.requestId) || !validId(value.runId) || !validId(value.agent)
    || !Number.isSafeInteger(value.childIndex) || value.childIndex < 0
    || value.expectsReply !== true || !['need_decision', 'interview_request'].includes(value.reason)
    || !['pending', 'replied', 'expired', 'cancelled', 'unavailable'].includes(value.outcome)
    || !Number.isFinite(value.createdAt) || !Number.isFinite(value.observedAt)
    || !(value.expiresAt === null || Number.isFinite(value.expiresAt))
    || (value.outcome === 'expired' && value.expiresAt === null)
    || !revision(value.revision) || value.revision < 1 || value.revision > maxRevision) return null;
  return Object.freeze({
    key: hash('request-key', owner, value.requestId),
    itemId: hash('child', owner, value.runId, value.agent, value.childIndex, value.requestId),
    outcome: value.outcome, revision: value.revision,
  });
}

export class SupervisorAdapter {
  #owner;
  #model;
  #records = new Map();
  #generation = null;
  #revision = 0;
  #query = null;
  #buffer = [];
  #needsSnapshot = true;
  #incomplete = true;
  #retiredGenerations = new Set();
  #sessionId;
  get sessionId() { return this.#sessionId; }

  constructor(ownerSessionId, model) {
    this.#sessionId = sessionIdentity(ownerSessionId);
    this.#owner = ownerSessionId;
    this.#model = model;
    if (!model.register(this.sessionId)) throw new Error('Watcher session capacity exceeded');
    model.supervisorSync(this.sessionId, false);
  }

  #unknown() {
    for (const [key, item] of this.#records) if (!terminal(item.outcome)) {
      this.#model.uncertain(this.sessionId, item.itemId);
      this.#records.set(key, { ...item, outcome: 'unavailable' });
    }
  }
  unavailable() {
    this.#unknown(); this.#needsSnapshot = true; this.#incomplete = true;
    this.#query = null; this.#buffer = [];
    this.#model.supervisorSync(this.sessionId, false);
  }
  request() {
    const correlationId = randomUUID();
    this.#query = correlationId; this.#buffer = [];
    return Object.freeze({ requestId: correlationId, ownerSessionId: this.#owner });
  }
  status() {
    return Object.freeze({ needsSnapshot: this.#needsSnapshot, awaitingSnapshot: this.#query !== null,
      incomplete: this.#incomplete || [...this.#records.values()].some(item => item.outcome === 'unavailable'), revision: this.#revision });
  }
  #envelope(value) {
    return value && typeof value === 'object' && value.apiVersion === 1 && value.ownerSessionId === this.#owner
      && validId(value.generation) && revision(value.revision) && typeof value.historyComplete === 'boolean';
  }
  #consistent(items, generation = this.#generation) {
    return items.every(item => {
      const previous = this.#records.get(item.key);
      return !previous || (previous.itemId === item.itemId
        && (previous.generation !== generation || item.revision >= previous.revision)
        && !(terminal(previous.outcome) && item.outcome === 'pending'));
    });
  }
  #apply(item) {
    const previous = this.#records.get(item.key);
    if (previous && terminal(previous.outcome) && item.outcome === 'unavailable') return true;
    if (!this.#records.has(item.key) && this.#records.size >= 256) {
      const retired = [...this.#records].find(([, value]) => terminal(value.outcome));
      if (!retired) return false; // Never discard unresolved identities to make space.
      this.#records.delete(retired[0]);
    }
    this.#records.set(item.key, { ...item, generation: this.#generation });
    if (item.outcome === 'pending') return this.#model.attention(this.sessionId, { itemId: item.itemId, kind: 'child' });
    if (terminal(item.outcome)) return this.#model.resolve(this.sessionId, item.itemId);
    return this.#model.uncertain(this.sessionId, item.itemId);
  }
  snapshot(value) {
    if (!this.#query || value?.requestId !== this.#query) return false;
    if (!this.#envelope(value) || !Array.isArray(value.requests) || value.requests.length > 256) {
      this.unavailable(); return false;
    }
    if (this.#retiredGenerations.has(value.generation)
      || (value.generation === this.#generation && value.revision < this.#revision)) { this.unavailable(); return false; }
    const items = value.requests.map(item => record(item, this.#owner, value.revision));
    if (items.some(item => !item) || new Set(items.map(item => item.key)).size !== items.length
      || new Set(items.map(item => item.revision)).size !== items.length || !this.#consistent(items, value.generation)) {
      this.unavailable(); return false;
    }
    const buffered = this.#buffer; this.#buffer = []; this.#query = null;
    if (this.#generation && value.generation !== this.#generation) {
      this.#retiredGenerations.add(this.#generation);
      if (this.#retiredGenerations.size > 16) this.#retiredGenerations.delete(this.#retiredGenerations.values().next().value);
    }
    this.#generation = value.generation; this.#revision = value.revision;
    this.#unknown(); // Missing from a snapshot is never a resolution, even with historyComplete:true.
    for (const item of items.sort((a, b) => a.revision - b.revision)) {
      if (this.#apply(item) === false) { this.unavailable(); return false; }
    }
    this.#needsSnapshot = false; this.#incomplete = !value.historyComplete;
    for (const update of buffered) {
      if (update.generation === this.#generation && (update.kind === 'invalidated' || update.revision > this.#revision)) {
        if (!this.#transition(update, update.normalized)) return false;
      } else if (update.generation !== this.#generation && !this.#retiredGenerations.has(update.generation)) {
        this.unavailable(); return false;
      }
    }
    this.#model.supervisorSync(this.sessionId, !this.status().incomplete);
    return true;
  }
  update(value) {
    // Shared bus traffic from other owners is ignored, not treated as our failure.
    if (value?.ownerSessionId !== this.#owner) return false;
    if (!this.#envelope(value) || !['transition', 'invalidated'].includes(value.kind)) { this.unavailable(); return false; }
    const item = value.kind === 'transition' ? record(value.record, this.#owner, value.revision) : null;
    if (value.kind === 'transition' && (!item || item.revision !== value.revision)) { this.unavailable(); return false; }
    if (this.#retiredGenerations.has(value.generation)) return false;
    if (this.#query) {
      if (this.#buffer.length >= 256) { this.unavailable(); return false; }
      // Buffer only allowlisted data, not the raw event's arbitrary extra fields.
      this.#buffer.push({ apiVersion: 1, ownerSessionId: this.#owner, generation: value.generation,
        revision: value.revision, historyComplete: value.historyComplete, kind: value.kind, normalized: item });
      return true;
    }
    return this.#transition(value, item);
  }
  #transition(value, item) {
    if (value.generation !== this.#generation || value.kind === 'invalidated') { this.unavailable(); return false; }
    if (value.revision <= this.#revision) return false;
    if (this.#needsSnapshot || value.revision !== this.#revision + 1 || !this.#consistent([item])) { this.unavailable(); return false; }
    if (this.#apply(item) === false) { this.unavailable(); return false; }
    this.#revision = value.revision; this.#incomplete ||= !value.historyComplete;
    this.#model.supervisorSync(this.sessionId, !this.status().incomplete);
    return true;
  }
}
