#!/bin/sh
# Builds and runs the frame recording codec test against the stub viewport header in stubs/,
# then a record-and-replay round trip of the built-in scene. Exits non-zero when either fails.
# Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-frame-record" "$here/test_frame_record.cpp"
"$here/out/test-frame-record"
# The plugin's own recorder captures the built-in scene; replaying it must report every frame
# as the recorder did, and must give the same draws.
"$here/build.sh" "$src" selftest
"$here/out/bench-selftest" selftest "$here/out/selftest.rec" "$here/out/selftest-record.trace" >/dev/null
"$here/out/bench-selftest" replay "$here/out/selftest.rec" "$here/out/selftest-replay.trace" >"$here/out/selftest-replay.txt" || { cat "$here/out/selftest-replay.txt"; echo "record/replay round trip: frames differ"; exit 1; }
tail -2 "$here/out/selftest-replay.txt"
grep -v '^#' "$here/out/selftest-record.trace" >"$here/out/selftest-record.draws"
grep -v '^#' "$here/out/selftest-replay.trace" >"$here/out/selftest-replay.draws"
if cmp -s "$here/out/selftest-record.draws" "$here/out/selftest-replay.draws"; then echo "record/replay round trip: OK ($(wc -l <"$here/out/selftest-record.draws" | tr -d ' ') draws)"; else echo "record/replay round trip: traces differ"; exit 1; fi
