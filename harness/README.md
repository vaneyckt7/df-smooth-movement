# Offline render harness

Replays frames recorded in the game through the plugin's render code outside the game, so two
versions of the plugin can be compared exactly and quickly. `bench.cpp` includes
`smooth-movement.cpp` directly, as it ships. The headers the plugin includes from DFHack, DF's
structures and SDL resolve to stubs in `stubs/`, which declare only the fields the plugin
touches: the viewport with its per-tile arrays, the renderer base class, the unit and item
structures, and the few DFHack calls the plugin makes. The code under test is the plugin, not a
copy.

The plugin asks the game's renderer for a few things: repaint a tile, copy a texture to the
screen, optionally mirrored, set a clip rectangle, set a draw colour, and fill a rectangle. The
stub renderer replaces each with a function that counts the call and, when a trace file is
given, writes one line for it. For a repaint that line holds which viewport, which tile, a hash
of the tile's current entries in the 24 arrays other than the interface array, and the tile's
interface entry, the texture the game shades it with, or 0 when the plugin repaints without
shading. A repaint of a tile whose 25 current entries are all zero draws nothing in the game,
so it is counted but not traced. A trace therefore lists every visible draw the plugin asked
for, in order. `bench.cpp` documents the line formats.

## Recording

`smooth-movement record <file> [frames]` in the game writes, for each of the next frames
(900 by default), everything the render hook read. That is the plugin's settings, the clock,
window position, pause state, follow target, mouse, zoom and origin, the free camera's offset,
the per-tile arrays of every active viewport, and the units in view with the texture of any
hauled item. The per-tile arrays are what the game fills before it draws a viewport: 20
texture slots and 5 flag words per tile, plus the game's copy of each from the previous frame.
They change little between frames, so each array is written as runs of zero entries, entries
equal to the previous frame, or literal values. At the end of the hook the recorder adds what
the hook did: how many tiles it repainted, whether it painted at all, and how many array
entries the game changed while the hook ran.

Starting a recording resets the plugin's animation and camera state and asks the game for one
full redraw, so a recording begins from a known state whenever it starts. A scroll glide or
follow in progress at that moment is dropped, and a `camera` offset set within a frame or two
of `record` may land one tile off. `record stop` ends a recording early and `record status`
reports progress. Recording costs about a millisecond per frame, so do not take timings while
it runs. The file is written relative to the game's directory; `frame_record.h` documents the
format.

## Scripts

The scripts build and write under `harness/out/`, which is ignored by git. Run them from the
repository root. `<plugin dir>` is a directory holding the plugin sources (`smooth-movement.cpp`,
`visual_animation.h`, `frame_record.h` and the unit test), such as the repository root (`.`)
or an export of another branch, for example
`mkdir -p harness/out/src-base && git archive origin/release/v0.5.0 | tar -x -C harness/out/src-base`.

- `replay.sh <plugin dir> <label> <recording>`: builds the harness against that source and
  replays the recording, writing `out/<label>.trace` and comparing every frame's repaint count
  and painted flag with what the game's plugin did.
- `compare.sh <label> [baseline label]`: byte-for-byte trace comparison. Identical traces
  mean the two versions asked the renderer for exactly the same pixels. Exits non-zero when
  they differ or one is missing or empty.
- `recinfo.py <recording> [first] [last]`: prints one line per recorded frame: settings,
  window, viewports, unit count, and the repaints the game made.
- `build.sh <plugin dir> <label>`: just builds `out/bench-<label>`, for running `bench` by hand.
  `bench replay <recording> [trace-out]` replays a recording and writes the trace to
  `trace-out`. Leave `trace-out` out, or pass `-`, to replay without a trace. It exits 0 when
  every frame matched the game, 1 when some differed, 2 when the recording could not be read
  or has no frames, the trace could not be written, or the plugin would not enable, 3 on a
  usage error.
  `replay.sh` passes that code through.
- `test.sh <plugin dir>`: builds and runs the recording codec test, then replays every recording
  in `recordings/` and fails when any frame differs from what the game's plugin did.
- `compile.sh <plugin dir>`: builds the plugin in DFHack's docker build image against the real
  headers. Needs `DFHACK_SRC` pointing at a DFHack checkout with `build/linux` configured,
  and touches nothing outside that build directory.

## Recordings in the repository

`recordings/` holds two recordings made in Dwarf Fortress 53.16 with DFHack 53.16-r1.1 and
plugin 0.5.0 as merged in the recorder pull request, both of the same fortress at zoom 192
with nine viewports on screen. `fortress-600.rec` is 600 frames with the free camera off, starting paused and then
scrolling. `fortress-camera-183.rec` is 183 frames with the free camera on, resting a little off
the tile grid, with creatures walking left so their sprites are mirrored. `test.sh` replays
both and requires zero differing frames, so a change to the plugin, the stubs or the replay
that alters what the render hook does on a real scene fails the test without the game. A
change that is meant to alter it needs a new recording made in the game with that version.

## What a replay tells you

A recording of the plugin version that made it must replay with zero differing frames. That
is the check that the stubs and the replay model what the hook reads; 1800 frames of a fresh
embark and 600 frames recorded mid-session with a camera offset both replay with none. Each
frame also records how many array entries the game changed while the hook ran. A frame with a
non-zero count saw input the replay cannot reproduce, and the replay summary says how many of
the differing frames were of that kind.

A recording then serves as a real scene for comparing versions: replay two versions with
traces and run `compare.sh` on them. Repaint, copy and fill counts are the same on every run
and are the figures that carry over to the game, where every repaint is real work. Renderer
calls cost nothing here, so the wall-clock time a replay prints reflects only the plugin's own
bookkeeping; it varies by about two percent between runs of one build on one machine, so a
difference smaller than that says nothing. The in-game `smooth-movement stats` command
remains the only measure of what a change saves.

## Limits

The harness only sees what the stubs model. `compile.sh` catches a field the stubs lack. A
field they model with the wrong size or meaning gets past it; a replay that matches the game
frame for frame catches that for everything the render hook reads. Scenes are limited to
what has been recorded: a recording from an empty part of the map or with the free camera on
is a different scene. The recorder writes only active viewports, so a viewport the game keeps
allocated but inactive is absent from the replay; the plugin's per-frame context update sees
the game's main viewport on such a frame and the replay does not, which can show up as a
differing frame rather than a missed difference.
The replay also fills the stub texture cache with every texture the recording mentions before
the first frame, while the game fills its cache as it paints. A unit whose sprite first appears
in the frame it starts moving has no cached texture in the game and is skipped there, but is
drawn in the replay, which shows up as a differing frame in the same way.
