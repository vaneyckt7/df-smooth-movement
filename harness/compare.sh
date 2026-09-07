#!/bin/sh
# Compares a trace against the baseline trace. Usage: harness/compare.sh <label> [baseline-label]
# Exits 0 when the traces are identical, 1 when they differ or one is missing or empty.
set -eu
here=$(cd "$(dirname "$0")" && pwd); a="$here/out/${2:-base}.trace"; b="$here/out/$1.trace"
for f in "$a" "$b"; do [ -s "$f" ] || { echo "no trace or empty trace $f" >&2; exit 1; }; done
if cmp -s "$a" "$b"; then echo "trace $1 == ${2:-base}: IDENTICAL ($(wc -l <"$b" | tr -d ' ') lines)"; else
  echo "trace $1 != ${2:-base}"; diff "$a" "$b" | head -20; exit 1; fi
