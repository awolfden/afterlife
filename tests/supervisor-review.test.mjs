import test from 'node:test';
import assert from 'node:assert/strict';
import { AttentionModel } from '../host/attention.mjs';
import { SupervisorAdapter, SUPERVISOR_EVENTS as E } from '../host/supervisor-adapter.mjs';
import { bindSupervisorObservation } from '../host/supervisor-binding.mjs';
import { EventEmitter } from 'node:events';
const fact = (n, outcome = 'pending', revision = n + 1) => ({ requestId: `request-${n}`, runId: 'run', agent: 'child', childIndex: 0,
  reason: 'need_decision', expectsReply: true, createdAt: 1, expiresAt: 10000, observedAt: 2, outcome, revision });
function fixture(model = new AttentionModel()) {
  const adapter = new SupervisorAdapter('owner', model);
  const envelope = (revision, generation = 'g0', historyComplete = true) => ({ apiVersion: 1, ownerSessionId: 'owner', generation, revision, historyComplete });
  const snapshot = (requests, revision, generation, complete) => adapter.snapshot({ ...adapter.request(), ...envelope(revision, generation, complete), requests });
  const update = (record, generation, complete) => adapter.update({ ...envelope(record.revision, generation, complete), kind: 'transition', record });
  return { model, adapter, snapshot, update, envelope };
}

test('missing terminal evidence stays unknown through later complete snapshots and generations', () => {
  const f = fixture(); f.snapshot([fact(0)], 1); f.snapshot([], 2, 'g0', false); f.snapshot([], 3);
  assert.equal(f.adapter.status().incomplete, true);
  f.snapshot([], 0, 'g1'); assert.equal(f.adapter.status().incomplete, true);
  assert.equal(f.model.snapshot().sessions[0].unknownCount, 1);
  f.snapshot([fact(0, 'cancelled', 1)], 1, 'g1'); assert.equal(f.adapter.status().incomplete, false);
});

test('pinned single-record producer profile rejects duplicate record revisions', () => {
  const f = fixture(); assert.equal(f.snapshot([fact(0), fact(1, 'pending', 1)], 1), false);
  assert.equal(f.model.snapshot().sessions[0].attentionCount, 0);
});

test('per-key revisions cannot regress in the same generation, but may reset in a new one', () => {
  const f = fixture(); f.snapshot([fact(0, 'pending', 5)], 5);
  assert.equal(f.snapshot([fact(0, 'pending', 2)], 6), false);
  assert.equal(f.snapshot([fact(0)], 1, 'g1'), true);
});

test('invalidation retires a generation even when its revision equals the snapshot', () => {
  const f = fixture(); const query = f.adapter.request();
  f.adapter.update({ ...f.envelope(1), kind: 'invalidated' });
  assert.equal(f.adapter.snapshot({ ...query, ...f.envelope(1), requests: [fact(0)] }), false);
  assert.equal(f.adapter.status().needsSnapshot, true);
  assert.equal(f.model.snapshot().sessions[0].unknownCount, 1);
});

test('unrecognized buffered generation cannot silently disappear during reconciliation', () => {
  const f = fixture(); const query = f.adapter.request(); f.update(fact(0), 'other-generation');
  assert.equal(f.adapter.snapshot({ ...query, ...f.envelope(0), requests: [] }), false);
  assert.equal(f.adapter.status().needsSnapshot, true);
});

test('retired-generation horizon is bounded and older unknown generations fail closed', () => {
  const f = fixture();
  for (let n = 0; n <= 17; n++) assert.equal(f.snapshot([], 0, `g${n}`), true);
  assert.equal(f.update(fact(0), 'g16'), false); assert.equal(f.adapter.status().needsSnapshot, false);
  assert.equal(f.update(fact(0), 'g0'), false); assert.equal(f.adapter.status().needsSnapshot, true);
});

test('producer history loss stays sticky until a new authoritative snapshot', () => {
  const f = fixture(); f.snapshot([], 0);
  f.update(fact(0), 'g0', false); f.update(fact(0, 'replied', 2));
  assert.equal(f.adapter.status().incomplete, true);
  f.snapshot([fact(0, 'replied', 2)], 2); assert.equal(f.adapter.status().incomplete, false);
});

test('model capacity does not silently drop unresolved attention or permit haptics from partial state', () => {
  const f = fixture(); f.model.status(f.adapter.sessionId, { state: 'idle' }); f.model.connected(true);
  assert.equal(f.snapshot(Array.from({ length: 65 }, (_, n) => fact(n)), 65), false);
  const state = f.model.snapshot().sessions[0]; assert.equal(state.attentionCount, 64); assert.equal(state.unknownCount, 64);
  assert.equal(state.incomplete, true); f.model.time(20000); assert.equal(f.model.flush().length, 0);
});

test('adapter capacity never evicts unresolved identities to admit a new key', () => {
  // Isolate the 256-record adapter bound from the real model's stricter 64-item bound.
  const unknown = new Set();
  const model = { register: () => true, supervisorSync: () => true, attention: () => true,
    resolve: () => true, uncertain: (_root, item) => { unknown.add(item); return true; } };
  const f = fixture(model); f.snapshot(Array.from({ length: 256 }, (_, n) => fact(n)), 256);
  assert.equal(f.update(fact(256)), false); assert.equal(unknown.size, 256);
  assert.equal(f.adapter.status().needsSnapshot, true);
});

test('an evicted adapter key cannot bypass a still-retained model tombstone', () => {
  const f = fixture(); f.snapshot(Array.from({ length: 256 }, (_, n) => fact(n, 'replied')), 256);
  assert.equal(f.update(fact(256)), true);
  assert.equal(f.update(fact(0, 'pending', 258)), false);
  assert.equal(f.model.snapshot().sessions[0].unknownCount, 1);
  assert.equal(f.adapter.status().needsSnapshot, true);
});

test('query invalidation permits immediate explicit refresh without waiting for old timeout', t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const f = fixture(), emitter = new EventEmitter(), queries = [];
  const bus = { on(name, fn) { emitter.on(name, fn); return () => emitter.off(name, fn); }, emit(name, value) { emitter.emit(name, value); } };
  emitter.on(E.request, value => queries.push(value));
  const binding = bindSupervisorObservation(bus, f.adapter);
  emitter.emit(E.update, { ...f.envelope(1), kind: 'transition', record: {} });
  assert.equal(binding.refresh(), true); assert.equal(queries.length, 2);
  assert.notEqual(queries[0].requestId, queries[1].requestId); binding.dispose();
});

test('two bindings on one public bus remain owner-isolated and dispose independently', () => {
  const emitter = new EventEmitter(), model = new AttentionModel();
  const bus = { on(name, fn) { emitter.on(name, fn); return () => emitter.off(name, fn); }, emit(name, value) { emitter.emit(name, value); } };
  emitter.on(E.request, query => emitter.emit(E.response, { ...query, apiVersion: 1, generation: 'g', revision: 0, historyComplete: true, requests: [] }));
  const a = new SupervisorAdapter('A', model), b = new SupervisorAdapter('B', model);
  const x = bindSupervisorObservation(bus, a), y = bindSupervisorObservation(bus, b);
  emitter.emit(E.update, { apiVersion: 1, ownerSessionId: 'A', generation: 'g', revision: 1, historyComplete: true, kind: 'transition', record: fact(0) });
  assert.deepEqual(model.snapshot().sessions.map(s => s.pendingCount), [1, 0]);
  x.dispose(); assert.equal(b.status().needsSnapshot, false); assert.equal(emitter.listenerCount(E.update), 1); y.dispose();
});
