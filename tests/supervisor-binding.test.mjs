import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { AttentionModel } from '../host/attention.mjs';
import { SupervisorAdapter, SUPERVISOR_EVENTS as E } from '../host/supervisor-adapter.mjs';
import { bindSupervisorObservation } from '../host/supervisor-binding.mjs';
function fixture() {
  const emitter = new EventEmitter(), model = new AttentionModel();
  const adapter = new SupervisorAdapter('synthetic-owner', model);
  model.status(adapter.sessionId, { state: 'idle' });
  const bus = new Proxy({
    on(name, fn) { emitter.on(name, fn); return () => emitter.off(name, fn); },
    emit(name, value) { assert.equal(name, E.request); emitter.emit(name, value); },
  }, { get(target, key) { if (!(key in target)) throw new Error('Unexpected control/API access'); return target[key]; } });
  return { emitter, model, adapter, bus };
}
const response = query => ({ ...query, apiVersion: 1, generation: 'generation', revision: 0, historyComplete: true, requests: [] });

test('binding subscribes before synchronous snapshot and never accesses control APIs', () => {
  const f = fixture(); let calls = 0;
  f.emitter.on(E.request, query => { calls++; f.emitter.emit(E.response, response(query)); });
  const binding = bindSupervisorObservation(f.bus, f.adapter);
  assert.equal(calls, 1); assert.equal(f.adapter.status().needsSnapshot, false);
  assert.equal(f.model.snapshot().sessions[0].supervisorSynced, true);
  binding.dispose(); binding.dispose();
  assert.equal(f.emitter.listenerCount(E.response), 0); assert.equal(f.emitter.listenerCount(E.update), 0);
  assert.equal(binding.refresh(), false);
  const session=f.model.snapshot().sessions[0];assert.equal(session.state,'idle');assert.equal(session.supervisorSynced,false);assert.equal(session.incomplete,true);
});

test('missing API preserves known root activity but marks child coverage incomplete, without busy retries', t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const f = fixture(); let calls = 0; f.emitter.on(E.request, () => calls++);
  const binding = bindSupervisorObservation(f.bus, f.adapter, { timeoutMs: 10 });
  assert.equal(binding.refresh(), false); t.mock.timers.tick(11);
  assert.equal(f.adapter.status().awaitingSnapshot, false); assert.equal(f.adapter.status().needsSnapshot, true);
  const session=f.model.snapshot().sessions[0];assert.equal(session.state,'idle');assert.equal(session.supervisorSynced,false);assert.equal(session.incomplete,true);assert.equal(calls,1);
  binding.dispose();
});

test('late timed-out response cannot answer a newer snapshot request', t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const f = fixture(), queries = []; f.emitter.on(E.request, value => queries.push(value));
  const binding = bindSupervisorObservation(f.bus, f.adapter, { timeoutMs: 10 });
  t.mock.timers.tick(11); binding.refresh();
  f.emitter.emit(E.response, response(queries[0])); assert.equal(f.adapter.status().awaitingSnapshot, true);
  f.emitter.emit(E.response, response(queries[1])); assert.equal(f.adapter.status().needsSnapshot, false);
  binding.dispose();
});

test('owner-unavailable response and throwing emit remain unavailable without escaping', () => {
  const f = fixture(); f.emitter.on(E.request, query => f.emitter.emit(E.response, { requestId: query.requestId, apiVersion: 1, error: 'owner_unavailable' }));
  const binding = bindSupervisorObservation(f.bus, f.adapter);
  assert.equal(f.adapter.status().needsSnapshot, true); binding.dispose();
  const broken = fixture(); broken.emitter.on(E.request, () => { throw new Error('synthetic emitter failure'); });
  const second = bindSupervisorObservation(broken.bus, broken.adapter);
  assert.equal(broken.adapter.status().awaitingSnapshot, false); second.dispose();
});
