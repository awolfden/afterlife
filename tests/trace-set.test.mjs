import test from 'node:test';
import assert from 'node:assert/strict';
import { checkTraceSet } from '../spikes/check-trace.mjs';
import { createProjector } from '../spikes/attention-observer.ts';

const details = { requestId: 'synthetic-request', runId: 'synthetic-run', agent: 'synthetic-child', childIndex: 0, reason: 'need_decision', expectsReply: true };
function capture(restored = false, parent = 'synthetic-parent', request = details) {
  const rows = [];
  let time = 0;
  const p = createProjector(parent, row => rows.push(row), () => time++);
  p.start();
  if (restored) {
    p.entries([{ type: 'custom_message', customType: 'subagent_supervisor_request', details: request }]);
  } else {
    p.prompt(true); p.prompt(false);
    p.message({ role: 'custom', customType: 'subagent_supervisor_request', details: request });
  }
  p.toolEnd({ toolName: 'subagent_supervisor', isError: false, result: { details: { runId: request.runId, replyTo: request.requestId } } });
  p.stop();
  return rows.map(row => JSON.stringify(row)).join('\n');
}

test('correlates exact parent/request across live and reopened replied history without certifying readiness', () => {
  const result = checkTraceSet([capture(), capture(true)]);
  assert.deepEqual(result.missingScenarios, []);
  assert.equal(result.matchedRestorations, 1);
  assert.equal(result.matchedRepliedRestorations, 1);
  assert.equal(result.compatibilityReady, false);
});

test('never combines different parent sessions into reload evidence', () => {
  assert.throws(() => checkTraceSet([capture(), capture(true, 'foreign-parent')]), /different parent/);
});

test('a different request and reversed captures cannot satisfy matched reload coverage', () => {
  const wrongRequest = { ...details, requestId: 'other-request' };
  assert.deepEqual(checkTraceSet([capture(), capture(true, 'synthetic-parent', wrongRequest)]).missingScenarios, ['restored-request']);
  assert.deepEqual(checkTraceSet([capture(true), capture()]).missingScenarios, ['restored-request']);
});

test('rejects cross-capture child identity mismatch even when each file is internally consistent', () => {
  const restored = capture(true).split('\n').map(JSON.parse);
  for (const row of restored) if ('childId' in row) row.childId = 'f'.repeat(64);
  assert.throws(() => checkTraceSet([capture(), restored.map(JSON.stringify).join('\n')]), /identity conflict/);
});

test('enforces bounds and retains per-file privacy validation', () => {
  assert.throws(() => checkTraceSet([]), /ordered captures/);
  assert.throws(() => checkTraceSet(Array(9).fill(capture())), /ordered captures/);
  const leaked = capture(true).split('\n').map(JSON.parse);
  leaked[1].message = 'FORBIDDEN';
  assert.throws(() => checkTraceSet([capture(), leaked.map(JSON.stringify).join('\n')]), /fields/);
});
