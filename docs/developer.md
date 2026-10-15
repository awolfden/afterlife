# Developer guide

This is a source developer preview, not a firmware installation kit. See [preservation](preservation.md) before connecting or building for a device.

## Prerequisites

- Node.js **24.14.1** is the tested host/test runtime. There are no npm dependencies or install hooks for the portable suite.
- Python **3.11+** for packaging/build helpers and Python tests.
- Clang with AddressSanitizer/UndefinedBehaviorSanitizer and CMake for native/policy checks. The development environment used Apple Clang 21 and CMake 3.30.2. `WATCHER_CMAKE` may select an existing CMake executable.
- USB operation additionally requires **macOS and pyserial 3.5** in an explicitly selected Python. The transport intentionally rejects other platforms/versions; do not remove its modem-control guards as a compatibility shortcut.
- Pi observation targets the **0.87.0** event interfaces used during development. Retained `host/pi-voice-*-pins.json` files are historical evidence, not an active capability. The preview does not install or modify Pi.

Obtain dependencies explicitly from their upstream sources and review their terms. Nothing here reads Keychain, installs a toolchain or downloads packages automatically. Use a dedicated environment rather than altering a global Python installation.

## Tests and local development

From the source root:

```sh
npm test
# Narrow iteration:
node --test tests/host-status.test.mjs tests/watch-command.test.mjs
node --test tests/touch-swipe.test.mjs tests/usb-controller.test.mjs
python3 tests/stage-source-test.py
```

The preview suite uses explicit test entries in `release/tests.json`; it is not a glob over historical probes. Tests create temporary files, native executables, private Unix sockets and simulated/loopback transports. They do not open a physical serial device, record audio, request speech permission or invoke a model. External-runtime integration and physical radio/audio qualification are separate.

For host development without hardware:

```sh
sh tools/watcher-host.sh --ipc-only
# Another terminal:
node tools/watcher-status.mjs --json
```

This deliberately reports `ipc-only`, not device success. Stop with Ctrl-C before changing modes. The default state directory is `~/.local/share/pi-watcher-pet/run`; tests use separate temporary directories.

## Architecture

1. `extensions/watcher.ts` explicitly starts root observation; `extensions/watch-command.ts` exposes dormant metadata-only `/watch`.
2. `host/pi-observer.mjs` maps authoritative lifecycle/prompt events. Supervisor/activity bindings consume compatible observation APIs; missing evidence remains unknown.
3. `SourceClient` sends bounded metadata over the private hub socket. Reconnect replays metadata only.
4. `SessionHub` owns registrations. `AttentionModel` owns selection, grouping and pending versus unseen. Per-controller `GhostContext`/`GhostRoster` assigns stable active-roster sprites, unique for up to six sessions. `DeliveryLedger` records bounded notification/ACK bookkeeping, not request bodies.
5. `runHost` is the single foreground transport owner. `UsbController` checks device generation, frame history, freshness and event authority. The separate inspection request returns only allowlisted connection metadata.
6. Firmware handles original RLE art, optional G1 group rendering, buttons, IMU rotation, optional touch, and feedback. Output-only codec/IOE writes remain under their established owner; touch does not create a second I2C driver.

### Protocol boundaries

- **Q1:** status query only; pre-link USB queries stop after six unanswered attempts until valid status arrives.
- **W1 / D2:** host state and device status/receipts. Local IPC or serial-open success is not a two-way handshake.
- **G1:** bounded display metadata (hue, rhythm, caption, counts). It cannot independently refresh liveness or authorize feedback.
- **Watcher-only:** no microphone RX allocation, capture transport, transcription or reply capability. The mandatory render-authority pre-loss hook remains explicitly non-null, with no active voice consumer.
- **Inspection:** a first-message `inspect` request on its own private hub connection; fixed-schema response, bounded deadline, no session registration or unsolicited messages to existing clients.

Do not reinterpret ACK as source resolution, session navigation as ACK, completion as absence of child work, or generic input as tool authorization. The host rejects paging against a stale selection epoch, including away/back changes.

## Firmware builds — never flash these outputs automatically

Preprovision the exact, clean Git checkouts below, including the SDK's required submodules/tools:

- ESP-IDF 5.5.4: `735507283d5b2f9fb363a1901172dbd9e847945d`.
- M5GFX 0.2.26: `729297d6e3d657ddc1ec5189bac2f2ea68828085`.
- Vendored codec/IMU source pins and licenses are checked by their component CMake files.

The project refuses dirty or differently pinned SDK/GFX trees and disables the IDF component manager. Export your selected SDK normally, then:

```sh
export WATCHER_M5GFX_PATH="$HOME/deps/M5GFX"
# First source your selected ESP-IDF export.sh using its documented setup.
python3 tools/build-firmware.py --profile usb --build-dir "$HOME/builds/watcher-usb-preview"
# For later incremental rebuilds of that same profile:
cmake --build "$HOME/builds/watcher-usb-preview"
```

The parent build directory must exist; the selected profile directory must be new. Generated SDK configuration stays outside the source tree. Available profiles are `usb`, `lan` and `afterlife`; former voice profiles are rejected. `--touch` explicitly enables swipe support for generic profiles. Generic profiles keep haptics and diagnostics OFF. The `afterlife` profile preserves retained paging, ghost family, Afterlife, measured touch diagnostics, LAN, orientation and output feedback, but excludes capture and acceptance/audio-probe modes. It requires `--accept-software-cutoff` to acknowledge software-only motor cutoff; this is build-only, not physical qualification or flash authorization. Sound playback is controlled by watch Settings (Sounds defaults off); the retired `--completion-chime` option is rejected. Do not reuse an old capture-enabled CMake cache: capture ON is rejected.

LAN profiles add the reviewed RAM-only radio overlay. Do not reuse a cached configuration from a different profile. The partition CSV is a **build reference**, not a redistributable flash plan or confirmation of another board's contents.

All resulting binaries, bootloaders and flash arguments are local build products only. Do not run generic flash/erase commands. Current private operational updaters are excluded from the preview because they are not a public compatibility/install path.

## Documentation visuals

The README uses the original disconnected SVG and a six-character artwork lineup. The Settings image is a labeled layout illustration, not a device photograph or framebuffer capture. No private sessions, device identifiers, browser profiles or screenshots of terminal contents are included. See [visual provenance](images/README.md).

## Artwork

Generated original art and its manifest are included, so ordinary builds need no browser. `ghost-assets` and native renderer checks validate source/header/frame consistency. Do not change manifest hashes just to silence a mismatch. Browser regeneration uses an isolated profile but currently depends on the documented local Chrome harness; it is optional, not required for clean source testing/building. Visual equivalence and physical display quality remain separate checks.
