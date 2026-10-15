#!/bin/sh
# Explicit opt-in root launcher; preserves cwd and does not alter global settings.
set -eu
if [ "${PI_SUBAGENT_CHILD:-}" = "1" ]; then
  printf '%s\n' 'This launcher is for an interactive root session, not a child.' >&2
  exit 1
fi
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OBSERVER="$PROJECT/extensions/watcher.ts"
if [ ! -f "$OBSERVER" ]; then
  printf '%s\n' 'Watcher entry point is missing.' >&2
  exit 1
fi
# A compatible provider is optional and explicitly selected. Do not auto-load a
# sibling private checkout or override the user's subagent budget/environment.
if [ -n "${WATCHER_SUBAGENT_PROVIDER:-}" ]; then
  if [ ! -f "$WATCHER_SUBAGENT_PROVIDER" ]; then
    printf '%s\n' 'Configured observation provider is missing.' >&2
    exit 1
  fi
  exec pi --no-extensions -e "$WATCHER_SUBAGENT_PROVIDER" -e "$OBSERVER" "$@"
fi
printf '%s\n' 'Root-only watcher: child telemetry remains unknown without a compatible observation provider.' >&2
exec pi --no-extensions -e "$OBSERVER" "$@"
