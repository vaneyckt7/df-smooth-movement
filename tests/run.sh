#!/bin/sh
# Builds and runs everything under tests/ against the headers in the repository root:
# the unit tests, the render property fuzzer and the render benchmark.
# Usage: tests/run.sh [compiler]
#
# render_fuzz.cpp drives the render pass over random viewport histories and checks each
# painted frame against the properties the pass owes the engine (what it blanks, repaints,
# in which order, with which buffers hidden, leaving the buffers as found); it must report
# "failed 0". The properties encode the engine's draw order as understood from its output,
# not as checked against its code.
set -eu
CXX=${1:-${CXX:-c++}}
root=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/smooth-movement-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT
cd "$root"

run_test()
	{
	"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -I. -o "$out/$1" "tests/$1.cpp"
	"$out/$1"
	echo "ok  $1"
	}
run_fuzz()
	{
	"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -I. -Itests -o "$out/$1" "tests/$1.cpp"
	last=$("$out/$1" | tail -1)
	case "$last" in
		*"failed 0"*) echo "ok  $1: $last";;
		*) echo "FAIL $1: $last"; exit 1;;
	esac
	}
run_bench()
	{
	"$CXX" -std=c++17 -O2 -I. -Itests -o "$out/$1" "$2"
	echo "bench $1: $("$out/$1" $3)"
	}

run_test test_visual_animation_manager
run_test test_frame_render
run_test test_view_context
run_test test_free_camera
run_test test_sdl_canvas
run_fuzz render_fuzz
run_bench render_bench tests/render_bench.cpp "75 50 5 9 2000"
