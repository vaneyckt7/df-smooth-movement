#!/bin/sh
# Builds and runs the unit tests (the animation manager, recording codec, tile repaint,
# sprite proxy, free camera, view context and console command tests) against the stub
# headers in stubs/, then replays the recording the codec test writes, which is three
# frames in the current format naming a different movement each, so that the replay's
# two-pass decode of a frame header is exercised without a fixture, then
# replays every recording in recordings/ and requires that every frame's draws digest to
# what expected/<name>.digest holds, and that recinfo.py reads the same number of frames.
# Exits non-zero when any fails, and also when recordings/ is empty, because that last part
# is then checking nothing and a pass would say otherwise; HARNESS_ALLOW_NO_RECORDINGS=1
# downgrades that to a note for a caller that knows. The game's own repaint count from the
# recording's self-check is printed for information: it matches only for the plugin version
# that made the recording.
# Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
# The animation manager test lives with the plugin sources, as the CMake target
# smooth-movement-test, which nothing else builds; it needs no stub.
c++ -std=c++17 -O2 ${CXXFLAGS:-} -Wall -Wextra -I"$src" \
    -o "$here/out/test-visual-animation-manager" "$src/test_visual_animation_manager.cpp"
"$here/out/test-visual-animation-manager"
# Builds and runs one harness test: harness/test_<name>.cpp against the stubs.
run_test() {
	c++ -std=c++17 -O2 ${CXXFLAGS:-} -Wall -Wextra -I"$here/stubs" -I"$src" \
	    -o "$here/out/test-$1" "$here/test_$(echo "$1" | tr - _).cpp"
	"$here/out/test-$1"
}
run_test frame-record
# recinfo.py must read a recording in the current format: the fixtures are older.
"$here/out/test-frame-record" "$here/out/test-current.rec"
fields='.* interpolation=\([a-z-]*\) settings=\([a-z=0-9.,-]*\) step=\([0-9]*\) sim=\(-*[0-9]*\) .*'
sample=$("$here/recinfo.py" "$here/out/test-current.rec" |
	sed -n "s/$fields/\1,\2,\3,\4/p;s/^frames //p" | tr '\n' ' ')
expected="none,-,250,1234567 linear,-,250,-1 hop,hop-height=0.15,horizontal-mult=1,diagonal-mult=2,vertical-mult=3,hops-per-step=1,250,1234569 3 "
if [ "$sample" != "$expected" ]; then
	echo "recinfo.py misreads the current format: $sample"; exit 1
fi
run_test tile-repaint
run_test sprite-proxies
run_test free-camera
run_test view-context
run_test plugin-commands
"$here/build.sh" "$src" test
# Exercise the replay's two-pass decoder with the generated current-format recording.
"$here/out/bench-test" replay "$here/out/test-current.rec" -
status=0
replayed=0
for rec in "$here"/recordings/*.rec; do
	[ -e "$rec" ] || continue
	replayed=$((replayed+1))
	name=$(basename "$rec" .rec)
	trace="$here/out/test-$name.trace"
	rc=0; "$here/out/bench-test" replay "$rec" "$trace" >"$here/out/test-$name.txt" || rc=$?
	if [ "$rc" -gt 1 ]; then
		cat "$here/out/test-$name.txt"; echo "replay of $name: exited $rc"; exit 1
	fi
	"$here/digest.py" "$trace" >"$here/out/test-$name.digest"
	# recinfo.py walks the file on its own; it must agree with the replay on the frame count.
	frames=$("$here/recinfo.py" "$rec" 0 -1 | sed -n 's/^frames //p')
	digested=$(wc -l <"$here/out/test-$name.digest" | tr -d ' ')
	if [ "$frames" != "$digested" ]; then
		echo "recinfo.py of $name: $frames frames, the replay $digested"; exit 1
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
if [ "$replayed" -eq 0 ]; then
	# The loop above is the only check that a change leaves the drawing alone on a real
	# scene. With no recordings it runs zero times, and a run that checks nothing must not
	# report success: a green check mark is read as "the drawing is unchanged", and nobody
	# reads the log of a job that passed. So this is a failure by default. Setting
	# HARNESS_ALLOW_NO_RECORDINGS turns it back into a note, for the one caller that knows
	# the fixtures are missing and has said so where the next reader can see it.
	if [ -z "${HARNESS_ALLOW_NO_RECORDINGS:-}" ]; then
		echo "FAIL: recordings/ holds no recordings, so nothing replayed a recorded scene"
		echo "      or compared it with expected/. The replay coverage in this run was the"
		echo "      three generated frames above, which is not a scene at all: they carry"
		echo "      no viewport and no unit, so no tile is drawn on any of them."
		echo "      Record a scene in the game to restore it; see the \"Recordings in the"
		echo "      repository\" section of harness/README.md. To run the rest of the"
		echo "      harness knowingly without it, set HARNESS_ALLOW_NO_RECORDINGS=1."
		exit 1
	fi
	echo "NOTE: recordings/ holds no recordings and HARNESS_ALLOW_NO_RECORDINGS is set, so"
	echo "      nothing replayed a recorded scene or compared it with expected/. The replay"
	echo "      coverage in this run was the three generated frames above."
fi
exit $status
