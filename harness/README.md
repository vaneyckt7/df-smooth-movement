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
equal to the previous frame, or literal values. At the end of the hook the recorder adds a
self-check for the harness: how many tiles the hook repainted, whether it painted at all, and
how many array entries the game changed while the hook ran. That is not part of the scene;
see "What a replay tells you".

Starting a recording resets the plugin's animation and camera state and asks the game for one
full redraw, so a recording begins from a known state whenever it starts. A scroll glide or
follow in progress at that moment is dropped, and a `camera` offset set within a frame or two
of `record` may land one tile off. `record stop` ends a recording early and `record status`
reports progress. Recording costs about a millisecond per frame, so do not take timings while
it runs. The file is written relative to the game's directory; `frame_record.h` documents the
format.

## Scripts

The scripts build and write under `harness/out/`, which is ignored by git, with the `c++` on
the path; clang and g++ both work. Run them from the repository root. `<plugin dir>` is a
directory holding the plugin sources (`smooth-movement.cpp`, `visual_animation.h`,
`movement.h`, `frame_record.h`, `frame_recorder.h`, `frame_stats.h`, `free_camera.h`,
`plugin_commands.h`, `plugin_settings.h`, `plugin_state.h`, `sprite_proxies.h`,
`tile_coverage.h`, `tile_repaint.h`, `view_context.h` and the unit test),
such as the repository root (`.`) or an export of another branch, for example
`mkdir -p harness/out/src-base && git archive release/v0.5.0 | tar -x -C harness/out/src-base`.

- `replay.sh <plugin dir> <label> <recording>`: builds the harness against that source and
  replays the recording, writing `out/<label>.trace` and printing the repaint stats and the
  self-check against the game.
- `compare.sh <label> [baseline label]`: byte-for-byte trace comparison. Identical traces
  mean the two versions asked the renderer for exactly the same pixels. Exits non-zero when
  they differ or one is missing or empty.
- `recinfo.py <recording> [first] [last]`: prints one line per recorded frame: settings,
  window, viewports, unit count, and the repaints the game made.
- `build.sh <plugin dir> <label>`: just builds `out/bench-<label>`, for running `bench` by hand.
  `bench replay <recording> [trace-out]` replays a recording and writes the trace to
  `trace-out`. Leave `trace-out` out, or pass `-`, to replay without a trace. It exits 0 when
  the self-check passed on every frame, 1 when it failed on some, 2 when the recording could
  not be read or has no frames, the trace could not be written, or the plugin would not
  enable, 3 on a usage error.
  `replay.sh` passes that code through.
- `digest.py <trace> [<trace>]`: one line per replayed frame: the frame number, whether the
  hook painted, how many repaints it asked for including blank ones, how many trace lines it
  wrote (its visible repaints and SDL draws) and a SHA-256 digest of those lines. With two
  traces it prints the frames whose lines differ.
- `test.sh <plugin dir>`: builds and runs the animation manager test
  (`test_visual_animation_manager.cpp` in the plugin directory, which steps creatures
  through the animation manager and checks the transitions, easing, retargets and hop
  decisions it makes), the recording codec test, the tile repaint test
  (`test_tile_repaint.cpp`, which checks against the stub viewport which of the 25 arrays
  each repaint pass zeroes and that every entry is restored) and the sprite proxy test
  (`test_sprite_proxies.cpp`, which steps a creature through the animation manager on the
  stub viewport and checks which sprites get a proxy, what tiles each covers, that fire,
  the clip and a missing texture block one, which proxies hop with the walk hop on and
  the row above each then covers, and which hauled icons hop with their carrier) and the
  free camera test
  (`test_free_camera.cpp`, which drives the camera with a stand-in manager and checks the
  render offset a landed scroll, a window jump, a followed movement, a normalization write
  and a middle-mouse drag give) and the view context test (`test_view_context.cpp`, which
  checks which changes of the main viewport's signature reset the animation context and
  that a map scroll only pans) and the console command test (`test_plugin_commands.cpp`,
  which runs every `smooth-movement` command word with valid and invalid arguments against
  a plugin state and checks the outcome, what each sets and prints, and when it asks the
  game for a full redraw), then replays the three-frame recording the codec test writes.
  That recording is in the current format and names a different movement on each frame,
  which is what a replay's two passes over a frame header need to be wrong about for the
  decode to carry one frame's movement settings into the next, so it holds whether or not
  there is a fixture. It then replays every recording
  in `recordings/` and fails when any frame's digest differs from `expected/<name>.digest`
  or `recinfo.py` reads a different number of frames than the replay.
  It prints the replay's repaint total alongside the game's from the self-check, which
  match only for the plugin version that made the recording, which for the four now in
  `recordings/` is the version that put them there. An empty `recordings/` would make that
  last part check nothing, so the run fails rather than passing quietly; setting
  `HARNESS_ALLOW_NO_RECORDINGS=1` runs the rest of the harness knowing the scenes are
  missing, which is not the case here.
- `compile.sh <plugin dir>`: builds the plugin in DFHack's docker build image against the real
  headers. Needs `DFHACK_SRC` pointing at a DFHack checkout with `build/linux` configured,
  and touches nothing outside that build directory.

## Recordings in the repository

There are four, all of one fortress -- Nakasmafol, a fresh embark -- at zoom 192 with nine
viewports, the window still and sprite flipping on. They were recorded on 2026-09-19 with the
plugin at `c4bb6ce`, which is the version that made the digests, so the game's repaint count
in each one's self-check matches the replay's rather than only being informative.

| recording | frames | movement and settings | what it covers |
| --------- | -----: | --------------------- | -------------- |
| `fortress-600.rec` | 600 | `hop`, defaults | the free camera off, mirrored sprites, the hop's extra repainted row |
| `fortress-camera-200.rec` | 200 | `hop`, defaults | the free camera on, resting at -0.3, 0.2 tiles; every frame painted |
| `fortress-hauled-400.rec` | 400 | `hop`, defaults | hauled item icons on |
| `fortress-smoothstep-300.rec` | 300 | `smoothstep` | the movement the README tells a new user to pick, which has no lift and so no extra row |

None of them has a followed unit, a scroll, or a zoom change, and none uses `linear` or
`none`. Those paths are still uncovered by a recorded scene.

They replace `fortress-600.rec` and `fortress-camera-183.rec`, which commit `b4c32fc` deleted
when the settings and the recording format moved over to naming a movement. Those two were
format version 2 and the reader now accepts version 7 only (`frame_record.h` sets
`oldest_version` equal to `version`), so they could not be restored from git history: they
cannot be read. Note that the version number alone does not make a recording readable. A
version 7 file written before the hop's settings were renamed -- when the movement was called
`bob` and its settings `amount`, `horizontal`, `diagonal`, `vertical` and `hops` -- is refused
with `bad movement settings`, because the names inside it are not the names this version
knows.

A recording is a fixture and stays fixed: every version of the plugin replays the same scene.
`expected/<name>.digest` holds, for each recording, one line per frame with the repaint count
and the digest of the visible draws the current version asks for on it, made with `digest.py`
from the trace of a replay. `test.sh` replays every recording and requires every line to
match, so a change to the plugin, the stubs or the replay that alters what the render hook
draws, or how many repaints it asks for, on those scenes fails the test without the game.

That only works if two machines agree on the digest, and a digest is a hash of trace lines
holding coordinates printed to three decimals, so the two have to agree on the last bit of a
float. Left alone they do not. A compiler is allowed to contract a multiply and a following
add into one fused instruction, which rounds once where the pair rounds twice, and both
compilers here take that permission by default. Whether they can act on it is decided by the
target: arm64's baseline floating point has `fmadd`, so clang fuses, while x86-64's baseline is
SSE2, which has no such instruction, so GCC emits the multiply and the add separately unless
the build asks for `-mfma`. Counted on the harness binary, the same 19 sites fuse under
clang/arm64 and none under GCC/x86-64. One draw of `recordings/fortress-hauled-400.rec` came
out at `x=146.340` on the fused side and `x=146.339` on the unfused one, which failed that
frame's digest on a scene where nothing had changed.

So `build.sh` and `test.sh` compile with `-ffp-contract=off`, ahead of `CXXFLAGS` so a caller
can still override it on purpose. Off rather than on is the direction that can be reached
everywhere: every target can do a separate multiply and add, while making x86-64 fuse would
mean requiring a CPU with FMA. With it the four recordings trace byte for byte the same on
both machines. Anything else that builds the plugin for a digest has to set it too, and a
digest regenerated by a build without it belongs to that build alone.

The rule for a change is by kind. A change that makes the plugin cheaper must change only the
repaint count column of the file, with every frame's painted flag, draw count and digest the
same, and that diff is its evidence. A change to what the plugin draws regenerates the file
(`test.sh` prints the copy command) and shows what changed with `compare.sh` on traces of the
two versions, since the digest only says which frames moved. Any other change, such as a
refactor or a change to the stubs, must leave the file untouched. The game's repaint count in
the recording's self-check then differs from the replay's and is only informative.

A scene is worth recording for the paths it covers, and a path no recording covers is not
covered at all. That is why the table above says what each one covers and what none of them
does. A recording added later belongs in that table with the same two things named: what it
covers, and which movement and settings were current when it was made, since the replay uses
the ones the recording carries rather than any default.

## What a replay tells you

The self-check: a recording of the plugin version that made it must replay with zero frames
differing from the game's repaint count and painted flag. That is the proof that the stubs
and the replay model what the hook reads, and it is meaningful only for that version; for a
later version the game's count is a reference, not a verdict. Each frame also records how
many array entries the game changed while the hook ran. A frame with a non-zero count saw
input the replay cannot reproduce, and the replay summary says how many of the differing
frames were of that kind.

A recording then serves as a real scene for comparing versions: replay two versions with
traces and run `compare.sh` on them. Repaint, copy and fill counts are the same on every run
and are the figures that carry over to the game, where every repaint is real work. Renderer
calls cost nothing here, so the wall-clock time a replay prints covers the plugin's own
bookkeeping and, when a trace is written, the trace lines; it varies by about two percent
between runs of one build on one machine, so a difference smaller than that says nothing.
That time is the measure for a change to the bookkeeping; for a change to the repaints the
in-game `smooth-movement stats` command remains the only measure of what it saves.

## Limits

The harness only sees what the stubs model. A field the stubs lack fails the harness build,
and a field the stubs invent fails `compile.sh`. A field they model with the wrong size or
meaning gets past both; a replay that matches the game frame for frame catches that only for
arrays the recording carries literal values of, since runs of zero or unchanged entries
decode the same at any size. Scenes are limited to
what has been recorded: a recording from an empty part of the map or with the free camera on
is a different scene. The recorder writes only active viewports, so a viewport the game keeps
allocated but inactive is absent from the replay; the plugin's per-frame context update sees
the game's main viewport on such a frame and the replay does not, which can show up as a
differing frame rather than a missed difference.
The replay also fills the stub texture cache with every texture the recording mentions before
the first frame, while the game fills its cache as it paints. A unit whose sprite first appears
in the frame it starts moving has no cached texture in the game and is skipped there, but is
drawn in the replay, which shows up as a differing frame in the same way.
