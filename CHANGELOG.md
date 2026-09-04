# Changelog

## Unreleased

- Fixed: at zooms other than 128 the engine's tiles are not a uniform stride apart, and
  sprites placed by multiples of the tile size drifted off their tile by up to a pixel per
  tile of distance (a mirrored fragment two tiles away sat one pixel into the next tile).
  Sprites are now placed on the engine's own tile positions at both ends of their path.
- The paint-op oracle (the pre-redesign render pass kept as a fixture) is replaced by
  `tests/render_fuzz.cpp`, which checks every painted random frame against the properties
  the pass owes the engine instead of against an older implementation; see the README.
  It found the zoom drift above. `tests/oracle/` is gone.
- Console commands no longer write the render thread's state while a frame is running.
  Every setting change (camera, time step, flipping, walk bob) is posted to a mailbox the
  render hook drains at the top of the next frame; with the plugin disabled it is applied on
  the spot. `disable` waits (up to 200 ms) for a frame already inside the hook to finish
  before the state is reset, and says so if it has to give up waiting. The console keeps its
  own copy of the settings, validates against it (so two commands in one frame, as from an
  init file, see each other), and seeds every reset with it: settings now survive
  `disable`/`enable`, and the camera stays on across them (its offset does not).
- Fixed: `camera <x> <y>` with the camera off landed a whole tile from what was asked and
  swallowed the next scroll. Enabling clears the camera's window baseline, and the
  normalization write went out before a baseline existed, so its landing was never observed.
  A normalization asked before the first update now waits for it.
- The render pass tells the canvas which repaint of a tile it asks for (the staging pass
  beneath the sprites, the pass above a sprite group, or the shading alone), so the property
  fuzzer checks each repaint against the pass the renderer named instead of inferring the
  pass from paint order.
- The plugin file's globals are one `plugin_statest`, sectioned by owner: bound at enable,
  console-set and render-read, the simulation-to-render draw handshake, and the render
  thread's own state, which is replaced whole when the plugin is reset. The snapshot
  command no longer writes the render thread's frame counters; it leaves a request.
- What a frame has to show (a movement in flight, a resting mirrored creature, a camera
  glide, or nothing) is decided in `frame_render.h` and unit tested; the plugin file only
  reads the answer. `stats` also counts the frames that found freshly drawn viewport buffers
  and the buffer draws in progress at either end of the render hook or started inside it,
  which is expected to stay at zero: the pass blanks and restores parts of the viewport
  buffers around each engine repaint, which is sound only while the simulation thread is not
  drawing into them.
- Fixed: a creature resting mirrored no longer flickers between its mirrored and native
  sprite while the game is paused. The engine redraws every map tile every frame before the
  plugin's pass runs (measured in game: one `update_viewport_tile` call per tile per frame with
  nothing changed), so nothing the plugin paints outlives its frame. Resting mirrored sprites
  are now painted every frame they exist; the bookkeeping that assumed painted sprites stayed
  on screen (the disturbed-rest check, last frame's coverage, the full-redraw gate and the
  forced full redraws on setting changes) is gone. With flipping on, a still frame with
  mirrored creatures costs one fill and one repaint per covered tile, a few microseconds.
- The plugin file is glue only. The free camera (`free_camera.h`), the view-context tracker
  (`view_context.h`) and the SDL canvas with its fill batching (`sdl_canvas.h`)
  are their own headers with unit tests, driven against scripted engines. The layers whose
  contents are followed between frames are a named list (`movement_tracked_layers`) checked
  against the layer descriptors at compile time.
- Fixed: sprites no longer flip or slide while the game is paused. A redraw at a simulation
  tick the buffers were already drawn at (the game showing the units sharing a tile in turn,
  blinking markers) is presentation, not a step, and is no longer read as movement. The tick
  is read where the buffers are drawn, in the map screens' render on the simulation thread.
- `smooth-movement snapshot [after] [count] [file]` saves the next painted frame, or `count`
  consecutive frames numbered `file-1`, `file-2`, ..., before the interface goes on top (or after
  the engine's UI stage with `after`) as BMPs in the game folder. `smooth-movement trace [count]` appends the next detected
  movements to `smooth-movement-trace.txt` in the game folder.
- Profiling is built in behind a runtime switch (`smooth-movement stats on|off|reset`,
  `stats detail on|off`); off, it costs one branch per counter.
- The test tooling lives in `tests/`: unit tests, the render property fuzzer and benchmarks,
  all driven by `tests/run.sh`.
- The render pass is rebuilt on components independent of the game: sprite
  collection, tile coverage, staged repaints and the frame pass live in their
  own headers, reuse their scratch across frames and allocate nothing per
  frame. Collecting the sprites and their coverage for a 120x70 view with 40
  moving creatures takes about 12 µs instead of 41 µs, and 87 µs instead of
  496 µs with 300 creatures in a 200x110 view. The painted output is unchanged
  except that a mirrored stationary sprite no longer blocks on fire in the
  tiles between its own tile and the one it is drawn on.
- With sprite flipping on, a frame without movement is painted only when the
  engine repainted a tile under a resting mirrored sprite (any buffer of a
  tile within the sprite's reach differs from its previous-frame twin), or
  after a full engine redraw. Before, every frame with a mirrored creature on
  screen was painted in full.
- The staged repaint takes about half as many engine repaints per tile. Each
  level repaints a covered tile once, beneath its sprites where it has any,
  and the shading (the interface layer) is painted with the repaint above the
  tile's last sprite group instead of in a pass of its own; a level without
  sprites on a tile repaints it whole. A paint-operation oracle keyed by pixel
  cell confirmed the painted result unchanged over 9381 random frames (since
  replaced by the property fuzzer), with one deliberate exception: the shading-only repaint after a tile's last sprite
  group now blanks the top shadow as well, so it is painted once, in engine
  order, instead of a second time over the sprites.
- A tile whose buffers are all zero at a level is no longer asked to repaint there: the
  engine paints nothing for it. Most tiles of the lower z-level viewports are
  such tiles, so with nine viewports the engine repaints per frame drop from
  about 185 to 22 and the plugin's render work from about 60 µs to 42 µs per
  frame. The painted result is unchanged.
- Idle frames cost less: the facing grid settles only when the buffers
  changed, and the black fills that blank tiles before a repaint are queued
  and handed to SDL in one call per batch instead of one call per tile.
- The frame pass no longer sweeps the screen: covered tiles are visited from
  the list of marked tiles instead of scanning the bounding box once per
  render group and level, and the tiles with a mirrored resting creature are
  listed once per frame when the facings settle instead of rescanned by every
  reader. Native, with a no-op engine, a 120x70 view with three levels and
  eight moving creatures takes about 10 µs per frame instead of 29 µs, and
  28 µs instead of 63 µs with forty creatures.
- Lower per-frame CPU cost: proxy collection visits only tiles near a
  movement instead of sweeping every layer of every viewport, the buffer
  signature covers only the current buffers and hashes them two tiles at a
  time in four independent lanes, and movements are indexed by tile so lookups
  no longer scan every movement. In a 75x50 view with eight lower z-level
  viewports the plugin's frame work dropped from about 1.24 ms to 0.08 ms;
  rendering is unchanged. A frame the game hands back with identical content
  no longer counts toward the scroll-landing tolerance or the settle window
  after a scroll is abandoned, so both are now independent of render frame
  rate.
- Optional walk bob (`smooth-movement bob on`, off by default): creature sprites
  hop twice per tile step while they glide, with `bob <amount>` for the height,
  `bobmult <horizontal> <diagonal> <vertical>` for per-direction multipliers and
  `hops 1|2` for the footfalls per step. Render-only; the glide itself is
  unchanged, and the lift is capped so it never leaves stale pixels above the
  path.
- The one-tile glide time is a runtime setting (`smooth-movement timestep <ms>`,
  default 100 ms). A movement keeps the time it started with.
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
