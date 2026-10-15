// Caller-driven read-only recovery. No timers, recursive callbacks or agent-control retries.
const BACKOFF_MS = Object.freeze([1000, 2000, 4000]);
export class SnapshotRecovery {
  #adapter;
  #binding;
  #attempts = 0;
  #nextAt = 0;
  #lastAt = 0;
  constructor(adapter, binding) { this.#adapter = adapter; this.#binding = binding; }
  tick(now) {
    if (!Number.isSafeInteger(now) || now < this.#lastAt) throw new TypeError('Recovery requires monotonic milliseconds');
    this.#lastAt = now;
    const state = this.#adapter.status();
    if (state.awaitingSnapshot) return false;
    if (!state.needsSnapshot) { this.#attempts = 0; this.#nextAt = now; return false; }
    if (this.#attempts >= BACKOFF_MS.length || now < this.#nextAt) return false;
    const delay = BACKOFF_MS[this.#attempts++];
    this.#nextAt = now + delay;
    // Failed emits count toward the budget too; they must not create a tight retry loop.
    try { return this.#binding.refresh(); }
    catch { this.#adapter.unavailable(); return false; }
  }
  // Only an explicit lifecycle reconnect/rebind should reset an exhausted outage budget.
  reset() { this.#attempts = 0; this.#nextAt = this.#lastAt; }
  status() { return Object.freeze({ attempts: this.#attempts, exhausted: this.#attempts >= BACKOFF_MS.length }); }
}
