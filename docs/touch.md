# Experimental touch swipes

`WATCHER_TOUCH_SWIPE` defaults to **OFF**. A separately reviewed private qualification image is installed; it has bounded physical evidence for upright interior swipes, not general hardware qualification. The earlier frozen source-preview archive remains unchanged and does not contain the later private qualification changes. Buttons remain the fallback.

## Observed acceptance and limits

One upright interior left swipe paged next across a three-session wrap; one right swipe paged previous back to the selected test page. Each produced one recognized event and an owner-confirmed page change, with zero rejected samples or setup/I/O errors. Earlier two-session interior swipes also worked. Temporary test sources were removed afterward, leaving the two real sessions.

Five-session readiness still failed: a 111 ms sample gap exceeded the unchanged 100 ms guard and the recognizer was unarmed. No swipe was requested in that state. The successful three-session round ended with a 100 ms gap, so it does not prove sustained reliability or broad timing margin. The renderer optimization preserves exact ordered spans in 45,000 differential cases; its desktop CPU savings are not MCU frame-time guarantees. Other rotations, repeatability, extreme edges, sleep/wake and combined load remain unqualified. The intermittent stage63 initialization fault remains unresolved.

## Contract

- A left swipe pages next; a right swipe pages previous, in the committed display orientation.
- An event occurs only after confirmed release. Touch never acknowledges, mutes, starts recording or submits input.
- At least three contact samples, 50 recognition units of travel, 80–800 ms duration, limited vertical drift/reversal and no sample gap above 100 ms are required.
- Two successful released samples rearm after boot, error or cancellation. A read failure is never treated as release.
- Held buttons, pending button delivery, link expiry, rotation and displayed-identity changes suppress or cancel the gesture.
- The event retains its original display-frame authority. Host selection epochs reject stale paging, including selection away and back; this also hardens physical button paging.

The raw CST820-family packet is decoded conservatively through the existing shared I2C owner. One operation per eligible poll, a 20 ms normal interval and 250 ms error backoff bound work/frequency. They do **not** guarantee wall-clock latency: the underlying LGFX mutex can wait indefinitely.

## Conservative measured-scale profile

Legacy decoding is unchanged. The separate default-OFF `WATCHER_TOUCH_MEASURED_SCALE` opt-in requires touch navigation. It admits only the observation-limited working raw envelope X 0–460 / Y 0–461, then applies `floor(raw/2)` before the unchanged orientation and gesture logic. These are conservative interior-test bounds, not calibrated controller maxima or measured physical endpoints. Unknown edge values remain errors, never clamps or releases. Scaling is fixed, not fitted to sampled extrema. The 50-unit travel threshold therefore refers to recognition coordinates, not universally to 50 raw controller units.

Release quarantine, duration, sample count, drift/reversal, orientation and authority rules remain unchanged. This is a private qualification profile, not evidence to enable touch by default.

## Qualification requirements

Before enabling by default or installing for unattended use, physically verify:

1. Actual controller identity, register behavior, address, release packets and 100 kHz behavior. The exact controller suffix is not established.
2. Left/right mapping in all four orientations, including rotation during contact.
3. Taps, jitter, held-at-start contact, malformed/error samples, sleep/wake and repeated swipes.
4. Multi-session paging across four-row boundaries, stale events, a disappearing session and selection away/back.
5. Button priority, inert retired voice chord, link transitions, feedback and combined-load latency/watchdog behavior.
6. Unchanged init(), chooser, calibration and personal storage after a separately reviewed app-only installation.

The initial qualification image disabled touch until reboot after any setup failure; physical swiping did not work, while a deliberate blue-button press did switch sessions. That setup policy is a proven availability flaw, but it is not yet the established cause of the observed failure.

The subsequently installed recovery change retains the failed setup stage and retries no sooner than 250 ms later, at most one operation per eligible poll. No setup failure occurred in the recent measured rounds, so those rounds did not exercise live setup recovery. Successful stages are not replayed. Invalid/backward/overflowing time still permanently disables the sampler. Errors never become release samples; successful setup does not arm a gesture. If the controller cannot supply two genuine released samples, swiping must remain unavailable. Sleep/wake, release reporting and actual polling gaps still require physical qualification.

## Temporary local diagnostics

`WATCHER_TOUCH_DIAGNOSTICS` defaults OFF and requires touch navigation. It draws two small counter lines locally. It adds no bus reads, wire messages, persistent logs, raw coordinates or session content. Counters saturate in RAM; displayed counts cap at 999.

- `T`: setup stage 0–3, ready 4, clock-halted 9. `F`: total setup failures.
- `I`: read I/O failures. `B`: packets rejected by our decoder (not proof the controller sent intrinsically malformed data). `D`: latest read-poll gap in milliseconds, capped at 999 (not a latency guarantee).
- `N`: finger-count byte greater than one. `H` / `V`: raw X / Y above 233 in the legacy profile, or above 460 / 461 respectively in the measured-scale profile. Count rejection takes precedence; one packet can increment both H and V but B only once. These are saturating aggregate counts, not raw-coordinate logs. Diagnostics do not change decoder acceptance, retry or gesture rules.
- `C` / `U`: valid contact / released samples. `A` / `X`: armed / tracking.
- `G`: recognizer guard currently permits polling. `S`: recognized swipes (not proof of host acceptance).
- `Z`: hexadecimal suppression bits: 01 link, 02 pending button event, 04 physical button/action, 08 displayed identity/rotation, 10 reserved (former voice bit; unused), 20 exit, 40 fewer than two sessions, 80 other guard rejection. These are diagnostic observations, not authority overrides.

The observed acceptance above does not qualify every touch behavior. Do not weaken release quarantine or infer touch health from a successful USB handshake.

## Opt-in one-shot USB counter reads (working candidate)

`WATCHER_TOUCH_USB_DIAGNOSTICS` defaults OFF and requires both touch and local diagnostics. The host also defaults OFF: explicitly enable `runHost({touchDiagnostics:true,...})` or the direct host CLI's `--touch-diagnostics`. Then `node tools/watcher-touch-status.mjs` requests one fresh numeric snapshot through the already-running private hub socket. It does not open the serial port, start a host, register a session or retry automatically. Existing `/watch`/host inspection is unchanged.

The separate first-message `touch-inspect` IPC endpoint accepts no arbitrary command or session identifier. The opted-in host sends a CRC-protected `TQ1` request only on a confirmed, fresh USB link; firmware may return `TD1` with the matching boot/request identity and counters. There is one pending request, no queue/cache, at most one issued request per second, a 1.5-second host deadline and a one-second firmware pending lifetime. Replies do not renew link health or acknowledge/page/resolve anything. Unsupported firmware, suppression, missed/partial writes or stale transport yield unavailable, never an inferred zero-counter result. Concurrent or over-rate reads yield busy; a non-opted-in host yields disabled.

Firmware uses the existing USB driver only, after buttons/render, with zero-timeout TX-idle admission and one nonblocking write attempt. Reports are suppressed during stale/no rendered link, LAN selection or exit. No new I2C transactions, raw coordinates, PCM, session content, persistent device storage, spontaneous reports or background streams are introduced. The host rejects unmatched, stale-epoch/boot, out-of-frame-range and malformed replies; no diagnostic packet establishes delivery authority. Short writes are not replayed.

Snapshots include uncapped uint16 counters (saturating at65535), last/max read-poll gaps, last touch guard/suppression state, setup stage, boot and frame IDs. Last guard fields may precede the render completed in that loop and are diagnostic observations, not action authority. Max gaps can include deliberate suppression or error backoff; they are not bus-latency measurements. Counter differences require same-boot continuity and attention to saturation. Counter snapshots contain no raw coordinates and do not change decoder mapping.

## Separately armed bounded raw-range diagnostics

`WATCHER_TOUCH_RANGE_DIAGNOSTICS` defaults OFF and requires USB touch diagnostics. The host must also explicitly enable `touchRanges:true` with `touchDiagnostics:true`. `node tools/watcher-touch-range.mjs` reads the current summary only; `--arm` explicitly opens a measurement window. These are diagnostic commands, not navigation or calibration actions. Do not automatically retry or rearm an unavailable request.

A window waits at most 120 seconds for contact, then collects at most one second, 50 samples or until the first genuine release. At most 10 arm admissions are allowed per boot, including windows that later expire or are cancelled. Only RAM extrema/sample/state metadata is retained, not point history; observation uses the existing I2C reads. Read/packet errors cancel active observation; context invalidation also clears completed summaries. No extra bus transactions or persistent device logging are introduced.

Range summaries remain raw, before normalization. A completed measurement requires matching boot/window identity, complete state and positive sample count. Zero extrema with no samples are unmeasured, not coordinates; unavailable never means zero. Counter reads and range requests share the one-pending/rate gate and remain USB-only and opt-in.
