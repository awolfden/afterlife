import test from 'node:test';
import assert from 'node:assert/strict';
import { AttentionModel } from '../host/attention.mjs';
import { SupervisorAdapter } from '../host/supervisor-adapter.mjs';
const fact = (overrides = {}) => ({ requestId: 'request-1', runId: 'run-1', agent: 'synthetic-child', childIndex: 0,
  reason: 'need_decision', expectsReply: true, createdAt: 10, expiresAt: 600010, observedAt: 20,
  outcome: 'pending', revision: 1, ...overrides });
function fixture(owner = 'synthetic-owner') {
  const model = new AttentionModel(), adapter = new SupervisorAdapter(owner, model);
  model.status(adapter.sessionId, { state: 'idle' }); model.connected(true);
  const envelope = (overrides = {}) => ({ apiVersion: 1, ownerSessionId: owner, generation: 'generation-1', revision: 1, historyComplete: true, ...overrides });
  const snapshot = (requests = [fact()], overrides = {}) => adapter.snapshot({ ...adapter.request(), ...envelope(overrides), requests });
  const update = (value, overrides = {}) => adapter.update({ ...envelope({ revision: value.revision, ...overrides }), kind: 'transition', record: value });
  const session = () => model.snapshot().sessions.find(s => s.id === adapter.sessionId);
  return { model, adapter, envelope, snapshot, update, session };
}

test('verified child facts drive hashed host attention and exact resolution', () => {
  const f = fixture(); f.model.time(5000); assert.equal(f.snapshot(), true);
  assert.equal(f.session().state, 'child-help'); assert.match(f.session().selected.id, /^[a-f0-9]{64}$/);
  f.model.time(15000); assert.equal(f.model.flush().length, 0);
  assert.equal(f.update(fact({ outcome: 'replied', revision: 2 })), true);
  f.model.time(15001); assert.equal(f.model.flush().length, 0); assert.equal(f.session().pendingCount, 0);
});

test('foreign-owner updates cannot alter or invalidate the owning root', () => {
  const f = fixture(); f.snapshot();
  assert.equal(f.update(fact({ outcome: 'cancelled', revision: 2 }), { ownerSessionId: 'foreign' }), false);
  assert.equal(f.session().pendingCount, 1); assert.equal(f.adapter.status().needsSnapshot, false);
  const second = new SupervisorAdapter('foreign', f.model); assert.notEqual(second.sessionId, f.adapter.sessionId);
  assert.throws(() => { f.adapter.sessionId = second.sessionId; }, TypeError);
});

test('duplicate and stale revisions cannot clear or restart a live request', () => {
  const f = fixture(); f.snapshot(); f.model.time(9000); assert.equal(f.update(fact()), false);
  f.model.time(10001); assert.equal(f.model.flush().length, 1);
  assert.equal(f.update(fact({ outcome: 'replied' })), false); assert.equal(f.session().pendingCount, 1);
  assert.equal(f.model.flush().length, 0);
});

test('revision gaps preserve unknown identity, suppress haptics and demand resnapshot', () => {
  const f = fixture(); f.snapshot();
  assert.equal(f.update(fact({ revision: 3 })), false);
  assert.equal(f.adapter.status().needsSnapshot, true); assert.equal(f.session().unknownCount, 1);
  f.model.time(20000); assert.equal(f.model.flush().length, 0);
  assert.equal(f.snapshot([fact({ revision: 3 })], { revision: 3 }), true);
  assert.equal(f.session().pendingCount, 1); assert.equal(f.model.flush().length, 1);
});

test('snapshot absence is unknown, never fabricated resolution or completion', () => {
  const f = fixture(); f.snapshot(); f.snapshot([], { revision: 2 });
  assert.equal(f.session().state, 'unavailable'); assert.equal(f.session().unknownCount, 1);
  assert.equal(f.adapter.status().incomplete, true); f.model.time(20000); assert.equal(f.model.flush().length, 0);
});

test('unknown-first restoration starts grace only when pending is confirmed', () => {
  const f = fixture(); f.snapshot([fact({ outcome: 'unavailable' })]);
  f.model.time(50000); f.update(fact({ revision: 2 })); assert.equal(f.model.flush().length, 0);
  f.model.time(60000); assert.equal(f.model.flush().length, 0);
  f.model.time(60001); assert.equal(f.model.flush().length, 1);
});

test('new-generation snapshot preserves ACK; retired generation cannot revive an item', () => {
  const f = fixture(); f.snapshot(); const itemId = f.session().selected.id;
  f.model.acknowledge(f.adapter.sessionId, itemId);
  f.snapshot([fact()], { generation: 'generation-2' });
  assert.equal(f.session().selected.acknowledged, true);
  assert.equal(f.update(fact({ outcome: 'replied', revision: 2 })), false);
  assert.equal(f.session().pendingCount, 1); f.model.time(20000); assert.equal(f.model.flush().length, 0);
});

test('subscribe-before-snapshot buffered updates newer than snapshot are applied', () => {
  const f = fixture(); const query = f.adapter.request();
  f.update(fact({ outcome: 'expired', revision: 2, message: 'FORBIDDEN_BODY' }));
  assert.equal(f.adapter.snapshot({ ...query, ...f.envelope(), requests: [fact()] }), true);
  assert.equal(f.session().pendingCount, 0); assert.equal(f.adapter.status().revision, 2);
  assert.ok(!JSON.stringify([f.model.snapshot(), f.adapter.status()]).includes('FORBIDDEN'));
});

test('wrong correlations and bounded buffer overflow fail closed', () => {
  const f = fixture(); f.snapshot(); const old = f.adapter.request(); f.adapter.request();
  assert.equal(f.adapter.snapshot({ ...old, ...f.envelope(), requests: [] }), false);
  for (let n = 1; n <= 257; n++) f.update(fact({ revision: n }));
  assert.equal(f.adapter.status().needsSnapshot, true); assert.equal(f.adapter.status().awaitingSnapshot, false);
  assert.equal(f.session().unknownCount, 1);
});

test('invalid records reject the whole snapshot before admitting new attention', () => {
  const f = fixture(); f.snapshot();
  assert.equal(f.snapshot([fact({ requestId: 'new', revision: 2 }), fact({ requestId: 'bad', revision: 3, expectsReply: false })], { revision: 3 }), false);
  assert.equal(f.session().attentionCount, 1); assert.equal(f.session().unknownCount, 1);
});

test('identity conflicts and terminal-to-pending revival are refused', () => {
  const f = fixture(); f.snapshot();
  assert.equal(f.update(fact({ runId: 'different-run', revision: 2 })), false);
  f.snapshot([fact({ outcome: 'cancelled', revision: 2 })], { revision: 2 });
  assert.equal(f.snapshot([fact({ revision: 3 })], { revision: 3 }), false);
  assert.equal(f.session().attentionCount, 0);
});

test('invalidation keeps unresolved attention unknown and does not affect root input', () => {
  const f = fixture(); f.snapshot(); const input = 'a'.repeat(64);
  f.model.attention(f.adapter.sessionId, { itemId: input, kind: 'input' });
  f.adapter.update({ ...f.envelope(), kind: 'invalidated' });
  assert.equal(f.session().unknownCount, 1); assert.equal(f.session().pendingCount, 1);
  assert.equal(f.session().state, 'needs-input'); assert.deepEqual(f.model.flush().map(x => x.itemId), [input]);
});

test('normalization drops prose and raw owner/child identity from badge-bound data', () => {
  const f = fixture(); f.snapshot([fact({ message: 'FORBIDDEN_BODY', interview: { x: 'FORBIDDEN' }, childTarget: 'FORBIDDEN_TARGET' })]);
  f.model.time(10001); const text = JSON.stringify([f.model.snapshot(), f.model.flush(), f.adapter.status()]);
  for (const secret of ['FORBIDDEN', 'synthetic-owner', 'synthetic-child', 'request-1', 'run-1']) assert.ok(!text.includes(secret));
});
