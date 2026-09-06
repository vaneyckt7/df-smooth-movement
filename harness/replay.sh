#!/bin/sh
# Usage: harness/replay.sh <plugin dir> <label> <recording>
# Builds the harness against <plugin dir> and replays frames recorded in the game with
# `smooth-movement stats record`, writing out/<label>.trace and comparing every frame's
# repaint count and painted flag with what the game's plugin did.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$1" && pwd); label=$2; recording=$3
mkdir -p "$here/out"
c++ -std=c++17 -O2 ${CXXFLAGS:-} -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I"$here/stubs" -I"$src" -DPLUGIN_SOURCE="\"$src/smooth-movement.cpp\"" -o "$here/out/bench-$label" "$here/bench.cpp"
"$here/out/bench-$label" replay "$recording" "$here/out/$label.trace"
