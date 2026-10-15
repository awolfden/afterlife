import { SUPERVISOR_EVENTS } from './supervisor-adapter.mjs';

/** Bind only the public read-only bus. No tools, files, transport, background polling or retries. */
export function bindSupervisorObservation(bus, adapter, { timeoutMs = 1000 } = {}) {
  if (!Number.isInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 5000) throw new TypeError('Invalid snapshot timeout');
  if (typeof bus?.on !== 'function' || typeof bus?.emit !== 'function') throw new TypeError('Unsupported observation bus');
  let disposed = false, pending = null, timer;
  const subscriptions = [];
  const clear = () => { clearTimeout(timer); timer = undefined; pending = null; };
  const fail = () => { clear(); adapter.unavailable(); };
  const dispose = () => {
    if (disposed) return;
    disposed = true; clear();
    for (const off of subscriptions.splice(0)) { try { off(); } catch { /* Best-effort listener cleanup. */ } }
    adapter.unavailable();
  };
  const refresh = () => {
    if (disposed) return false;
    if (pending !== null && !adapter.status().awaitingSnapshot) clear();
    if (pending !== null) return false;
    const query = adapter.request(); pending = query.requestId;
    timer = setTimeout(() => { if (!disposed && pending === query.requestId) fail(); }, timeoutMs);
    timer.unref?.();
    try { bus.emit(SUPERVISOR_EVENTS.request, query); }
    catch { fail(); return false; }
    return true;
  };
  try {
    for (const [name, handler] of [
      [SUPERVISOR_EVENTS.response, value => {
        if (disposed || value?.requestId !== pending || pending === null) return;
        clear();
        try { adapter.snapshot(value); } catch { adapter.unavailable(); }
      }],
      [SUPERVISOR_EVENTS.update, value => {
        if (disposed) return;
        try { adapter.update(value); } catch { adapter.unavailable(); }
      }],
    ]) {
      const off = bus.on(name, handler);
      if (typeof off !== 'function') throw new TypeError('Observation bus must return unsubscribe functions');
      subscriptions.push(off);
    }
    refresh();
  } catch (error) { dispose(); throw error; }
  return Object.freeze({ refresh, dispose });
}
