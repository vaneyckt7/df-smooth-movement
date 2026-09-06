#!/bin/sh
# Builds and runs the upstream unit test executable (and the manager benchmark when present),
# the frame recording codec test, and a record-and-replay round trip of the synthetic scene.
# Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
c++ -std=c++17 -O2 -Wall -Wextra -I"$src" -o "$here/out/test" "$src/test_visual_animation_manager.cpp" && "$here/out/test" && echo "unit tests: OK"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-frame-record" "$here/test_frame_record.cpp" && "$here/out/test-frame-record"
# The plugin's own recorder captures the synthetic run; replaying it must give the same draws.
CXXFLAGS=-DHARNESS_STATS HARNESS_RECORD="$here/out/roundtrip.rec" "$here/run.sh" "$src" roundtrip-record >/dev/null
"$here/out/bench-roundtrip-record" replay "$here/out/roundtrip.rec" "$here/out/roundtrip-replay.trace" | tail -1
grep -v '^#' "$here/out/roundtrip-record.trace" >"$here/out/roundtrip-record.draws"
grep -v '^#' "$here/out/roundtrip-replay.trace" >"$here/out/roundtrip-replay.draws"
if cmp -s "$here/out/roundtrip-record.draws" "$here/out/roundtrip-replay.draws"; then echo "record/replay round trip: OK"; else echo "record/replay round trip: traces differ"; exit 1; fi
if [ -f "$src/bench_visual_animation_manager.cpp" ]; then
  c++ -std=c++17 -O2 -Wall -Wextra -I"$src" -o "$here/out/mbench" "$src/bench_visual_animation_manager.cpp" && "$here/out/mbench"
fi
