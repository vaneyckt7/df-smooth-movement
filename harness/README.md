# Offline render harness

Compiles `smooth-movement.cpp` against stub DFHack/SDL headers, drives nine synthetic z-levels
through busy, idle, scrolling and paused phases, records every engine repaint and SDL draw as
a trace, and times the render hook per frame. Used to verify the `speed/*` branches.

- `run.sh <plugin dir> <label>`: build and run, writes `out/<label>.trace` and prints timings.
- `compare.sh <label> [base]`: byte-for-byte trace comparison.
- `oracle.py <a.trace> <b.trace>`: per-cell paint-order oracle (for the shading fold).
- `oracle6.py <with.trace> <without.trace>`: oracle for dropping the previous-coverage re-blank.
- `timings.sh [rounds]`: interleaved timing rounds over `out/src-*` source directories.
- `test.sh <plugin dir>`: builds and runs the unit tests and the manager benchmark.
- `compile.sh <plugin dir>`: builds the plugin in DFHack's docker build image (`DFHACK_SRC`).

Timings are noisy (±10–20%); repaint counts and traces are deterministic.
