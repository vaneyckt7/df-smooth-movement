# Changelog

## Unreleased

- The console commands read `on` and `off` through one parser, and a setting's bare command prints the same line the bare `smooth-movement` lists for it. Every command prints what it printed before.
- `harness/test.sh` builds and runs each harness test through one shell function. Nothing the plugin does changes.
- `sdl_apist::clear` assigns a fresh value instead of listing every binding. Nothing the plugin does changes.
- Both map screen interposes note the simulation tick through `note_map_render`, and the three hooks are applied and removed by `apply_hooks` and `remove_hooks`. Nothing the plugin does changes.
- Every request for a full redraw by the game goes through `full_redraw`, which the console commands already used. Nothing the plugin does changes.
- The repaint passes of `tile_repaint.h` get the callable that asks the game for a tile repaint from one function, `staged_repainter`. Nothing the plugin draws changes.
- The render hook looks the units in view up through one function, `units_in_view`, for the frame recorder and for the hauled item icons. Nothing the plugin draws or records changes.
- A creature sprite and a hauled item's icon find their place on screen through one function, `place_sprite`, instead of each working out the glide and the bob lift. Nothing the plugin draws changes.
- The tile size on screen is computed by one function, `tile_size_px`, where four places in the render hook each had the formula. Nothing the plugin draws changes.
- `harness/test.sh` builds and runs the animation manager test with the other unit tests;
  it prints a line when it passes, like them. Nothing the plugin does changes.
- The match that makes a hauled item's icon bob with its carrier moved out of the render
  hook into `sprite_proxies.h`, beside the rule that decides which sprites bob, and is
  covered by the sprite proxy test. Nothing the plugin draws changes.
- The plugin's settings are one value, `plugin_settingsst` in `plugin_settings.h`, that the
  state hands out and takes back whole; a frame recording stores it with every frame and the
  harness restores it before replaying the frame. The recording format is unchanged.
- The render hook's two paint paths, the camera glide and the incremental frame, share one
  black fill under the tiles they repaint, with the game's draw colour saved and restored
  around it, instead of a copy each. Nothing the plugin draws changes.
- The plugin's state and its console commands moved out of `smooth-movement.cpp` into
  `plugin_state.h` and `plugin_commands.h`; the commands are now covered by a harness test.
  Nothing the plugin draws or prints changes.
- Frame recordings carry the walk bob settings (format version 5), so a recording made
  with the bob on replays with it. The harness reads versions 2 to 4 as well, with the bob
  off, which replays them as before.
- Added: an optional walk bob. `bob on` lifts every gliding creature sprite twice per step,
  like two footfalls, by `bob <amount>` (default 0.10 tile) times a multiplier for the step's
  direction (`bobmult <horizontal> <diagonal> <vertical>`, default 1, 2.4, 2.7); `hops 1`
  gives a single bounce. Off by default, and with it off nothing the plugin draws changes.
  Carried item icons bob with their creature, vehicles never do, and a creature whose row
  above is off the screen or burning glides without the bob.
- Fixed: a repaint while the game is paused can no longer be read as a step, so sprites do
  not slide or flip with the simulation standing still. A redraw at a simulation tick the
  per-tile arrays were already drawn at (the game showing the units sharing a tile in turn,
  blinking markers) is presentation, not a step, and starts no movement.
  The tick is read where the arrays are filled, in the map screens' render on the simulation
  thread. Frame recordings carry it (format version 4); the harness reads versions 2 and 3
  as well, with the tick unknown, which replays them as before.
- Frame recordings carry the one-tile step time (format version 3), so a recording made at
  a `timestep` other than the default replays at that step. The harness reads version 2
  recordings as well, at the 150 ms they were made with.
- Add `smooth-movement timestep <ms>`: how long a one-tile glide takes, 20 to 2000 ms,
  150 by default as before. A movement keeps the time it started with, so a change applies
  to the movements that start after it. With `linear` on, the adaptive duration's floor is
  the step time and its 500 ms ceiling rises to the step time when that is longer, and a
  movement in flight keeps its own duration as the ceiling when the step time is lowered.
  The setting returns to its default when the plugin is disabled or enabled, like `linear`.
- Add `smooth-movement stats [on|off|reset]`: counts frames, frames that reached the draw
  stage and tile repaints by the game, and (while on) times the render hook split into movement
  detection and drawing. Timing is off by default; counting costs one increment per frame,
  per painted frame and per repaint.
- Add `smooth-movement record <file> [frames]`, with `record stop` and `record status`:
  writes what the render hook reads each frame to a file that tools outside the game can
  replay (`frame_record.h` documents the format).
- Add `harness/`, which replays a recording through the plugin outside the game against stub
  headers, so two versions can be compared on the same frames (see `harness/README.md`).
  The recordings in `harness/recordings/` are fixed fixtures; `harness/test.sh` checks each
  replay against the per-frame draw digests in `harness/expected/`.
- Skip repaints of tiles that have nothing to paint. The game paints nothing for a tile whose
  25 per-tile entries are all zero, which is most tiles of a level below the camera, and any
  tile once the layers a stage hides are zeroed; the render hook now checks that before
  asking for the repaint. What appears on screen is unchanged.
- Hash the per-tile array signature in eight independent lanes. The render hook hashes every
  tracked array of every viewport each frame to tell a redrawn viewport from a repeated one,
  and one FNV-1a chain is a serial multiply per entry; eight chains over interleaved entries
  let the CPU overlap them. What the hook draws is unchanged.
- Skip the sweep for resting mirrored creatures on a viewport that has none. The sweep
  visited every tile of every viewport each frame while `flip` is on; the movement tracker
  already knows whether any tile of a viewport faces the mirrored way, and a level below the
  camera usually has none. What the hook draws is unchanged.
- Walk only the tiles that can carry a movement when collecting sprites to draw. The render
  hook read every layer of every tile of every viewport each frame and asked the movement
  tracker about each non-empty one; the tracker now lists the tiles its active movements can
  reach, and a viewport with none is skipped. What the hook draws is unchanged.
- Sweep only the tiles that face the mirrored way when repainting resting mirrored creatures.
  The sweep asked the movement tracker for the facing of every tile of a viewport that had
  any; the tracker now lists those tiles. What the hook draws is unchanged.
- Skip blank tiles before hiding layers while the free camera is off the tile grid. That
  glide visits every tile of every viewport each frame, and each visit first zeroed and
  restored the entries of the layers it hides and then found the tile blank; the hook now
  summarizes which tiles have any non-zero entry once per viewport per glide frame and skips
  the blank ones before touching them. What the hook draws is unchanged.
- Set smoothstep movement tweens to 150 ms. Add optional linear easing with
  adaptive 150–500 ms durations based on the cadence between consecutive steps
  (`smooth-movement linear on`) and icons for boulders, bars, and wood hauled by units
  (`smooth-movement hauled on`). Both flags are off by default.
- Mirror creature sprites horizontally so they face their direction of travel.
  Dwarf Fortress creature art natively faces west, so only creatures moving
  east are mirrored. Facing is sticky: only horizontal movement changes it,
  so walking north or south, and standing still, keep the last facing. Worn
  clothing and equipment flip with the creature because Dwarf Fortress
  composites them into a single tile sprite. Multi-tile creatures mirror as
  one composite, reflected about their anchor tile. Items, vehicles, and
  designations are never mirrored. Off by default; turn it on with
  `smooth-movement flip on`.

## 0.3.0 - 2026-08-03

- Target DFHack `develop` and reuse DFHack's SDL library handle instead of
  independently opening and closing SDL.
- Centralize viewport layer metadata, redraw stages, SDL bindings, and pending
  state cleanup to remove duplicated rendering policy.
- Animate creature status icons with their creature instead of letting their
  flashing texture fragments jump between tiles.
- Animate item-layer wheelbarrows and the vehicle layer used by minecarts;
  minecart sprite changes no longer interrupt interpolation, and consecutive
  steps retarget from the current fractional position instead of snapping back
  to the previous tile center.
- Optional free camera (`smooth-movement camera on`, off by default): map scrolls
  glide with an exponential catch-up, middle-mouse drag pans pixel-perfectly and
  can rest between tiles, and `camera <fx> <fy>` sets a persistent sub-tile
  offset. Render-only; the game's tile camera is untouched.
- Fix sprites floating while the camera pans. The scroll variables change at input time but the
  viewport buffers shift on a later render frame, where the shift used to read as a real creature
  move and started a bogus slide across the screen. The buffer shift is now detected directly
  (hypothesis-tested against the pending scroll delta): new-movement detection is suppressed while
  a pan is pending, and in-flight movements are translated on the frame the shift lands so they
  keep tracking the world. Zoom, Z-level, resize, and viewport changes still reset.

## 0.2.0 - 2026-07-28

- Add a Windows x86-64 build for DFHack 53.15-r2.
- Load SDL2 by its platform-specific library name.

## 0.1.0 - 2026-07-28

- Add smooth visual interpolation for adjacent creature movement.
- Preserve world layer ordering and render UI after animated creatures.
- Reset interpolation on camera, zoom, Z-level, resize, or viewport changes.
- Keep gameplay, simulation timing, and save data unchanged.
