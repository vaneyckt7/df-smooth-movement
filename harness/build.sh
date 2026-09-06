#!/bin/sh
# Usage: harness/build.sh <plugin dir> <label>
# Builds the harness against the plugin sources in <plugin dir> as out/bench-<label>.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$1" && pwd); label=$2
mkdir -p "$here/out"
c++ -std=c++17 -O2 ${CXXFLAGS:-} -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I"$here/stubs" -I"$src" -DPLUGIN_SOURCE="\"$src/smooth-movement.cpp\"" -o "$here/out/bench-$label" "$here/bench.cpp"
