#!/bin/sh
# Usage: harness/replay.sh <plugin dir> <label> <recording>
# Builds the harness against <plugin dir> and replays frames recorded in the game with
# `smooth-movement record`, writing out/<label>.trace and comparing every frame's
# repaint count and painted flag with what the game's plugin did.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
"$here/build.sh" "$1" "$2"
"$here/out/bench-$2" replay "$3" "$here/out/$2.trace"
