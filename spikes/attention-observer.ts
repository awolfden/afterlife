/** Phase-1 observation spike. Not a production watcher or approval handler. */
import type { ExtensionAPI, ExtensionContext } from "@earendil-works/pi-coding-agent";
import { createHash } from "node:crypto";
import { mkdtemp, open, readFile, stat } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, basename } from "node:path";
import { performance } from "node:perf_hooks";

export const SUPPORTED = Object.freeze({ pi: "0.87.0", subagents: "0.67.0" });
const MAX_RECORDS = 512;
const reasons = new Set(["need_decision", "interview_request", "progress_update"]);
type RecordLike = Record<string, unknown>;
export type Observation = {
  version: 1;
  kind: string;
  observedAtMs: number;
  parentId: string;
  runId?: string;
  requestId?: string;
  childId?: string;
  reason?: string;
  expectsReply?: boolean;
};
const object = (v: unknown): v is RecordLike => !!v && typeof v === "object" && !Array.isArray(v);
const identifier = (v: unknown): v is string => typeof v === "string" && v.length > 0 && v.length <= 256;
// IDs are domain-separated hashes: no names, paths, titles or raw request content leave the seam.
const opaque = (domain: string, value: string) => createHash("sha256").update(domain + "\0" + value).digest("hex");

export function requireVersions(pi: unknown, subagents: unknown): void {
  if (!object(pi) || pi.name !== "@earendil-works/pi-coding-agent" || pi.version !== SUPPORTED.pi
      || !object(subagents) || subagents.name !== "pi-subagents" || subagents.version !== SUPPORTED.subagents) {
    throw new Error("Unsupported probe package versions; repeat source review.");
  }
}

/** Pure projector. Receipt of old history is NOT evidence that a request is still pending. */
export function createProjector(parentSessionId: string, emit: (v: Observation) => void, now = () => performance.now()) {
  if (!identifier(parentSessionId)) throw new Error("Invalid parent identity");
  const parentId = opaque("parent", parentSessionId);
  const started = now();
  const requests = new Map<string, { runId: string; childId: string; phase: "live" | "unknown" | "replied" }>();
  let stopped = false;
  let limited = false;
  let historyUnavailable = false;
  let count = 0;
  let lastTime = 0;
  const send = (kind: string, fields: Partial<Observation> = {}) => {
    if (stopped || (limited && kind !== "session-end")) return;
    // Reserve one record for an explicit limit marker and one for orderly shutdown.
    if (kind !== "session-end" && count >= MAX_RECORDS - 2) {
      kind = "capture-limit";
      fields = {};
      limited = true;
    }
    const elapsed = now() - started;
    if (!Number.isFinite(elapsed) || elapsed < lastTime) { stopped = true; return; }
    lastTime = elapsed;
    count++;
    try { emit({ version: 1, kind, observedAtMs: elapsed, parentId, ...fields }); }
    catch { stopped = true; } // Diagnostics must never throw into Pi.
  };
  const ids = (d: RecordLike) => {
    const rawId = d.requestId ?? d.id;
    if (!identifier(rawId) || !identifier(d.runId) || !identifier(d.agent)
        || !Number.isSafeInteger(d.childIndex) || Number(d.childIndex) < 0) return;
    return {
      requestId: opaque("request", parentSessionId + "\0" + d.runId + "\0" + rawId),
      runId: opaque("run", parentSessionId + "\0" + d.runId),
      childId: opaque("child", parentSessionId + "\0" + d.runId + "\0" + d.agent + "\0" + d.childIndex),
    };
  };
  const request = (details: unknown, restored: boolean) => {
    if (!object(details)) return;
    const id = ids(details);
    if (!id || !reasons.has(String(details.reason)) || typeof details.expectsReply !== "boolean") return;
    if ((details.reason === "progress_update") === details.expectsReply) return;
    const previous = requests.get(id.requestId);
    if (previous?.childId !== undefined && previous.childId !== id.childId) { send("unavailable"); return; }
    if (previous?.phase === "replied" || (previous?.phase === "live") || (previous && restored)) return;
    if (!details.expectsReply) { send("progress", { ...id, reason: "progress_update", expectsReply: false }); return; }
    if (requests.size >= MAX_RECORDS) { send("unavailable"); return; }
    requests.set(id.requestId, { runId: id.runId, childId: id.childId, phase: restored ? "unknown" : "live" });
    send(restored ? "restored-request" : "request", {
      ...id, reason: String(details.reason), expectsReply: true,
    });
  };
  const reply = (details: unknown) => {
    if (!object(details) || !identifier(details.runId)) return;
    const raw = details.requestId ?? details.replyTo;
    if (!identifier(raw)) return;
    const requestId = opaque("request", parentSessionId + "\0" + details.runId + "\0" + raw);
    const previous = requests.get(requestId);
    if (!previous || previous.phase === "replied") return;
    previous.phase = "replied";
    send("reply", { requestId, runId: previous.runId, childId: previous.childId });
  };
  return {
    start() { send("session-start"); },
    prompt(start: boolean) { send(start ? "root-input-start" : "root-input-end"); },
    hint(details: unknown) {
      if (!object(details)) return;
      const id = ids(details);
      if (id) send("request-hint", id); // Immediate surfacing hint; not proof of request state or reason.
    },
    message(message: unknown) {
      if (object(message) && message.role === "custom" && message.customType === "subagent_supervisor_request") {
        request(message.details, false);
      }
    },
    entries(entries: readonly unknown[]) {
      if (entries.length > MAX_RECORDS) {
        if (!historyUnavailable) send("unavailable");
        historyUnavailable = true;
        return;
      }
      for (const e of entries) {
        if (!object(e)) continue;
        if (e.type === "custom_message" && e.customType === "subagent_supervisor_request") request(e.details, true);
        if (e.type === "custom" && e.customType === "subagent_supervisor_reply") reply(e.data);
      }
    },
    toolEnd(event: unknown) {
      if (!object(event) || event.toolName !== "subagent_supervisor" || event.isError !== false || !object(event.result)) return;
      const d = event.result.details;
      // Reply details are only returned AFTER writeReply succeeds in the inspected version.
      if (object(d) && identifier(d.replyTo)) reply(d);
    },
    unknown() { send("unavailable"); },
    stop() { send("session-end"); stopped = true; },
  };
}

/** Creates a private trace only when an explicitly opted-in disposable session starts. */
export async function privateTrace() {
  const dir = await mkdtemp(join(tmpdir(), "pi-watcher-probe-"));
  const path = join(dir, "observations.jsonl");
  const file = await open(path, "wx", 0o600);
  let queue = Promise.resolve();
  let failed = false;
  let closed = false;
  let count = 0;
  return {
    path,
    emit(row: Observation) {
      if (closed || failed || count >= MAX_RECORDS) return;
      count++;
      const line = JSON.stringify(row) + "\n";
      queue = queue.then(async () => { if (!failed) await file.writeFile(line); }).catch(() => { failed = true; });
    },
    async close() {
      if (closed) return;
      closed = true;
      await queue;
      await file.close();
      if (failed) throw new Error("Probe trace write failed");
    },
  };
}

async function manifest(path: string | undefined) {
  if (!path || basename(path) !== "package.json") throw new Error("Explicit package.json path required");
  if ((await stat(path)).size > 64 * 1024) throw new Error("Oversized package manifest");
  return JSON.parse(await readFile(path, "utf8"));
}

export default function attentionObserver(pi: ExtensionAPI) {
  // Off unless deliberately loaded into a synthetic test session. No timers, files or commands at factory time.
  if (process.env.PI_WATCHER_PROBE !== "synthetic-only") return;
  registerSyntheticObserver(pi, {
    piManifest: process.env.PI_WATCHER_PI_PACKAGE_JSON,
    subagentsManifest: process.env.PI_WATCHER_SUBAGENTS_PACKAGE_JSON,
  });
}

// Explicit entry for an isolated harness; never auto-discovers or enables itself.
export function registerSyntheticObserver(pi: ExtensionAPI, options: {
  piManifest?: string;
  subagentsManifest?: string;
  onTrace?: (path: string) => Promise<void>;
}) {
  let projector: ReturnType<typeof createProjector> | undefined;
  let trace: Awaited<ReturnType<typeof privateTrace>> | undefined;
  let unsubscribe: (() => void) | undefined;
  const snapshot = (ctx: ExtensionContext) => {
    if (!projector) return;
    try { projector.entries(ctx.sessionManager.getBranch()); }
    catch { projector.unknown(); }
  };

  pi.on("session_start", async (_event, ctx) => {
    // Session switching can reuse this factory; retire the prior capture/subscription first.
    unsubscribe?.();
    unsubscribe = undefined;
    projector?.stop();
    projector = undefined;
    try { await trace?.close(); }
    catch { if (ctx.hasUI) ctx.ui.notify("Prior watcher capture was incomplete.", "warning"); }
    trace = undefined;
    try {
      // Explicit manifests are a preflight check, not an attestation of the loaded provider instance.
      requireVersions(await manifest(options.piManifest), await manifest(options.subagentsManifest));
      trace = await privateTrace();
      projector = createProjector(ctx.sessionManager.getSessionId(), trace.emit);
      projector.start();
      projector.entries(ctx.sessionManager.getBranch());
      unsubscribe = pi.events.on("pi-intercom:detach-request", d => projector?.hint(d));
      await options.onTrace?.(trace.path);
      if (ctx.hasUI) ctx.ui.notify(`Watcher probe (metadata only): ${trace.path}`, "info");
    } catch {
      unsubscribe?.();
      unsubscribe = undefined;
      projector = undefined;
      await trace?.close().catch(() => {});
      trace = undefined;
      if (ctx.hasUI) ctx.ui.notify("Watcher probe unavailable: check reviewed versions and explicit manifest paths.", "warning");
    }
  });
  pi.on("ui_prompt_start", () => { projector?.prompt(true); });
  pi.on("ui_prompt_end", () => { projector?.prompt(false); });
  pi.on("message_end", event => { projector?.message(event.message); });
  pi.on("tool_execution_end", (event, ctx) => {
    projector?.toolEnd(event);
    // Only this explicitly opted-in session's in-memory branch, never filesystem session discovery.
    snapshot(ctx);
  });
  pi.on("agent_settled", (_event, ctx) => {
    snapshot(ctx);
  });
  pi.on("session_shutdown", async (_event, ctx) => {
    unsubscribe?.();
    unsubscribe = undefined;
    projector?.stop();
    projector = undefined;
    try { await trace?.close(); }
    catch {
      if (ctx.hasUI) ctx.ui.notify("Watcher probe trace could not be saved completely; do not use it as evidence.", "warning");
    } finally { trace = undefined; }
  });
}
