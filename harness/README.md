# Development tools

Tools for working on the plugin outside the game. Nothing here is part of the plugin's build.

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
`visual_animation.h`, `frame_record.h` and the unit test), such as the repository root (`.`).

- `recinfo.py <recording> [first] [last]`: prints one line per recorded frame: settings,
  window, viewports, unit count, and the repaints the game made.
- `test.sh <plugin dir>`: builds and runs the recording codec test against the stub viewport
  header in `stubs/`.
