# Operating the preview

These instructions assume a device already running separately qualified compatible pet firmware. The source preview does not install firmware. Do not apply them to an unidentified board or erase a device to make it compatible.

## Attention and focus

Automatic focus keeps the first selected session that still needs attention. Manual paging excludes older requests from future automatic focus; a genuinely new request may focus after Settings/contact and the quiet interval end. Completion never changes focus.

Reconnect keeps the current selection if that session still exists. Requests received before restoration cannot steal focus. This focus cutoff does **not** consume never-delivered notification intents: existing eligibility, child grace and durable receipt rules still apply. Already consumed notifications and cached completion states are not replayed. Eligibility still includes settings synchronization: while a settings-enabled host is renegotiating, its output mask is zero and attention flushed in that interval is consumed as suppressed, not queued for a later surprise alert. This is distinct from the focus watermark and also applies on reconnect. New attention after settings synchronization can produce output normally.

A settings-enabled host waits for a fresh interaction witness before moving focus. Firmware without settings negotiation therefore retains basic status/manual navigation but not automatic focus on this host; mixed versions are not a qualified deployment. Selection is not persisted across host restarts.

## Ghost assignment and Settings motion

The new source assigns ghost sprites randomly among the least-used characters in the active roster. Up to six active sessions have distinct sprites; larger rosters spread repeats evenly. Assignments stay stable during paging and reconnect. Removing sessions may reassign excess duplicate copies to restore balance, while preserving the selected session's sprite. Assignments are not persisted across host restarts or removal/rejoin.

Settings scrolling follows vertical drags inside the row viewport and settles to a row over 200 ms. Header, status and Back stay fixed. Animations off/reduced motion retains immediate release-only changes. A new gesture during settling is ignored until the finger lifts and rearms; this prevents toggling the wrong moving row. Host settings updates may cancel an in-progress drag and reset its pose. The owner accepted the observed scrolling experience on the development watch; font clipping, rapid gestures and sustained display timing are not exhaustively qualified.

These features require matching host and firmware versions. Changing source does not update a running server or an installed watch. There is no public firmware deployment workflow in this preview.

## One foreground host

Select the actual USB port yourself. Example placeholders are not device identities:

```sh
export WATCHER_PYTHON="$HOME/venvs/watcher/bin/python"
sh tools/watcher-host.sh --port /dev/cu.usbmodemPORT
```

That Python must contain pyserial 3.5 and run on macOS. Alternatively pass `--python` explicitly. `--device-serial DEVICE_SERIAL` selects one exact USB identity instead of a port; it expects the device's six colon-separated hexadecimal MAC octets, not an arbitrary serial label. Never supply both. These command-line launchers require explicit transport selection and never reset the device. `--completion-chime` is retired and rejected. On the new Afterlife firmware, use watch Settings: Sounds defaults off and Vibration defaults on; both apply to eligible attention and live-completion notifications. Selected Settings and connectivity behaviour has owner acceptance on the development watch; this is not exhaustive physical output qualification.

Ctrl-C stops the host and releases its socket/ledger. Do not start a second bridge, delete a lock to steal ownership, or open a diagnostic serial reader while the host owns the port. A second interrupt or forced kill can leave a stale lock: establish that the exact owner has exited before any manual recovery; never assume the lock is stale just because a launch fails.

## Opt in a Pi session

With Pi installed and compatible:

```sh
sh tools/pi-watched.sh
```

This launches a new root session with the metadata observer and disables other auto-discovered extensions for that launch. It does not change global settings or subagent budgets. Without an explicitly configured compatible `WATCHER_SUBAGENT_PROVIDER` entry point, child telemetry stays unknown. The compatible development provider is not included: portable preview scope is root-only observation, not a publicly reproduced full-child integration. Do not substitute an arbitrary provider and infer compatibility from its package version.

### Optional session-owned server menu

The explicitly loaded `extensions/watch-command.ts` also exposes `/watch server`, `/watch server start`, `/watch server status` and `/watch server stop`. This is a separate convenience path from the foreground shell launcher. It is dormant on extension load and requires an interactive root Pi session; starting it is an explicit action, not automatic setup.

Before starting Pi, set `WATCHER_PYTHON` to the absolute path of a trusted Python containing pyserial 3.5. The command is macOS-only and does not install dependencies or fall back to a development SDK. For example:

```sh
export WATCHER_PYTHON="$HOME/venvs/watcher/bin/python"
# Start your separately configured Pi session with watch-command.ts loaded.
# Then use /watch server start inside Pi.
```

This menu enumerates matching USB descriptors and local private IPv4 addresses, uses the sole device/address or asks you to choose when there are multiple, then requests confirmation and Wi-Fi credentials in hidden fields. It is a wireless-provisioning flow, not the IPC-only quickstart or the explicit-port shell launcher. No device reset is performed. A missing interpreter, wrong pyserial version or failed discovery can produce **Server command unavailable**; check the prerequisites rather than removing guards. Do not use it while a foreground host already owns the watch.

The server belongs to that Pi session. `/watch server stop` asks for confirmation and closes only its owned process; an externally owned server is not adopted. Ending the owning session stops its server. `/watch on` separately opts the session into observation.

### Optional workspace captions

Workspace grouping/captions are **OFF by default** in both public entry points. To opt in, set `WATCHER_SPACE_IDENTITY=1` before launching Pi. This additionally requires the optional `herdr` workspace tool and its `HERDR_ENV=1`, `HERDR_PANE_ID`, `HERDR_SOCKET_PATH` environment. When opted in, the observer periodically invokes `herdr pane current --current` and `herdr workspace get` via `PATH` (at most once per five seconds) and sends a bounded sanitized workspace label plus opaque workspace identity to the bridge/watch. Use only a trusted `herdr` executable on PATH. The tool is neither bundled nor installed automatically; its public availability is not a preview prerequisite. Leave the variable unset or set it to `0` for generated session labels and no workspace-tool invocation. Captions may still be sensitive after sanitization.

To use the command entry point in a separately configured Pi session, explicitly load `extensions/watch-command.ts` through Pi's extension loader, then use `/watch on`, `/watch status`, or `/watch off`. Do not load both watcher entry points into one session. The extension is dormant until opted in. Existing sessions must reload/restart intentionally to pick up source changes; changing files does not update already imported modules.

## Diagnose without opening USB

```sh
node tools/watcher-status.mjs
node tools/watcher-status.mjs --json
```

`--state-dir DIRECTORY` selects the same private directory passed to the host. `/watch status` additionally reports whether this session is registered/selected.

| Status | Meaning / next action |
|---|---|
| Host status unavailable | Host absent, old host without inspection, wrong directory, unsafe socket, timeout or capacity limit. Check the foreground process first. |
| IPC-only | Intentional host-only mode; no USB/LAN delivery is possible. |
| Waiting for device status | The bridge has not received fresh valid device status. Serial-open success alone is insufficient. |
| Handshaking | Device status arrived, but fresh two-way state-frame acceptance is not established. |
| Device link confirmed | Recent valid device status and advancing accepted host frames; not proof of what a person sees/hears. |
| Another session selected | Device connectivity is separate from selection; page to the desired session. |
| Child telemetry unknown | Missing compatible child observation, not a USB error and not proof that no children are active. |

Pre-link USB probing is capped at six unanswered Q1 queries, even across helper restarts, until valid status arrives. It never resets the badge. Serial-helper failures retain the hub and retry the explicitly selected transport with backoff; a silent but open port is not continually reopened just because it is silent.

### Known recovery case

A post-boot link once delivered zero bytes while the watch still rotated normally. Stopping the host, unplugging/replugging USB while leaving the watch powered, and restarting the host restored two-way traffic. This is a recorded workaround, **not an established root-cause fix**. Do not progress from this symptom to erase/reflash. If it recurs, retain content-free counters and follow the preservation boundary.

## LAN without a credential file

Use a private IPv4 address belonging to this Mac on the intended LAN, a 2.4 GHz network the badge can join, and USB for initial provisioning:

```sh
python3 tools/watcher-lan.py --address 192.168.1.10 --port /dev/cu.usbmodemPORT --python "$WATCHER_PYTHON"
```

The address above is synthetic: replace it with your Mac's actual address. The older host `--lan-config PRIVATE_FILE` path remains available with strict owner/mode/schema checks, but stores credentials in a file; it is distinct from the recommended stdin launcher and is not used by these instructions. The launcher asks for the network name and a hidden password in the terminal. It passes bounded JSON over stdin, not command-line secrets or a config file. It refuses noninteractive use and does not inspect saved credentials. Python/JavaScript memory copies cannot be guaranteed erased.

Wait for RAM pairing, an IP and a TLS peer. USB may remain the active transport until unplugged; **`lanLive:true` after unplugging USB** is the relevant two-way LAN observation, not TLS connection alone. A Mac can use 5 GHz while the badge uses 2.4 GHz if the network permits local peer communication; native 5 GHz on the badge is not supported.

Pairing/credentials are RAM-only. A host restart creates a new pairing; badge reboot also loses its provisioning. Reconnect USB and explicitly reprovision when necessary. This source has simulated recovery tests and initial physical connectivity evidence, not exhaustive wireless stability qualification.

## Watcher-only source

Voice capture, transcription and reply submission are removed. `/watch voice …`, `--voice-helper`, `--voice-sha256` and the former voice build profiles are rejected. No user-installed helper, recording or macOS permission is removed. `/watcher-reconnect` reconnects metadata only; it does not reset the watch or resolve attention.

Releasing the former both-button voice chord after 600–1199 ms does nothing, rather than acknowledging attention. Short both-button ACK and the long mute gesture remain. Updating source does not update an already running host, extension or installed watch; mixed-version deployment is not qualified.

The host also accepts `--reduced-motion`. Its `--audio-init-probe` and `--audio-chime-probe` switches are developer-only supervised diagnostic paths, not ordinary preview setup; do not invoke them against a running owner device or treat their presence as permission to play/record audio. Public build helpers keep diagnostic modes OFF.

Device installation and physical output qualification require a separately authorized release window. See the capability/limitation table in the README.
