import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { SnapshotRecovery } from '../host/snapshot-recovery.mjs';
import { SupervisorAdapter, SUPERVISOR_EVENTS as E } from '../host/supervisor-adapter.mjs';
import { bindSupervisorObservation } from '../host/supervisor-binding.mjs';
import { AttentionModel } from '../host/attention.mjs';
function fixture() {
  const state = { needsSnapshot: true, awaitingSnapshot: false }; let calls = 0;
  const adapter = { status: () => ({ ...state }), unavailable() { state.needsSnapshot = true; state.awaitingSnapshot = false; } };
  const binding = { refresh() { calls++; return true; } };
  return { state, adapter, binding, calls: () => calls, recovery: new SnapshotRecovery(adapter, binding) };
}

test('recovery permits only three spaced retries per outage and never busy-loops', () => {
  const f = fixture(); assert.equal(f.recovery.tick(0), true); assert.equal(f.recovery.tick(0), false);
  assert.equal(f.recovery.tick(999), false); assert.equal(f.recovery.tick(1000), true);
  assert.equal(f.recovery.tick(2999), false); assert.equal(f.recovery.tick(3000), true);
  assert.equal(f.recovery.tick(100000), false); assert.equal(f.calls(), 3); assert.equal(f.recovery.status().exhausted, true);
});

test('in-flight queries do not consume retry budget or create overlap', () => {
  const f = fixture(); f.state.awaitingSnapshot = true;
  for (const now of [0, 1000, 10000]) assert.equal(f.recovery.tick(now), false);
  assert.equal(f.calls(), 0); assert.equal(f.recovery.status().attempts, 0);
});

test('successful synchronization or explicit reconnect resets the budget, not incomplete history alone', () => {
  const f = fixture(); f.recovery.tick(0); f.recovery.tick(1000); f.recovery.tick(3000);
  f.state.needsSnapshot = false; f.state.incomplete = true;
  assert.equal(f.recovery.tick(3001), false); assert.equal(f.recovery.status().attempts, 0);
  f.state.needsSnapshot = true; assert.equal(f.recovery.tick(3002), true);
  f.recovery.reset(); assert.equal(f.recovery.tick(3002), true);
});

test('failed emits count toward the budget and invalid clocks cannot emit', () => {
  const f = fixture(); let failed = 0; f.binding.refresh = () => { failed++; throw new Error('synthetic'); };
  assert.equal(f.recovery.tick(0), false); f.recovery.tick(1000); f.recovery.tick(3000); f.recovery.tick(99999);
  assert.equal(failed, 3); assert.equal(f.state.needsSnapshot, true);
  assert.throws(() => f.recovery.tick(10), TypeError); assert.throws(() => f.recovery.tick(NaN), TypeError);
});

test('real binding timeout can recover with a correlated response using only read-only bus traffic', t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const emitter = new EventEmitter(), queries = [];
  const bus = { on(name, fn) { emitter.on(name, fn); return () => emitter.off(name, fn); }, emit(name, value) { assert.equal(name, E.request); emitter.emit(name, value); } };
  emitter.on(E.request, value => queries.push(value));
  const adapter = new SupervisorAdapter('owner', new AttentionModel());
  const binding = bindSupervisorObservation(bus, adapter);
  const recovery = new SnapshotRecovery(adapter, binding);
  assert.equal(recovery.tick(0), false); t.mock.timers.tick(1001);
  assert.equal(recovery.tick(1001), true); assert.equal(queries.length, 2);
  emitter.emit(E.response, { ...queries[1], apiVersion: 1, generation: 'g', revision: 0, historyComplete: true, requests: [] });
  assert.equal(recovery.tick(1002), false); assert.equal(recovery.status().attempts, 0);
  assert.equal(adapter.status().needsSnapshot, false); binding.dispose();
});
