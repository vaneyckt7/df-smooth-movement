#!/bin/sh
# Builds and runs everything under tests/ against the headers in the repository root:
# the unit tests, the paint-op oracle fuzzer (which must report no differing frames),
# and the render benchmark. Usage: tests/run.sh [compiler]
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
	"$CXX" -std=c++17 -O1 -I. -Itests/oracle -o "$out/$1" "tests/oracle/$1.cpp"
	last=$("$out/$1" | tail -1)
	case "$last" in
		*"differing 0"*) echo "ok  $1: $last";;
		*) echo "FAIL $1: $last"; exit 1;;
	esac
	}
run_bench()
	{
	"$CXX" -std=c++17 -O2 -I. -Itests/oracle -o "$out/$1" "$2"
	echo "bench $1: $("$out/$1" $3)"
	}

run_test test_visual_animation_manager
run_test test_frame_render
run_fuzz render_fuzz_ops
run_bench render_bench tests/oracle/render_bench.cpp "75 50 5 9 2000"
