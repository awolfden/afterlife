#!/bin/sh
# Explicit foreground bridge; no discovery, installs, reset or flashing.
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "${PI_SUBAGENT_CHILD:-}" = "1" ]; then
  printf '%s\n' 'Start the host from an interactive root terminal, not an agent child.' >&2
  exit 1
fi
if [ "${1:-}" = "--help" ]; then
  printf '%s\n' 'Usage: watcher-host.sh (--port PORT | --device-serial SERIAL) [--python PYTHON]' '       watcher-host.sh --ipc-only' 'Options: --state-dir DIRECTORY, --lan-config PRIVATE_FILE. Use watch Settings for Sounds and Vibration; --completion-chime is retired.' 'Prefer watcher-lan.py for interactive RAM-only LAN setup without a credential file.' 'WATCHER_PYTHON may select a macOS Python with pyserial 3.5. No installs are performed.' 'Foreground process; Ctrl-C closes its socket and ledger cleanly.'
  exit 0
fi
# IPC needs no Python. An explicit CLI choice is validated by the host parser.
for arg in "$@"; do
  case "$arg" in --ipc-only|--python) exec node "$PROJECT/host/run.mjs" "$@" ;; esac
done
# Do not silently choose an SDK or globally installed Python.
PYTHON="${WATCHER_PYTHON:-}"
if [ ! -x "$PYTHON" ]; then
  printf '%s\n' 'Set WATCHER_PYTHON or pass --python with a macOS Python containing pyserial 3.5. No installation attempted.' >&2
  exit 1
fi
exec node "$PROJECT/host/run.mjs" --python "$PYTHON" "$@"
