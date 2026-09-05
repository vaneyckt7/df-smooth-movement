#!/bin/sh
# Builds and runs the upstream unit test executable (and the manager benchmark when present).
# Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
c++ -std=c++17 -O2 -Wall -Wextra -I"$src" -o "$here/out/test" "$src/test_visual_animation_manager.cpp" && "$here/out/test" && echo "unit tests: OK"
if [ -f "$src/bench_visual_animation_manager.cpp" ]; then
  c++ -std=c++17 -O2 -Wall -Wextra -I"$src" -o "$here/out/mbench" "$src/bench_visual_animation_manager.cpp" && "$here/out/mbench"
fi
