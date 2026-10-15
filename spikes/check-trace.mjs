#!/usr/bin/env node
/** Validate only metadata produced by our phase-1 spike. Never parse full Pi sessions. */
import { open } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

const MAX_BYTES = 256 * 1024;
const MAX_ROWS = 512;
const BASE = ['version', 'kind', 'observedAtMs', 'parentId'];
const IDS = ['runId', 'requestId', 'childId'];
const SHAPES = {
  'session-start': [], 'session-end': [], unavailable: [], 'capture-limit': [],
  'root-input-start': [], 'root-input-end': [],
  'request-hint': IDS, reply: IDS,
  request: [...IDS, 'reason', 'expectsReply'],
  'restored-request': [...IDS, 'reason', 'expectsReply'],
  progress: [...IDS, 'reason', 'expectsReply'],
};
const REQUIRED = ['root-input-start', 'root-input-end', 'request', 'reply', 'restored-request'];
const hash = value => typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
const record = value => value && typeof value === 'object' && !Array.isArray(value);

export function checkTrace(text) {
  if (Buffer.byteLength(text, 'utf8') > MAX_BYTES) throw new Error('Trace exceeds size bound');
  const lines = text.trimEnd().split('\n');
  if (!text.trim() || lines.length > MAX_ROWS) throw new Error('Empty or oversized trace');
  const rows = lines.map((line, i) => {
    let row;
    try { row = JSON.parse(line); } catch { throw new Error(`Invalid JSON at row ${i + 1}`); }
    if (!record(row) || row.version !== 1 || !Object.hasOwn(SHAPES, row.kind)) throw new Error(`Unsupported schema at row ${i + 1}`);
    const keys = [...BASE, ...SHAPES[row.kind]].sort();
    if (JSON.stringify(Object.keys(row).sort()) !== JSON.stringify(keys)) throw new Error(`Unexpected/missing fields at row ${i + 1}`);
    if (!hash(row.parentId) || !Number.isFinite(row.observedAtMs) || row.observedAtMs < 0) throw new Error(`Invalid metadata at row ${i + 1}`);
    for (const key of IDS) if (key in row && !hash(row[key])) throw new Error(`Invalid identity at row ${i + 1}`);
    if ('reason' in row) {
      const progress = row.kind === 'progress';
      if (row.expectsReply !== !progress || !(progress ? row.reason === 'progress_update' : ['need_decision', 'interview_request'].includes(row.reason))) {
        throw new Error(`Invalid request semantics at row ${i + 1}`);
      }
    }
    return row;
  });
  if (rows[0].kind !== 'session-start' || rows.at(-1).kind !== 'session-end') throw new Error('Trace must contain one complete session');
  const requests = new Map();
  const seen = new Set();
  let lastTime = -1;
  let prompt = false;
  rows.forEach((row, i) => {
    if (row.parentId !== rows[0].parentId) throw new Error('Mixed parent identities');
    if (row.observedAtMs < lastTime) throw new Error('Non-monotonic trace');
    if ((i !== 0 && row.kind === 'session-start') || (i !== rows.length - 1 && row.kind === 'session-end')) throw new Error('Multiple sessions in one trace');
    lastTime = row.observedAtMs;
    seen.add(row.kind);
    if (row.kind === 'root-input-start') {
      if (prompt) throw new Error('Overlapping outer prompt spans');
      prompt = true;
    }
    if (row.kind === 'root-input-end') {
      if (!prompt) throw new Error('Prompt end without start');
      prompt = false;
    }
    if (['request', 'restored-request'].includes(row.kind)) {
      const prior = requests.get(row.requestId);
      if (prior && (prior.runId !== row.runId || prior.childId !== row.childId || prior.replied)) throw new Error('Request identity/state conflict');
      requests.set(row.requestId, { runId: row.runId, childId: row.childId, replied: false });
    }
    if (row.kind === 'reply') {
      const req = requests.get(row.requestId);
      if (!req || req.runId !== row.runId || req.childId !== row.childId || req.replied) throw new Error('Reply is duplicate or not correlated to this child request');
      req.replied = true;
    }
  });
  if (prompt && !seen.has('capture-limit')) throw new Error('Unclosed root prompt span');
  const missingScenarios = REQUIRED.filter(kind => !seen.has(kind));
  // Deliberate fail-closed outcome: this spike has no authoritative expiry/termination seam.
  // A self-reported trace also cannot attest that the events came from real installed Pi.
  return {
    structuralPass: true,
    rows: rows.length,
    pendingOrUnknown: [...requests.values()].filter(v => !v.replied).length,
    missingScenarios,
    captureIncomplete: seen.has('capture-limit') || seen.has('unavailable'),
    compatibilityReady: false,
    blockers: ['expiry/termination observation unimplemented', 'real installed-provider and reload evidence requires independent inspection'],
  };
}

/** Ordered captures from one exact parent across reopen. No cross-file clock comparison. */
export function checkTraceSet(texts) {
  if (!Array.isArray(texts) || texts.length < 2 || texts.length > 8) throw new Error('Expected 2–8 ordered captures');
  const checks = texts.map(checkTrace);
  const captures = texts.map(text => text.trimEnd().split('\n').map(line => JSON.parse(line)));
  const parent = captures[0][0].parentId;
  const previous = new Map();
  const seen = new Set();
  let matchedRestorations = 0, matchedRepliedRestorations = 0;
  for (const rows of captures) {
    if (rows[0].parentId !== parent) throw new Error('Cannot combine different parent sessions');
    const current = new Map();
    for (const row of rows) {
      seen.add(row.kind);
      if (['request', 'restored-request'].includes(row.kind)) {
        const prior = previous.get(row.requestId);
        if (prior && (prior.runId !== row.runId || prior.childId !== row.childId)) throw new Error('Cross-capture request identity conflict');
        if (row.kind === 'restored-request' && prior) matchedRestorations++;
        current.set(row.requestId, { ...row, replied: false });
      }
      if (row.kind === 'reply') {
        const req = current.get(row.requestId);
        req.replied = true; // Already correlated and checked by checkTrace.
        if (req.kind === 'restored-request' && previous.get(row.requestId)?.replied) matchedRepliedRestorations++;
      }
    }
    for (const [key, value] of current) previous.set(key, value);
  }
  const missingScenarios = REQUIRED.filter(kind => !seen.has(kind) || (kind === 'restored-request' && !matchedRestorations));
  return {
    structuralPass: true,
    files: texts.length,
    rows: checks.reduce((sum, check) => sum + check.rows, 0),
    matchedRestorations,
    matchedRepliedRestorations,
    missingScenarios,
    captureIncomplete: checks.some(check => check.captureIncomplete),
    compatibilityReady: false,
    blockers: ['pending-reload and expiry/termination evidence not certified', 'capture ordering and live provenance require independent inspection'],
  };
}

export async function readTrace(path) {
  const file = await open(path, 'r');
  try {
    const info = await file.stat();
    if (!info.isFile() || info.size > MAX_BYTES) throw new Error('Not a bounded regular trace file');
    // Bounded even if a producer grows the file after stat.
    const buffer = Buffer.alloc(MAX_BYTES + 1);
    let size = 0;
    while (size < buffer.length) {
      const { bytesRead } = await file.read(buffer, size, buffer.length - size, null);
      if (!bytesRead) break;
      size += bytesRead;
    }
    if (size > MAX_BYTES) throw new Error('Trace exceeds size bound');
    return buffer.subarray(0, size).toString('utf8');
  } finally { await file.close(); }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const paths = process.argv.slice(2);
    if (!paths.length || paths.length > 8) throw new Error('Usage: node spikes/check-trace.mjs <trace.jsonl> [later-capture.jsonl ...] (max 8)');
    const texts = [];
    for (const path of paths) texts.push(await readTrace(path));
    const result = texts.length === 1 ? checkTrace(texts[0]) : checkTraceSet(texts);
    console.log(JSON.stringify(result, null, 2));
    process.exitCode = result.missingScenarios.length || result.captureIncomplete ? 3 : result.compatibilityReady ? 0 : 2;
  } catch (error) {
    // Paths, raw contents and malformed record values are intentionally never echoed.
    console.error('Trace rejected:', error?.code ? 'file access failed' : error.message);
    process.exitCode = 1;
  }
}
