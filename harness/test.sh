#!/bin/sh
# Builds and runs the unit tests (the animation manager, recording codec, tile repaint,
# sprite proxy, free camera, view context and console command tests) against the stub
# headers in stubs/, then
# replays every recording in recordings/ and requires that every frame's draws digest to
# what expected/<name>.digest holds, and that recinfo.py reads the same number of frames.
# Exits non-zero when any fails. The game's own repaint count from the recording's
# self-check is printed for information: it matches only for the plugin version that made
# the recording.
# Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
# The animation manager test lives with the plugin sources, as the CMake target
# smooth-movement-test, which nothing else builds; it needs no stub.
c++ -std=c++17 -O2 -Wall -Wextra -I"$src" -o "$here/out/test-visual-animation-manager" \
    "$src/test_visual_animation_manager.cpp"
"$here/out/test-visual-animation-manager"
# Builds and runs one harness test: harness/test_<name>.cpp against the stubs.
run_test() {
	c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-$1" \
	    "$here/test_$(echo "$1" | tr - _).cpp"
	"$here/out/test-$1"
}
run_test frame-record
# recinfo.py must read a recording in the current format: the fixtures are older.
"$here/out/test-frame-record" "$here/out/test-current.rec"
fields='.* step=\([0-9]*\) sim=\(-*[0-9]*\) hop=\([a-z]*\) amount=\([0-9.]*\)'
fields="$fields"' mult=\([0-9.,]*\) hops=\([0-9]\) .*'
sample=$("$here/recinfo.py" "$here/out/test-current.rec" |
	sed -n "s/$fields/\1,\2,\3,\4,\5,\6/p;s/^frames //p" | tr '\n' ' ')
expected="250,1234567,off,0.15,1,2,3,1 250,-1,off,0.15,1,2,3,1 250,1234569,on,0.15,1,2,3,1 3 "
if [ "$sample" != "$expected" ]; then
	echo "recinfo.py misreads the current format: $sample"; exit 1
fi
run_test tile-repaint
run_test sprite-proxies
run_test free-camera
run_test view-context
run_test plugin-commands
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
	# recinfo.py walks the file on its own; it must agree with the replay on the frame count.
	frames=$("$here/recinfo.py" "$rec" 0 -1 | sed -n 's/^frames //p')
	replayed=$(wc -l <"$here/out/test-$name.digest" | tr -d ' ')
	if [ "$frames" != "$replayed" ]; then
		echo "recinfo.py of $name: $frames frames, the replay $replayed"; exit 1
	fi
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
