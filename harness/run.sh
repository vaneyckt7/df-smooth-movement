#!/bin/sh
# Usage: harness/run.sh <plugin dir> <label> [creatures] [levels]
# Builds the harness against the plugin sources in <plugin dir>, writes out/<label>.trace and prints timings.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$1" && pwd); label=$2; creatures=${3:-40}; levels=${4:-9}
mkdir -p "$here/out"
c++ -std=c++17 -O2 ${CXXFLAGS:-} -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I"$here/stubs" -I"$src" -DPLUGIN_SOURCE="\"$src/smooth-movement.cpp\"" -o "$here/out/bench-$label" "$here/bench.cpp"
"$here/out/bench-$label" "$here/out/$label.trace" "$creatures" "$levels"
