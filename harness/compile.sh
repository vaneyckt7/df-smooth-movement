#!/bin/sh
# Real compile check: builds the plugin in DFHack's build container against the real headers.
# Usage: harness/compile.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd)
docker run --rm -v "${DFHACK_SRC:?set DFHACK_SRC to a DFHack checkout with build-linux configured}":/src -v "$DFHACK_SRC/build/linux":/src/build-linux \
  -v "$src":/src/plugins/external/smooth-movement ghcr.io/dfhack/build-env:master \
  ninja -C /src/build-linux plugins/external/smooth-movement/smooth-movement.plug.so 2>&1 | tail -5
