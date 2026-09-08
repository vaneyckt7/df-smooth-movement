#!/bin/sh
# Builds and runs the frame recording codec test and the tile repaint test against the stub
# viewport header in stubs/, then replays every recording in recordings/ and requires that
# every frame's draws digest to what expected/<name>.digest holds. Exits non-zero when any
# fails. The game's own repaint count from the recording's self-check is printed for
# information: it matches only for the plugin version that made the recording.
# Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-frame-record" "$here/test_frame_record.cpp"
"$here/out/test-frame-record"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-tile-repaint" \
    "$here/test_tile_repaint.cpp"
"$here/out/test-tile-repaint"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-sprite-proxies" \
    "$here/test_sprite_proxies.cpp"
"$here/out/test-sprite-proxies"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-free-camera" \
    "$here/test_free_camera.cpp"
"$here/out/test-free-camera"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-view-context" \
    "$here/test_view_context.cpp"
"$here/out/test-view-context"
"$here/build.sh" "$src" test
status=0
for rec in "$here"/recordings/*.rec; do
	name=$(basename "$rec" .rec)
	trace="$here/out/test-$name.trace"
	rc=0; "$here/out/bench-test" replay "$rec" "$trace" >"$here/out/test-$name.txt" || rc=$?
	if [ "$rc" -gt 1 ]; then
		cat "$here/out/test-$name.txt"; echo "replay of $name: exited $rc"; exit 1
	fi
	"$here/digest.py" "$trace" >"$here/out/test-$name.digest"
	if cmp -s "$here/out/test-$name.digest" "$here/expected/$name.digest"; then
		game=$(sed -n 's/^game: *//p' "$here/out/test-$name.txt")
		replay=$(sed -n 's/^replay: *//p' "$here/out/test-$name.txt")
		echo "replay of $name: OK, digests match; game $game; replay $replay"
	else
		echo "replay of $name: FAIL, frames differ from expected/$name.digest:"
		diff "$here/expected/$name.digest" "$here/out/test-$name.digest" | head -20
		echo "  (to accept: cp $here/out/test-$name.digest $here/expected/$name.digest)"
		status=1
	fi
done
exit $status
