# Offline render harness

Replays frames recorded in the game through the plugin's render code outside the game, so two
versions of the plugin can be compared exactly and quickly. `bench.cpp` includes
`smooth-movement.cpp` directly and provides stub headers in `stubs/` in place of DFHack, DF's
structures and SDL: the viewport buffers, the renderer base class and the few DFHack calls the
plugin uses. The code under test is the plugin as it ships, not a copy.

The stub renderer replaces the engine's tile repaint, texture copy and rectangle fill with
functions that count each call and, when a trace file is given, write one line per call:
which viewport, which tile, a hash of the tile's buffers and whether interface shading was
set. Repaints of tiles whose buffers are all zero are counted but not traced, so a trace
records every visible draw the plugin asked for.

## Recording

`smooth-movement stats record <file> [frames]` in the game writes, for each of the next frames
(900 by default), everything the render hook read: the plugin's settings, the clock, window
position, pause state, follow target, mouse, zoom and origin, every buffer of every active
viewport (run-coded against the previous frame), the units in view with the texture of any
hauled item, and what the hook did with it: how many tiles it repainted and whether it painted
at all. Starting a recording resets the plugin's animation and camera state and asks the
engine for one full redraw, so the game and a replay begin from the same state whenever the
recording starts (a scroll glide or follow in progress at that moment is dropped, and a
`camera` offset set within a frame or two of `stats record` may land one tile off); the free
camera's offset is recorded with each frame, and the units are
captured after the hook has applied the camera for the frame. `stats record stop` ends a
recording early. Recording costs about a millisecond per frame, so do not take timings while
it runs. The file is written relative to the game's directory; `frame_record.h` documents the
format.

## Scripts

The scripts build and write under `harness/out/`, which is ignored by git. Run them
from the repository root. `<plugin dir>` is a directory holding `smooth-movement.cpp` and
`visual_animation.h`, such as the repository root (`.`) or an export of another branch, for
example `mkdir -p harness/out/src-base && git archive release/v0.5.0 | tar -x -C harness/out/src-base`.

- `replay.sh <plugin dir> <label> <recording>`: builds the harness against that source and
  replays the recording, writing `out/<label>.trace` and comparing every frame's repaint count
  and painted flag with what the game's plugin did.
- `compare.sh <label> [baseline label]`: byte-for-byte trace comparison. Identical traces
  mean the two versions asked the engine for exactly the same pixels.
- `recinfo.py <recording> [first] [last]`: prints one line per recorded frame: settings,
  window, viewports, unit count, and the repaints the game made.
- `build.sh <plugin dir> <label>`: just builds `out/bench-<label>`, for running `bench` by hand.
- `test.sh <plugin dir>`: builds and runs the unit tests, the recording codec test, a
  record-and-replay round trip of a small built-in scene, and, when present, the manager
  benchmark.
- `compile.sh <plugin dir>`: builds the plugin in DFHack's docker build image against the real
  headers. Needs `DFHACK_SRC` pointing at a DFHack checkout with `build/linux` configured,
  and touches nothing outside that build directory.

## What a replay tells you

A recording of the plugin version that made it must replay with zero differing frames; that is
the check that the stubs and the replay model the game faithfully (1800 frames of a fresh
embark replay with none, as do 600 frames recorded mid-session with a camera offset, as of
this writing). Each frame also records how many buffer words
the game changed while the hook ran; a frame with a non-zero count saw input the replay cannot
reproduce, and the replay summary says how many of the differing frames were of that kind.

A recording then serves as a real scene for comparing versions: replay two versions with
traces and run `compare.sh` on them. Repaint, copy and fill counts are deterministic and are
the figures that carry over to the game, where every repaint is real work. Engine calls cost
nothing here, so the wall-clock time a replay prints mostly reflects the plugin's own
bookkeeping and is noisy; the in-game `smooth-movement stats` command remains the only measure
of what a change saves.

## Limits

The harness only sees what the stubs model. `compile.sh` catches a field the stubs lack, but
not a field they model with the wrong size or meaning; a replay that matches the game frame
for frame catches the second kind for everything the render hook reads. Scenes are limited to
what has been recorded: a recording from an empty part of the map or with the free camera on
is a different scene, and the built-in self-test scene is only there to check the codec.
