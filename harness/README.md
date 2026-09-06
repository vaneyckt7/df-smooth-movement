# Offline render harness

Runs the plugin's render code outside the game, so two versions of the plugin can be compared
exactly and quickly. `bench.cpp` includes `smooth-movement.cpp` directly and provides stub
headers in `stubs/` in place of DFHack, DF's structures and SDL: the viewport buffers, the
renderer base class and the few DFHack calls the plugin uses. The code under test is the
plugin as it ships, not a copy.

The stub renderer replaces the engine's tile repaint, texture copy and rectangle fill with
functions that count each call and, when a trace file is given, write one line per call:
which viewport, which tile, a hash of the tile's buffers and whether interface shading was
set. Repaints of tiles whose buffers are all zero are counted (the `blank` column of
`run.sh`) but not traced, so a trace records every visible draw the plugin asked for and
`compare.sh` cannot see a change in blank repaints.

## The scene

Nine stacked z-levels of 100x60 tiles. Forty creatures walk randomly on the top level and four on each lower one, some hauling items;
designations move; items lie on the floor; some tiles carry z-level shading and top shadows;
open tiles show the level below. The scene is seeded, so every run of every version sees the
identical sequence of buffer states. Phases: 12 frames warm-up, 600 busy, 300 idle with
sprite flipping on, 240 with the view scrolling, 120 paused, 300 idle with flipping off,
300 busy again: 1872 frames.

## Scripts

All scripts except `compile.sh` write under `harness/out/`, which is ignored by git. Run them
from the repository root. `<plugin dir>` is a directory
holding `smooth-movement.cpp` and `visual_animation.h`, such as the repository root (`.`) or
an export of another branch, for example
`git archive release/v0.5.0 | tar -x -C harness/out/src-base`.

- `run.sh <plugin dir> <label> [creatures] [levels]`: builds the harness against that source,
  writes `out/<label>.trace` and prints per-phase timings and draw counts.
- `compare.sh <label> [baseline label]`: byte-for-byte trace comparison. Identical traces
  mean the two versions asked the engine for exactly the same pixels.
- `oracle.py fold <a.trace> <b.trace>`: for a change that reorders or merges draws. Keys
  every operation to the 32-pixel screen cells it touches and requires the sequence of
  operations per cell to be identical over the whole run.
- `oracle.py reblank <with.trace> <without.trace>`: for dropping the previous-coverage
  blank-out. Per frame, every cell the new trace paints must get the same operations, and
  cells only the old trace touched may carry nothing but a blank-out and engine repaints.
- `timings.sh [-r rounds] <label>=<plugin dir> ...`: builds each version once, runs them
  interleaved for the given rounds and prints the median us/frame per phase, for example
  `harness/timings.sh -r 5 base=harness/out/src-base stats=.`.
- `CXXFLAGS=-DHARNESS_STATS harness/run.sh . stats` builds the harness with the `stats`
  console command wired in and exercises it after the run; the trace must stay identical.
- `test.sh <plugin dir>`: builds and runs the unit tests and, when present, the manager
  benchmark.
- `compile.sh <plugin dir>`: builds the plugin in DFHack's docker build image against the real
  headers. Needs `DFHACK_SRC` pointing at a DFHack checkout with `build/linux` configured,
  and touches nothing outside that build directory.

## What the numbers mean

Engine calls cost nothing here, so wall-clock time mostly reflects the plugin's own
bookkeeping and is noisy (about 10 to 20 percent between runs). Repaint, copy and fill counts
per phase are deterministic and are the figures that carry over to the game, where every
repaint is real work. Use `timings.sh` for time and the counts from `run.sh` for everything
else.

## Limits

The harness only sees what the stubs model. `compile.sh` catches a field the stubs lack, but
not a field they model with the wrong size or meaning. The in-game `smooth-movement stats`
command measures the same counters on a real fortress and is the check the harness cannot
replace.
