#!/bin/sh
# Usage: harness/build.sh <plugin dir> <label>
# Builds the harness against the plugin sources in <plugin dir> as out/bench-<label>.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$1" && pwd); label=$2
mkdir -p "$here/out"
# -ffp-contract=off, before CXXFLAGS so a caller can still override it deliberately. A digest
# is a hash of coordinates printed to three decimals, so two builds must agree on the last
# bit of a float or the same scene digests differently. Left to the compiler they do not:
# GCC contracts a multiply and an add into one fused instruction by default and clang does
# it within a statement, and the two round differently. One draw of
# recordings/fortress-hauled-400.rec came out at x=146.340 on clang and x=146.339 on GCC,
# which failed the replay on a scene where nothing was wrong. With contraction off all four
# recordings trace byte for byte the same on clang/arm64 and GCC/x86-64.
c++ -std=c++17 -O2 -ffp-contract=off ${CXXFLAGS:-} -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I"$here/stubs" -I"$src" -DPLUGIN_SOURCE="\"$src/smooth-movement.cpp\"" -o "$here/out/bench-$label" "$here/bench.cpp"
