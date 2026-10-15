import test from 'node:test';
import assert from 'node:assert/strict';
import { AttentionModel } from '../host/attention.mjs';
const id = n => n.toString(16).padStart(64, '0');
function fixture() { const m = new AttentionModel(); m.register(id(1)); m.status(id(1), { state: 'idle' }); m.connected(true); return m; }
const item = (m, n, kind = 'child', owner = 1) => m.attention(id(owner), { itemId: id(n), kind });
const session = (m, index = 0) => m.snapshot().sessions[index];

test('root input issues one immediate intent, duplicates and reconnect do not replay it', () => {
  const m = fixture(); item(m, 10, 'input'); assert.equal(m.flush().length, 1);
  item(m, 10, 'input'); m.connected(false); m.connected(true); assert.deepEqual(m.flush(), []);
  assert.equal(session(m).state, 'needs-input');
});

test('child grace is strictly after 10 seconds and a boundary resolution wins', () => {
  const m = fixture(); item(m, 10); item(m, 11);
  m.time(9999); assert.equal(m.flush().length, 0);
  m.time(10000); assert.equal(m.flush().length, 0); m.resolve(id(1), id(10));
  m.time(10001); assert.deepEqual(m.flush().map(x => x.itemId), [id(11)]); assert.equal(m.flush().length, 0);
});

test('ACK is exact, displayed-only and never clears source attention', () => {
  const m = fixture(); item(m, 10); item(m, 11); item(m, 12, 'input');
  assert.equal(session(m).selected.id, id(12)); assert.equal(m.acknowledge(id(1), id(10)), false);
  assert.equal(m.acknowledge(id(1), id(12)), true); assert.equal(session(m).pendingCount, 3);
  assert.equal(session(m).selected.id, id(10)); m.acknowledge(id(1), id(10)); m.acknowledge(id(1), id(11));
  assert.equal(session(m).pendingCount, 3); assert.equal(session(m).selected.id, id(10));
  m.time(20000); assert.equal(m.flush().length, 0);
});

test('stale ACK cannot acknowledge a newly arrived higher-priority item', () => {
  const m = fixture(); item(m, 10); const stale = session(m).selected.id;
  item(m, 11, 'input'); assert.equal(m.acknowledge(id(1), stale), false);
  assert.equal(session(m).unacknowledgedCount, 2);
});

test('three roots keep identically named items isolated; paging changes no ACK or source state', () => {
  const m = fixture(); m.register(id(2)); m.register(id(3));
  for (let owner = 1; owner <= 3; owner++) item(m, 10, 'child', owner);
  m.resolve(id(2), id(10)); assert.deepEqual(m.snapshot().sessions.map(s => s.pendingCount), [1, 0, 1]);
  m.page(1); assert.equal(m.acknowledge(id(1), id(10)), false); m.page(1);
  assert.equal(m.snapshot().selectedSessionId, id(3)); assert.equal(m.acknowledge(id(3), id(10)), true);
  assert.equal(session(m).unacknowledgedCount, 1); m.remove(id(2)); assert.equal(m.snapshot().sessions.length, 2);
});

test('mute suppresses due items, unmute does not replay, a new request can alert', () => {
  const m = fixture(); m.mute(true); item(m, 10, 'input'); item(m, 11);
  m.time(12000); assert.equal(m.flush().length, 0); m.mute(false); assert.equal(m.flush().length, 0);
  item(m, 12, 'input'); assert.equal(m.flush().length, 1); assert.equal(session(m).pendingCount, 3);
});

test('normal status changes and recovered tool errors cannot generate failure intents', () => {
  const m = fixture();
  for (const state of ['thinking', 'working', 'failed', 'working', 'completed', 'idle']) {
    m.status(id(1), { state }); assert.equal(m.flush().length, 0);
  }
  item(m, 10, 'failure'); assert.equal(m.flush().length, 1); assert.equal(session(m).state, 'failed');
  m.resolve(id(1), id(10)); assert.equal(session(m).state, 'idle');
});

test('active children prevent completed/idle display and source unavailability remains explicit', () => {
  const m = fixture(); m.status(id(1), { state: 'completed', activeChildren: 1 }); assert.equal(session(m).state, 'working');
  item(m, 10); m.status(id(1), { state: 'idle', available: false }); m.time(11000);
  assert.equal(m.flush().length, 0); assert.equal(session(m).available, false); assert.equal(session(m).pendingCount, 1);
  m.resolve(id(1), id(10)); assert.equal(session(m).state, 'unavailable');
});

test('missing child telemetry preserves root activity without claiming complete coverage', () => {
  const m = fixture(); m.supervisorSync(id(1), false);
  for (const state of ['idle', 'thinking', 'working', 'completed', 'failed']) {
    m.status(id(1), { state });
    assert.equal(session(m).state, state);
    assert.equal(session(m).incomplete, true);
    assert.equal(session(m).supervisorSynced, false);
    assert.equal(m.flush().length, 0);
  }
  m.status(id(1), { state: 'completed', activeChildren: 1 });
  assert.equal(session(m).state, 'working');
  m.status(id(1), { state: 'idle' });
  item(m, 10, 'input'); assert.equal(session(m).state, 'needs-input');
  m.uncertain(id(1), id(10)); assert.equal(session(m).state, 'unavailable');
  m.resolve(id(1), id(10)); assert.equal(session(m).state, 'idle');
  m.status(id(1), { state: 'idle', available: false });
  assert.equal(session(m).state, 'unavailable');
});

test('terminal-before-create cannot resurrect an item within bounded retention', () => {
  const m = fixture(); m.resolve(id(1), id(10)); assert.equal(item(m, 10), false);
  assert.equal(session(m).pendingCount, 0); assert.equal(m.flush().length, 0);
});

test('disconnect preserves current attention without blocking local lifecycle updates', () => {
  const m = fixture(); m.connected(false); item(m, 10, 'input'); assert.equal(m.flush().length, 0);
  m.status(id(1), { state: 'working' }); assert.equal(session(m).pendingCount, 1);
  m.connected(true); assert.equal(m.flush().length, 1); assert.equal(m.flush().length, 0);
});

test('invalid IDs, fields, times and overflow fail closed without evicting unresolved items', () => {
  const m = fixture(); assert.equal(m.register('BODY-MARKER'), false); assert.equal(item(m, 10, 'approval'), false);
  assert.equal(m.status(id(1), null), false); assert.equal(m.attention(id(1), null), false);
  m.register(id(2)); assert.equal(session(m, 1).state, 'unavailable');
  assert.equal(m.status(id(1), { state: 'working', activeChildren: Infinity }), false);
  m.time(100); assert.equal(m.time(99), false); assert.equal(m.time(NaN), false);
  for (let n = 10; n < 74; n++) assert.equal(item(m, n), true);
  assert.equal(item(m, 74), false); assert.equal(session(m).pendingCount, 64); assert.equal(session(m).incomplete, true);
});

test('outbound snapshots allowlist fixed labels and immutable copies; no prose is forwarded', () => {
  const m = fixture(); m.attention(id(1), { itemId: id(10), kind: 'input', message: 'BODY-MARKER', title: 'TITLE-MARKER', interview: { secret: true } });
  const snapshot = m.snapshot(); assert.ok(!JSON.stringify(snapshot).includes('MARKER'));
  assert.equal(snapshot.sessions[0].label, 'Session 1');
  assert.throws(() => { snapshot.sessions[0].selected.id = id(99); }, TypeError);
  assert.equal(session(m).selected.id, id(10));
});

test('child grace starts at nonzero receipt time, not model construction', () => {
  const m = fixture(); m.time(5000); item(m, 10);
  m.time(15000); assert.equal(m.flush().length, 0);
  m.time(15001); assert.equal(m.flush().length, 1);
});

test('unmuting before a child is due preserves its future eligibility', () => {
  const m = fixture(); m.mute(true); item(m, 10); m.time(5000); m.mute(false);
  m.time(10001); assert.equal(m.flush().length, 1);
});

test('mute consumes due items even while disconnected or source-unavailable', () => {
  for (const blocked of ['disconnected', 'unavailable']) {
    const m = fixture();
    if (blocked === 'disconnected') m.connected(false);
    else m.status(id(1), { state: 'idle', available: false });
    item(m, 10, 'input'); m.mute(true); m.mute(false);
    m.connected(true); m.status(id(1), { state: 'idle' });
    assert.equal(m.flush().length, 0); assert.equal(session(m).pendingCount, 1);
    item(m, 11, 'input'); assert.equal(m.flush().length, 1);
  }
});

test('conflicting duplicate kind cannot restart grace or reissue an intent', () => {
  const m = fixture(); m.time(5000); item(m, 10); m.time(14000);
  assert.equal(item(m, 10, 'input'), false); assert.equal(session(m).selected.kind, 'child');
  m.time(15001); assert.equal(m.flush().length, 1);
  assert.equal(item(m, 10, 'failure'), false); assert.equal(m.flush().length, 0);
});

test('retired identities have an explicit bounded FIFO horizon', () => {
  const m = fixture();
  for (let n = 100; n <= 356; n++) m.resolve(id(1), id(n));
  assert.equal(item(m, 100), true); assert.equal(item(m, 101), false);
  assert.equal(session(m).pendingCount, 1);
});

test('root capacity rejects the seventeenth root without replacing existing roots', () => {
  const m = fixture();
  for (let n = 2; n <= 16; n++) assert.equal(m.register(id(n)), true);
  assert.equal(m.register(id(17)), false); assert.equal(m.snapshot().incomplete, true);
  assert.equal(m.snapshot().sessions.length, 16); assert.equal(session(m).id, id(1));
});

test('removing the displayed root selects a survivor and rejects its stale ACK', () => {
  const m = fixture(); m.register(id(2)); item(m, 10, 'input'); item(m, 10, 'input', 2);
  m.remove(id(1)); assert.equal(m.snapshot().selectedSessionId, id(2));
  assert.equal(m.acknowledge(id(1), id(10)), false);
  assert.equal(session(m).unacknowledgedCount, 1);
});

test('ACK before the first input flush silences without resolving', () => {
  const m = fixture(); item(m, 10, 'input'); assert.equal(m.acknowledge(id(1), id(10)), true);
  assert.equal(m.flush().length, 0); assert.equal(session(m).pendingCount, 1);
});

test('same-time batches are accepted and empty intent arrays are immutable', () => {
  const m = fixture(); assert.equal(m.time(5000), true); assert.equal(m.time(5000), true);
  m.connected(false); assert.equal(Object.isFrozen(m.flush()), true);
  m.connected(true); m.mute(true); assert.equal(Object.isFrozen(m.flush()), true);
});
