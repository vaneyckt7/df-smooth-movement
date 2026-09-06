#!/bin/sh
# Compares a trace against the baseline trace. Usage: harness/compare.sh <label> [baseline-label]
set -eu
here=$(cd "$(dirname "$0")" && pwd); a="$here/out/${2:-base}.trace"; b="$here/out/$1.trace"
if cmp -s "$a" "$b"; then echo "trace $1 == ${2:-base}: IDENTICAL ($(wc -l <"$b" | tr -d ' ') lines)"; else
  echo "trace $1 != ${2:-base}"; diff "$a" "$b" | head -20; fi
