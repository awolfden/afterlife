import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, stat, rm, mkdtemp, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import observer, { createProjector, requireVersions, privateTrace } from '../spikes/attention-observer.ts';
import { checkTrace, readTrace } from '../spikes/check-trace.mjs';

const details = (requestId = 'request-a', runId = 'run-a') => ({ requestId, runId, agent: 'synthetic-child', childIndex: 0, reason: 'need_decision', expectsReply: true });
const message = d => ({ role: 'custom', customType: 'subagent_supervisor_request', details: d });
const entry = d => ({ type: 'custom_message', customType: 'subagent_supervisor_request', details: d });
const replyEvent = (replyTo = 'request-a', runId = 'run-a', isError = false) => ({ toolName: 'subagent_supervisor', isError, result: { details: { replyTo, runId } } });
function fixture(parent = 'synthetic-parent') {
  const rows = [];
  let time = 100;
  const p = createProjector(parent, row => rows.push(row), () => time++);
  p.start();
  return { p, rows, text: () => rows.map(r => JSON.stringify(r)).join('\n') + '\n' };
}
function completeTrace() {
  const f = fixture();
  f.p.prompt(true); f.p.prompt(false);
  f.p.message(message(details())); f.p.toolEnd(replyEvent());
  f.p.entries([entry(details('restored-b'))]); f.p.stop();
  return f;
}

test('supported versions accepted; drift and wrong packages rejected', () => {
  const pi = { name: '@earendil-works/pi-coding-agent', version: '0.87.0' };
  const sub = { name: 'pi-subagents', version: '0.67.0' };
  requireVersions(pi, sub);
  assert.throws(() => requireVersions({ ...pi, version: '0.88.0' }, sub));
  assert.throws(() => requireVersions(pi, { ...sub, name: 'other' }));
  assert.throws(() => requireVersions(null, sub));
});

test('module is inert when not explicitly opted in', () => {
  const previous = process.env.PI_WATCHER_PROBE;
  delete process.env.PI_WATCHER_PROBE;
  try { observer(new Proxy({}, { get() { throw new Error('Unexpected Pi API access'); } })); }
  finally { if (previous !== undefined) process.env.PI_WATCHER_PROBE = previous; }
});

test('root prompt records ignore free-form titles and do not create approval semantics', () => {
  const f = fixture(); f.p.prompt(true); f.p.prompt(false); f.p.stop();
  assert.deepEqual(f.rows.map(r => r.kind), ['session-start', 'root-input-start', 'root-input-end', 'session-end']);
  assert.ok(!f.text().includes('approval'));
  assert.equal(checkTrace(f.text()).structuralPass, true);
});

test('live request and exact successful reply correlate; duplicate delivery suppressed', () => {
  const f = fixture();
  f.p.message(message(details())); f.p.message(message(details()));
  f.p.toolEnd(replyEvent()); f.p.toolEnd(replyEvent()); f.p.stop();
  assert.equal(f.rows.filter(r => r.kind === 'request').length, 1);
  assert.equal(f.rows.filter(r => r.kind === 'reply').length, 1);
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 0);
});

test('failed or unrelated tool result cannot resolve a request', () => {
  const f = fixture(); f.p.message(message(details()));
  f.p.toolEnd(replyEvent('request-a', 'run-a', true));
  f.p.toolEnd({ ...replyEvent(), toolName: 'other' });
  f.p.toolEnd(replyEvent('request-a', 'other-run'));
  f.p.toolEnd(replyEvent('other-request')); f.p.stop();
  assert.equal(f.rows.filter(r => r.kind === 'reply').length, 0);
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 1);
});

test('resolving one of two requests preserves the other', () => {
  const f = fixture(); f.p.message(message(details())); f.p.message(message(details('request-b')));
  f.p.toolEnd(replyEvent()); f.p.stop();
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 1);
});

test('restored history is unknown, not live pending; journaled reply is evidence', () => {
  const f = fixture();
  f.p.entries([entry(details()), { type: 'custom', customType: 'subagent_supervisor_reply', data: { requestId: 'request-a', runId: 'run-a' } }]);
  f.p.stop();
  assert.deepEqual(f.rows.map(r => r.kind), ['session-start', 'restored-request', 'reply', 'session-end']);
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 0);
});

test('a fresh request observation may promote restored-unknown but history never downgrades live', () => {
  const f = fixture(); f.p.entries([entry(details())]); f.p.message(message(details()));
  f.p.entries([entry(details())]); f.p.stop();
  assert.deepEqual(f.rows.map(r => r.kind), ['session-start', 'restored-request', 'request', 'session-end']);
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 1);
});

test('detach hint is recorded separately and does not fabricate pending status', () => {
  const f = fixture(); f.p.hint(details()); f.p.stop();
  assert.equal(f.rows[1].kind, 'request-hint');
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 0);
});

test('progress update never becomes a reply-expecting request', () => {
  const f = fixture();
  f.p.message(message({ ...details(), reason: 'progress_update', expectsReply: false }));
  f.p.message(message({ ...details(), reason: 'progress_update', expectsReply: true }));
  f.p.stop();
  assert.equal(f.rows.filter(r => r.kind === 'progress').length, 1);
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 0);
});

test('forbidden marker fields and raw identities never reach the trace', () => {
  const f = fixture('SECRET_PARENT');
  const secretFields = Object.fromEntries(['requestBody', 'message', 'content', 'interview', 'replyHint', 'childTarget', 'title'].map(k => [k, 'FORBIDDEN_' + k]));
  f.p.message({ ...message({ ...details(), ...secretFields }), content: 'FORBIDDEN_outer' });
  f.p.stop();
  assert.ok(!f.text().includes('FORBIDDEN_'));
  for (const raw of ['SECRET_PARENT', 'synthetic-child', 'request-a', 'run-a']) assert.ok(!f.text().includes(raw));
  assert.equal(checkTrace(f.text()).structuralPass, true);
});

test('raw identity suffix collisions remain separate across parents', () => {
  const a = fixture('parent-one-12345678'); const b = fixture('parent-two-12345678');
  a.p.message(message(details())); b.p.message(message(details()));
  assert.notEqual(a.rows[1].parentId, b.rows[1].parentId);
  assert.notEqual(a.rows[1].requestId, b.rows[1].requestId);
});

test('malformed requests are dropped and oversized history becomes unavailable', () => {
  const f = fixture();
  for (const d of [null, {}, { ...details(), childIndex: -1 }, { ...details(), requestId: 'x'.repeat(257) }, { ...details(), reason: 'surprise' }]) f.p.message(message(d));
  for (let i = 0; i < 600; i++) f.p.entries(Array.from({ length: 513 }, () => entry(details())));
  f.p.stop();
  assert.deepEqual(f.rows.map(r => r.kind), ['session-start', 'unavailable', 'session-end']);
});

test('sink failures do not escape; event count is bounded', () => {
  const p = createProjector('parent', () => { throw new Error('sink down'); });
  assert.doesNotThrow(() => { p.start(); p.prompt(true); p.stop(); });
  const f = fixture(); for (let i = 0; i < 600; i++) f.p.unknown(); f.p.stop();
  assert.equal(f.rows.length, 512);
  assert.equal(f.rows.at(-2).kind, 'capture-limit');
  assert.equal(f.rows.at(-1).kind, 'session-end');
  assert.equal(checkTrace(f.text()).captureIncomplete, true);
});

test('structurally valid synthetic trace is never a Q1 compatibility pass', () => {
  const result = checkTrace(completeTrace().text());
  assert.equal(result.structuralPass, true);
  assert.deepEqual(result.missingScenarios, []);
  assert.equal(result.compatibilityReady, false);
  assert.ok(result.blockers.some(x => x.includes('expiry')));
});

test('trace validator rejects privacy leaks, mixed owners, corruption and missing closure', () => {
  const original = completeTrace().rows;
  const encode = r => r.map(x => JSON.stringify(x)).join('\n');
  const leak = structuredClone(original); leak[1].title = 'secret';
  assert.throws(() => checkTrace(encode(leak)), /fields/);
  const wrongOwner = structuredClone(original); wrongOwner[1].parentId = 'f'.repeat(64);
  assert.throws(() => checkTrace(encode(wrongOwner)), /parent/);
  const backwards = structuredClone(original); backwards[2].observedAtMs = 0;
  assert.throws(() => checkTrace(encode(backwards)), /monotonic/);
  assert.throws(() => checkTrace('{BROKEN\n'), /JSON/);
  assert.throws(() => checkTrace(encode(original.slice(0, -1))), /complete/);
});

test('trace validator rejects a deliberately miscorrelated reply and unsupported schema', () => {
  const rows = structuredClone(completeTrace().rows);
  rows.find(r => r.kind === 'reply').runId = 'f'.repeat(64);
  assert.throws(() => checkTrace(rows.map(r => JSON.stringify(r)).join('\n')), /correlated/);
  assert.throws(() => checkTrace(JSON.stringify({ version: 2 })), /schema/);
});

test('private writer preserves order, restrictive modes, and closes idempotently', async () => {
  const trace = await privateTrace();
  try {
    const f = completeTrace();
    for (const row of f.rows) trace.emit(row);
    await trace.close(); await trace.close();
    assert.equal((await stat(trace.path)).mode & 0o777, 0o600);
    assert.equal((await stat(dirname(trace.path))).mode & 0o777, 0o700);
    assert.equal(await readFile(trace.path, 'utf8'), f.text());
    assert.equal(checkTrace(await readTrace(trace.path)).structuralPass, true);
  } finally { await trace.close(); await rm(dirname(trace.path), { recursive: true, force: true }); }
});

test('cap during a prompt still closes honestly as incomplete', () => {
  const f = fixture(); f.p.prompt(true);
  for (let i = 0; i < 600; i++) f.p.unknown();
  f.p.prompt(false); f.p.stop();
  assert.equal(checkTrace(f.text()).captureIncomplete, true);
  assert.equal(f.rows.filter(r => r.kind === 'capture-limit').length, 1);
});

test('reply journal and tool result never forward free-form fields', () => {
  const f = fixture(); f.p.message(message(details())); f.p.message(message(details('request-b')));
  f.p.entries([{ type: 'custom', customType: 'subagent_supervisor_reply', data: {
    requestId: 'request-a', runId: 'run-a', message: 'FORBIDDEN_reply', childTarget: 'FORBIDDEN_target',
  } }]);
  const result = replyEvent('request-b'); result.result.details.agent = 'FORBIDDEN_agent';
  result.result.content = 'FORBIDDEN_content'; f.p.toolEnd(result); f.p.stop();
  assert.ok(!f.text().includes('FORBIDDEN_'));
  assert.equal(checkTrace(f.text()).pendingOrUnknown, 0);
});

test('validator rejects child mismatch and revival of already replied identity', () => {
  const f = completeTrace();
  const rows = structuredClone(f.rows);
  rows.find(r => r.kind === 'reply').childId = 'f'.repeat(64);
  assert.throws(() => checkTrace(rows.map(r => JSON.stringify(r)).join('\n')), /correlated/);
  const conflict = structuredClone(f.rows);
  const request = { ...conflict.find(r => r.kind === 'request'), kind: 'restored-request', observedAtMs: conflict.at(-1).observedAtMs };
  conflict.splice(-1, 0, request);
  assert.throws(() => checkTrace(conflict.map(r => JSON.stringify(r)).join('\n')), /conflict/);
});

test('actual extension wiring opens/closes a private trace and unsubscribes without control APIs', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'watcher-wiring-test-'));
  const keys = ['PI_WATCHER_PROBE', 'PI_WATCHER_PI_PACKAGE_JSON', 'PI_WATCHER_SUBAGENTS_PACKAGE_JSON'];
  const previous = keys.map(key => process.env[key]);
  let tracePath, switchedTracePath;
  try {
    const { mkdir } = await import('node:fs/promises');
    for (const name of ['pi', 'subagents']) await mkdir(join(dir, name));
    const piPath = join(dir, 'pi/package.json'), subPath = join(dir, 'subagents/package.json');
    await writeFile(piPath, JSON.stringify({ name: '@earendil-works/pi-coding-agent', version: '0.87.0' }));
    await writeFile(subPath, JSON.stringify({ name: 'pi-subagents', version: '0.67.0' }));
    process.env.PI_WATCHER_PROBE = 'synthetic-only';
    process.env.PI_WATCHER_PI_PACKAGE_JSON = piPath;
    process.env.PI_WATCHER_SUBAGENTS_PACKAGE_JSON = subPath;
    const handlers = new Map(); const events = new Map(); const notifications = [];
    let unsubscribed = 0, branchThrows = false;
    // Any attempt to register/invoke tools, emit messages, or call control APIs fails this test.
    const pi = new Proxy({
      on: (name, handler) => { handlers.set(name, handler); },
      events: { on: (name, handler) => { events.set(name, handler); return () => { events.delete(name); unsubscribed++; }; } },
    }, { get(target, name) { if (!(name in target)) throw new Error('Unexpected API access'); return target[name]; } });
    const ctx = {
      hasUI: true,
      ui: { notify: (text, level) => { notifications.push({ text, level }); } },
      sessionManager: { getSessionId: () => 'synthetic-parent', getBranch: () => {
        if (branchThrows) throw new Error('branch unavailable');
        return [entry(details('historical'))];
      } },
    };
    observer(pi);
    await handlers.get('session_start')({}, ctx);
    tracePath = notifications.find(n => n.text.startsWith('Watcher probe (metadata only): '))?.text.split(': ').at(-1);
    assert.ok(tracePath);
    events.get('pi-intercom:detach-request')(details());
    handlers.get('ui_prompt_start')({ title: 'FORBIDDEN_title' }, ctx);
    handlers.get('ui_prompt_end')({}, ctx);
    handlers.get('message_end')({ message: message(details()) }, ctx);
    handlers.get('tool_execution_end')(replyEvent(), ctx);
    branchThrows = true;
    assert.doesNotThrow(() => handlers.get('agent_settled')({}, ctx));
    branchThrows = false;
    await handlers.get('session_start')({}, ctx); // Switch without a preceding shutdown.
    switchedTracePath = notifications.filter(n => n.text.startsWith('Watcher probe (metadata only): ')).at(-1)?.text.split(': ').at(-1);
    assert.notEqual(switchedTracePath, tracePath);
    assert.equal(unsubscribed, 1);
    assert.equal(events.size, 1);
    await handlers.get('session_shutdown')({}, ctx);
    await handlers.get('session_shutdown')({}, ctx);
    assert.equal(unsubscribed, 2);
    assert.equal(checkTrace(await readTrace(switchedTracePath)).structuralPass, true);
    assert.equal(events.size, 0);
    const text = await readTrace(tracePath);
    assert.ok(!text.includes('FORBIDDEN_'));
    const result = checkTrace(text);
    assert.deepEqual(result.missingScenarios, []);
    assert.equal(result.captureIncomplete, true);
    assert.equal(result.pendingOrUnknown, 1); // Historical request is unknown, not discarded.
  } finally {
    keys.forEach((key, i) => { if (previous[i] === undefined) delete process.env[key]; else process.env[key] = previous[i]; });
    if (tracePath) await rm(dirname(tracePath), { recursive: true, force: true });
    if (switchedTracePath) await rm(dirname(switchedTracePath), { recursive: true, force: true });
    await rm(dir, { recursive: true, force: true });
  }
});

test('CLI distinguishes invalid, valid-but-blocked, and missing scenario evidence', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'watcher-cli-test-'));
  try {
    const cli = fileURLToPath(new URL('../spikes/check-trace.mjs', import.meta.url));
    const path = join(dir, 'trace.jsonl');
    const run = () => spawnSync(process.execPath, [cli, path], { encoding: 'utf8', timeout: 5000 });
    await writeFile(path, 'not json'); assert.equal(run().status, 1);
    await writeFile(path, completeTrace().text()); assert.equal(run().status, 2);
    const f = fixture(); f.p.stop(); await writeFile(path, f.text()); assert.equal(run().status, 3);
  } finally { await rm(dir, { recursive: true, force: true }); }
});

test('reader rejects oversized input without parsing it', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'watcher-test-'));
  try {
    const path = join(dir, 'oversized.jsonl');
    await writeFile(path, 'x'.repeat(256 * 1024 + 1), { mode: 0o600 });
    await assert.rejects(readTrace(path), /bounded/);
  } finally { await rm(dir, { recursive: true, force: true }); }
});
