#!/bin/sh
# Builds and runs the frame recording codec test against the stub viewport header in stubs/,
# then replays every recording in recordings/ and requires that no frame differs from what
# the game's plugin did. Exits non-zero when either fails. Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-frame-record" "$here/test_frame_record.cpp"
"$here/out/test-frame-record"
"$here/build.sh" "$src" test
for rec in "$here"/recordings/*.rec; do
	name=$(basename "$rec" .rec)
	"$here/out/bench-test" replay "$rec" "$here/out/test-$name.trace" >"$here/out/test-$name.txt" || { rc=$?; cat "$here/out/test-$name.txt"; echo "replay of $name: exited $rc"; exit 1; }
	echo "replay of $name: OK, $(sed -n 's/^game: *//p' "$here/out/test-$name.txt")"
done
