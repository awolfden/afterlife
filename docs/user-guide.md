# Meet your desk companion

Afterlife displays Pi session state on an M5Stack StopWatch. It does not run the agent or replace Pi's terminal. This guide describes the matching Afterlife firmware and host; the public source preview does not install that firmware.

## Six ghosts, one roster

![Six original ghost characters](images/ghost-family.svg)

For up to six active sessions, each gets a different character. The host randomly picks among the least-used characters; for seven to sixteen sessions, repeats are balanced. A character is not a status indicator: the same ghost can be working, waiting or idle.

Assignments stay stable while paging or reconnecting within the same host controller. Removing sessions can rebalance duplicate characters without changing the selected session's character. A host restart or removal/rejoin can assign a new character. Session labels in examples are synthetic; workspace captions are separately opt-in.

## Controls

With compatible firmware running, outside Settings:

| Action | Control |
|---|---|
| Previous session | Short yellow A press, or page with a horizontal swipe. |
| Next session | Short blue B press, or page with a horizontal swipe. |
| Acknowledge the displayed alert | Short A+B chord; release before 600 ms. This silences/acknowledges, not resolves or approves. |
| Toggle master mute | Hold A+B for at least 1.2 seconds. |
| Cycle brightness | Hold blue B for at least 1.8 seconds. |
| Exit/restart the companion | Hold yellow A for at least 4 seconds. This requests a restart/return to the separately preserved application on the development device; the destination depends on the device's boot configuration. It is not an installer or factory restore. |
| Open Settings | Tap **Settings** near the bottom of the display. |

The retired 600–1199 ms A+B chord does nothing; it does not start voice capture. Touch is enabled in the Afterlife profile, not by default in every generic firmware build. Buttons and touch are fenced against stale display state, so an input can be ignored instead of applied to the wrong session.

## Settings

![Settings layout illustration](images/settings.svg)

*Illustration, not a photograph. Labels and default values match the firmware; typography is approximate.*

Tap a row to change it. Drag vertically within the rows to scroll; the list moves by at most one row per gesture and settles over 200 ms. **Settings**, the save status and **Back** stay fixed. Tap **Back** to return to the ghost.

| Setting | Default | Effect |
|---|---|---|
| Vibration | On | Enables eligible notification vibration, subject to master mute and safety/readiness checks. |
| Sounds | Off | Enables eligible notification sounds. No microphone or voice mode. |
| Animations | On | Enables motion. Off uses immediate, release-only Settings scrolling. A host-forced reduced-motion setting can still override this. |
| Cycle sessions | Off | Automatically cycles eligible sessions on a ten-second interval; interaction and attention can pause automatic movement. |
| Brightness | Medium | Cycles Low / Medium / High (80 / 160 / 240). |

### Where preferences live

- **Saved on Mac:** the host confirmed the saved values.
- **Saving...:** a change is awaiting confirmation.
- **Not saved on Mac:** persistence is unavailable or confirmation has not completed; do not assume the value will survive reconnect.
- **Temporary until reconnect:** local RAM-only values. Reconnect restores the Mac's settings rather than uploading these offline choices.

No preference write to watch flash is needed. Changing either output setting does not itself emit an alert. A newly synchronized host does not replay alerts suppressed while outputs were disabled.

### Scrolling deliberately

Let a settling row finish before starting another gesture. Touches begun during settling are ignored until you lift and rearm, so a moving row is not accidentally toggled. A host settings update can cancel an in-progress drag. There is no inertial scrolling or overscroll. Physical smoothness has owner acceptance on the development watch, not exhaustive timing or all-gesture qualification.

## Connection and attention

The disconnected display uses the original Afterlife wordmark and mural with a **Connect to Pi** prompt. A connected host with no registered sessions shows **Connecting to Pi** and a small animated ghost; this is a waiting indicator, not measured progress.

A registered session can be active without needing attention. When a request arrives, the first selected session that still needs attention keeps focus. Manual paging prevents old requests from pulling you back; genuinely new attention may focus later. Completion does not steal focus. Settings/contact pauses automatic focus movement.

Unknown child telemetry means exactly that—not that no children exist. The public launcher supports root-only observation without the separate compatible child provider.

## If something looks wrong

1. Check the foreground host and `node tools/watcher-status.mjs`.
2. Distinguish **IPC-only**, **Waiting for device status**, **Handshaking** and **Device link confirmed**.
3. If Settings says **Host update required**, use matching reviewed source/firmware; do not bypass synchronization guards.
4. Do not run a second serial reader, delete an ownership lock, or erase/reflash as troubleshooting.

See [operations and recovery](operations.md), [preservation](preservation.md), and [developer prerequisites](developer.md). Report reproducible issues with synthetic examples and content-free status—not flash backups, pairing material or private session text.
