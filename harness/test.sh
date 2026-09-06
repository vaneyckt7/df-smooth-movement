#!/bin/sh
# Builds and runs the frame recording codec test against the stub viewport header in stubs/.
# Exits non-zero when it fails. Usage: harness/test.sh <plugin dir>
set -eu
src=$(cd "$1" && pwd); here=$(cd "$(dirname "$0")" && pwd); mkdir -p "$here/out"
c++ -std=c++17 -O2 -Wall -Wextra -I"$here/stubs" -I"$src" -o "$here/out/test-frame-record" "$here/test_frame_record.cpp"
"$here/out/test-frame-record"
