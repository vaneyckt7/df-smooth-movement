#!/bin/sh
# Builds every branch once, then runs them interleaved N rounds so drift hits all alike.
# Writes out/timings.txt: label phase us/frame per round, and out/timings-median.txt.
set -eu
here=$(cd "$(dirname "$0")" && pwd); rounds=${1:-3}
labels="base pr0 pr1 pr2 pr3 pr4 pr5 pr6"
for l in $labels; do src=$here/out/src-$l; $here/run.sh "$src" "$l" >/dev/null 2>&1; done
: > "$here/out/timings.txt"
r=0; while [ $r -lt "$rounds" ]; do
  for l in $labels; do
    "$here/out/bench-$l" - 40 9 2>&1 | awk -v l="$l" -v r="$r" '/us\/frame/{print l, $1, $2, r}' >> "$here/out/timings.txt"
  done; r=$((r+1)); done
python3 - "$here/out/timings.txt" <<'PY' | tee "$here/out/timings-median.txt"
import sys,statistics
from collections import defaultdict
d=defaultdict(list)
for line in open(sys.argv[1]):
    l,ph,us,r=line.split();d[(l,ph)].append(float(us))
labels="base pr0 pr1 pr2 pr3 pr4 pr5 pr6".split();phases=[]
for (l,ph) in d:
    if ph not in phases:phases.append(ph)
print("phase".ljust(12)+"".join(l.rjust(9) for l in labels))
for ph in phases:print(ph.ljust(12)+"".join(f"{statistics.median(d[(l,ph)]):9.0f}" for l in labels))
PY
