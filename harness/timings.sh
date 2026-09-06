#!/bin/sh
# Interleaved timing rounds: builds every version once, then runs them round-robin so machine
# drift hits all alike, and prints the median us/frame per phase.
# Usage: harness/timings.sh [-r rounds] <label>=<plugin dir> [<label>=<plugin dir> ...]
# Example: harness/timings.sh -r 5 base=out/src-base stats=.
set -eu
here=$(cd "$(dirname "$0")" && pwd); rounds=3
if [ "${1:-}" = "-r" ]; then rounds=$2; shift 2; fi
[ $# -ge 1 ] || { echo "usage: $0 [-r rounds] <label>=<plugin dir> ..." >&2; exit 2; }
labels=""
for spec in "$@"; do
  l=${spec%%=*}; src=${spec#*=}; labels="$labels $l"
  "$here/run.sh" "$src" "$l" >/dev/null
done
: > "$here/out/timings.txt"
r=0; while [ "$r" -lt "$rounds" ]; do
  for l in $labels; do
    "$here/out/bench-$l" - 40 9 2>&1 | awk -v l="$l" -v r="$r" '/us\/frame/{print l, $1, $2, r}' >> "$here/out/timings.txt"
  done; r=$((r+1)); done
python3 - "$here/out/timings.txt" $labels <<'PY' | tee "$here/out/timings-median.txt"
import sys,statistics
from collections import defaultdict
d=defaultdict(list);labels=sys.argv[2:];phases=[]
for line in open(sys.argv[1]):
    l,ph,us,r=line.split();d[(l,ph)].append(float(us))
    if ph not in phases:phases.append(ph)
print("phase".ljust(12)+"".join(l.rjust(9) for l in labels))
for ph in phases:print(ph.ljust(12)+"".join(f"{statistics.median(d[(l,ph)]):9.0f}" for l in labels))
PY
