#!/bin/sh
# Real compile check: builds the plugin in DFHack's build container against the real headers.
# Usage: harness/compile.sh <plugin dir>
set -eu
# Checked before the exit trap is installed: bash 3.2 reports the trap's status, not the
# expansion error's, when an EXIT trap is set.
: "${DFHACK_SRC:?set DFHACK_SRC to a DFHack checkout with build/linux configured}"
src=$(cd "$1" && pwd); log=$(mktemp)
trap 'rm -f "$log"' EXIT
docker run --rm -v "$DFHACK_SRC":/src -v "$DFHACK_SRC/build/linux":/src/build-linux \
  -v "$src":/src/plugins/external/smooth-movement ghcr.io/dfhack/build-env:master \
  ninja -C /src/build-linux plugins/external/smooth-movement/smooth-movement.plug.so >"$log" 2>&1 || { tail -20 "$log"; exit 1; }
tail -5 "$log"
