# Changelog

## 0.1.0-dev.1 — developer-preview candidate (unpublished)

- Afterlife disconnected wordmark/mural and connected-but-empty waiting animation, using original artwork.
- Six original ghost characters; random non-repeating assignment for up to six sessions and balanced reuse for larger rosters, stable during paging and reconnect.
- On-watch Settings for vibration, sounds, animations, ten-second session cycling and brightness. Preferences persist on the Mac; offline changes are temporary.
- Drag-following Settings scrolling with a 200 ms row settle and reduced-motion fallback; fixed header, save status and Back control.
- Watcher-only operation: microphone acquisition, transcription and reply submission removed. Retired voice entry points reject requests rather than falling back to another action.
- Attention focus respects manual navigation and reconnect without consuming never-delivered notifications solely to prevent focus changes. Existing output eligibility and durable receipt rules remain separate.
- Private-socket host inspection, truthful per-session status, bounded USB probing and stale-display fences for touch and buttons.
- Explicit Python/provider choices and root-only launch fallback. The optional session-owned `/watch server` menu requires `WATCHER_PYTHON` and checks pyserial 3.5 before USB descriptor discovery; no private SDK-path fallback. Workspace captions require separate opt-in. RAM-only LAN provisioning does not put passwords on the command line.
- Illustrated documentation, explicit source allowlist, dependency notices and offline tests.

Selected USB connectivity, Settings and polish behaviour has owner acceptance on a development StopWatch/macOS setup. This is not clean-machine reproduction, exhaustive touch/output qualification, sustained LAN qualification, or public installation support.

No public firmware installer, binaries, OTA, recovery images, direct Claude Code/Codex adapters or request-context display are included. The watcher does not answer native Pi dialogs or approve agent actions.
