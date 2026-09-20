# Changelog

## 0.5.0 - unreleased

The plugin reports this version (`plugin_version` in `smooth-movement.cpp`); there is no
`v0.5.0` tag, so everything in this section is still unreleased.

- Handing a sprite to SDL moved out of `smooth-movement.cpp` into `sprite_drawing.h`: the
  rectangle a gliding sprite is copied into, the inset rectangle a hauled item's icon gets,
  and the choice between SDL's plain copy and the one that can flip a sprite horizontally.
  The functions take the plugin's table of SDL functions as an argument rather than reading
  the plugin's state, so a harness test can fill that table with stand-ins that record what
  was asked for. It now covers the mirror shift in pixels and which of the two calls a
  flipped sprite takes, neither of which any unit test reached before, only the replays,
  and it covers the icon's insets being applied to the placed corner rather than to a tile
  corner -- `carried_item_icon_rect` itself was already tested on its own. Nothing the
  plugin draws changes.

- The pixel arithmetic that places a sprite on screen moved out of `smooth-movement.cpp`
  into `sprite_placement.h`: the tile size at a zoom, a tile's edge in pixels, and the
  corner a gliding sprite's rectangle starts at. Like the other split headers it needs
  nothing from DFHack, so a harness test now covers it. Every recording in the repository
  was made at zoom 192, where a tile is exactly 48 pixels, so the replays had only ever run
  this at that one zoom, never at the default zoom and never at a zoom whose tile size is
  not a whole number of pixels; the test covers all three. Nothing the plugin draws changes.

- A recording's reader refuses a recording whose nine viewport slots claim more than eight
  million tiles between them. Each viewport's width and height were already capped at 4096,
  but nothing bounded what they claimed in total, and the replay sizes a slot's fifty
  per-tile arrays from the numbers in that slot's viewport header before it reads anything
  into them, then keeps that storage from frame to frame. A run of zeros costs a few bytes
  however long it is, so a 2660-byte file of nine viewports at the per-dimension cap asked
  the replay for 30.4 GiB, which on a machine that cannot give it is a crash in the harness
  rather than a rejected recording. What is charged is the largest each slot has ever been,
  added up, which is what the replay actually holds: a slot drawn in every frame is charged
  once, so the length of a recording costs nothing, and a slot that only a later frame uses
  is charged when it appears. The cap is 1.69 GiB of per-tile arrays, against 3825 tiles in
  the largest frame of the recordings in this repository and 8.3 million for nine viewports
  each filling a 5120 by 2880 display at four-pixel tiles. No format change: the reader
  refuses more than it did, and every recording that read before still reads.

- Every `camera` command that changes something replies with the camera's line, and
  `record stop` refuses when nothing is recording. A refused offset still prints only its
  reason, since it has not touched the camera.
  `camera on`, `camera off`, `camera reset` and `camera <east> <south>`
  used to change the camera and print nothing at all, alone among the settings, so there was
  no way to tell an offset that was accepted from one that was silently refused, and no way
  to see the part of an offset over half a tile that scrolling takes rather than the camera.
  The reply is the line `status` prints, from one place, so the two can no longer say the
  same state differently: bare `camera` used to leave off the
  `(tiles east/south of the grid)` gloss that `status` carried. A rest of zero now reads as
  `0.000` rather than `-0.000`, which is what negating a positive zero gives. `record stop`
  used to confirm a stop whether or not a recording was running, which is how a `record` that
  failed to start looks to anyone who then stops it; it now says `no recording is running`
  and fails, deciding that under the one lock that ends the recording rather than asking
  first and acting after. A recording that reached its own frame count has already ended, so
  a `record stop` after that is refused too; `record status` still reports how many frames it
  wrote and where.

- `harness/recordings/` holds four recorded scenes again, and `harness/expected/` the 1500
  digest lines that go with them, so the harness checks what the plugin draws on a real
  fortress rather than only on the three frames the codec test generates. They are
  `fortress-600` (the free camera off, mirrored sprites, the hop's extra repainted row),
  `fortress-camera-200` (the free camera on, resting at -0.3, 0.2 tiles), `fortress-hauled-400`
  (hauled item icons, which no earlier fixture covered) and `fortress-smoothstep-300` (the
  movement a new user is told to pick). All four were recorded in Dwarf Fortress 53.16 on
  the version that made the digests, so each one's self-check repaint count matches the
  replay's instead of only being informative. The CI workflow no longer sets
  `HARNESS_ALLOW_NO_RECORDINGS`; `test.sh` still honours it for a tree that has none.
  This replaces the two fixtures `b4c32fc` deleted, which were format 2 and unreadable. Note
  that the format version alone does not make a recording readable: a version 7 file written
  before the hop's settings were renamed is refused with `bad movement settings`, because the
  setting names inside it are not the ones this version knows.

- `harness/build.sh` and `harness/test.sh` compile with `-ffp-contract=off`, placed ahead of
  `CXXFLAGS` so a caller can still override it. A digest hashes coordinates printed to three
  decimals, so a build that contracts a multiply and a following add into one fused
  instruction, rounding once where the pair rounds twice, digests the same scene differently
  from one that does not. Both compilers contract by default; what decides it is the target,
  since arm64 has a fused multiply-add in its baseline and x86-64 does not have one without
  `-mfma`. The same 19 sites in the harness binary fuse under clang on arm64 and none under
  GCC on x86-64, and one draw of `fortress-hauled-400` came out at `x=146.340` fused against
  `x=146.339` unfused, which failed that frame on a scene where nothing had changed. Off is
  the reachable direction, since every target can do a separate multiply and add. With it the
  four recordings trace identically on both machines. The `fortress-hauled-400` digest for
  that one frame is regenerated to match.

- The vocabulary the movement interface introduced is used everywhere, so that one thing has
  one name. `harness/recinfo.py` prints a frame's movement as `movement=` where it said
  `interpolation=`, and `harness/test.sh`, which reads that field, changes with it. In the
  plugin, `render_interpolated_world`, `draw_interpolation_stages` and
  `draw_viewport_interpolation_stages` become `render_world_with_movement`,
  `draw_movement_stages` and `draw_viewport_movement_stages`. The viewports' per-tile arrays
  are called that rather than buffers: `drawn_buffersst`, `buffer_signature`, `buffer_tick`
  and `buffers_advanced` become `drawn_arraysst`, `array_signature`, `array_tick` and
  `arrays_advanced`, and about thirty comments follow. Names only; nothing the plugin draws,
  records or prints changes, apart from that one field of `recinfo.py`.

- `harness/test.sh` fails when `harness/recordings/` is empty instead of passing. The replay
  of a recorded scene against `harness/expected/` is the only check that a change leaves the
  drawing alone on a real fortress, and the directory has been empty since the fixtures were
  deleted, so the check has been running zero times and reporting success. The rest of the
  harness still runs with `HARNESS_ALLOW_NO_RECORDINGS=1`, which the workflow sets with a
  note saying why, so that the gap is written down where it can be found rather than living
  in a log nobody reads while the job is green.

- Movements are one named choice. How a creature is paced, and where it is drawn along the
  way, is a movement picked by name: `movement none`, `movement smoothstep`,
  `movement linear` or `movement hop`. `movement <name> <setting> <value>` sets one of that
  movement's settings, `movement <name> <setting>` prints one, `movement <name>` prints them
  all, and `movement` alone lists the names. The default is `none`, which draws every creature
  at its tile exactly as the game does, so a freshly enabled plugin changes nothing on screen
  until a movement is picked. This replaces the `linear on|off` flag, which is now the
  movement named `linear`, and the walk hop's own flag, which is now the movement named `hop`.

- `smooth-movement status` prints the plugin's settings, and bare `smooth-movement` prints the
  usage. Every setting's bare command prints the same line that `status` lists for it.

- Frame recordings name the movement in force and carry its settings (format version 7), so a
  recording replays with the movement it was made with. Version 6 was never used: the format
  went from 5 to 7 in one step. The reader accepts version 7 only, where it used to read every
  version from 2; recordings in an older format are not read at all, which is why the two
  fixtures that were in `harness/recordings/` were removed rather than converted.

- `camera <east> <south>` checks that its two offsets are decimal numbers before it uses
  them. It used to hand each word straight to `std::stod`, which accepts `nan`: a NaN then
  failed the -0.99..0.99 range test the way it fails every comparison, so it was accepted
  and fed `std::lround` every frame until another camera command replaced it. `std::stod`
  also stops at the first character it cannot read, so `camera 0.5abc 0` silently meant
  `0.5`. Both now read as a usage error, like `camera east 0` always has. An offset is an
  optional leading minus then digits with at most one point, which is the rule `timestep`
  and the hop settings already follow. A leading plus is the one spelling that used to work
  and now does not: `std::stod` read it as a sign, so `camera +0.5 0` meant `camera 0.5 0`.
  Write the offset without it.

- A camera offset and a hop setting keep their decimal point whatever the process's locale
  says one looks like. `std::stod` and `std::stof` take the point from `LC_NUMERIC`: under a
  locale whose decimal separator is a comma they stop at the `.`, so `camera 0.99 0` quietly
  meant `camera 0 0`, and `movement hop hop-height 0.5` read as `0`, which is outside the
  `(0, 1]` a hop height must be in and so was refused outright. On a leading point like `.5`
  they threw instead, and nothing on the command path catches that. The digits are now read
  where the command's own rule for them is written, which needs no locale. Nothing in the
  plugin sets one, but DFHack opens Lua's standard library, so a script calling
  `os.setlocale` moves it for the whole process.

- The sprite sweep asks whether a tile is inside the viewport's per-tile arrays before it
  reads one, covers the tile or asks the game to repaint it. It used to ask only whether the
  tile was inside the clip rectangle, which is a different rectangle: nothing derives clipx
  and clipy from dim_x and dim_y, and nothing keeps the two in step. A tile one column past
  the last one indexes a whole row into the next array, which the repaint then writes through
  for as many as twenty-five arrays, every per-tile array the game draws a tile from, so an
  accepted tile outside the arrays is a write and not only a stray read. Every place that
  asked `inside_clip` now asks `paintable_tile`, which is the clip test and the array bounds
  together, and `has_fire` checks the bounds before it reads the spatter flags. No recorded
  scene has a viewport whose clip reaches outside its arrays: over the two
  recordings kept for the harness, all 7047 viewport headers have a clip exactly equal to the
  array rectangle, so on those scenes the two tests agree on every tile and nothing the plugin
  draws changes. This is a bound the sweep was missing, not a fault seen in a game.
- `movement <name>` with a name the plugin does not have says so and lists the names it does
  have, where it used to print only the usage, which names `<name>` without saying which
  names exist. `movement <name> <setting> <value>` checks the setting name before it looks at
  the value, so a misspelt setting reads the same whether or not the value that follows it
  parses. Every command that worked before prints what it printed before.
- A recording that ends mid-field is reported as truncated rather than read past its end: the
  reader's remaining-bytes check no longer overflows when a length read out of the file is
  larger than the file. Recordings the plugin writes are unaffected.

- Names in the movement path say their unit: `_ms` for milliseconds, `_tiles` for tiles, `_px` for pixels, `_pct` for a fraction from 0 to 1. Nothing the plugin draws or records changes.
- Fix the free camera sitting a whole tile off after two camera commands within one frame of each other in a running game (`camera <x> <y>` followed at once by `camera reset` or by another `camera <x> <y>`): a command now asks for its offset against the window as written, so a window write of the camera's own that has not landed yet is taken off the offset and comes back when it lands.
- Fix the free camera sitting one tile off after `camera <x> <y>` in a paused game, or when a recording started, the followed unit changed, the z-level or zoom changed, or the window was resized within a frame or two of the command. The camera folds whole tiles of the offset into the game's window and used to wait for that scroll to land before moving the offset the other way; a restart of the camera's tracking (every paused frame, a recording's first frame, a change of followed unit) forgot the wait, and the offset never moved. The restart now folds the write into the offset at once.
- The scroll detector counts the votes for a view shift over the background and over the sprite layers with one loop. Nothing the plugin draws changes.
- The console commands read `on` and `off` through one parser, and a setting's bare command prints the same line the bare `smooth-movement` lists for it. Every command prints what it printed before.
- `harness/test.sh` builds and runs each harness test through one shell function. Nothing the plugin does changes.
- `sdl_apist::clear` assigns a fresh value instead of listing every binding. Nothing the plugin does changes.
- Both map screen interposes note the simulation tick through `note_map_render`, and the three hooks are applied and removed by `apply_hooks` and `remove_hooks`. Nothing the plugin does changes.
- Every request for a full redraw by the game goes through `full_redraw`, which the console commands already used. Nothing the plugin does changes.
- The repaint passes of `tile_repaint.h` get the callable that asks the game for a tile repaint from one function, `staged_repainter`. Nothing the plugin draws changes.
- The render hook looks the units in view up through one function, `units_in_view`, for the frame recorder and for the hauled item icons. Nothing the plugin draws or records changes.
- A creature sprite and a hauled item's icon find their place on screen through one function, `place_sprite`, instead of each working out the glide and the hop lift. Nothing the plugin draws changes.
- The tile size on screen is computed by one function, `tile_size_px`, where four places in the render hook each had the formula. Nothing the plugin draws changes.
- `harness/test.sh` builds and runs the animation manager test with the other unit tests;
  it prints a line when it passes, like them. Nothing the plugin does changes.
- The match that makes a hauled item's icon hop with its carrier moved out of the render
  hook into `sprite_proxies.h`, beside the rule that decides which sprites hop, and is
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
- Frame recordings carry the walk hop settings (format version 5), so a recording made
  with the hop on replays with it.
- Added: the walk hop, the movement named `hop`. It lifts every gliding creature sprite twice
  per step, like two footfalls, by `hop-height` (default 0.10 tile) times a multiplier for the
  step's direction (`horizontal-mult`, `diagonal-mult` and `vertical-mult`, default 1, 2.4 and
  2.7); `hops-per-step 1` gives a single bounce. Carried item icons hop with their creature,
  vehicles never do, and a creature whose row above is off the screen or burning glides
  without the hop. It arrived as a `bob on|off` flag and was renamed before release; no `bob`
  command exists.
- Fixed: a repaint while the game is paused can no longer be read as a step, so sprites do
  not slide or flip with the simulation standing still. A redraw at a simulation tick the
  per-tile arrays were already drawn at (the game showing the units sharing a tile in turn,
  blinking markers) is presentation, not a step, and starts no movement.
  The tick is read where the arrays are filled, in the map screens' render on the simulation
  thread. Frame recordings carry it (format version 4).
- Frame recordings carry the one-tile step time (format version 3), so a recording made at
  a `timestep` other than the default replays at that step.
- Add `smooth-movement timestep <ms>`: how long a one-tile glide takes, 20 to 2000 ms,
  150 by default as before. A movement keeps the time it started with, so a change applies
  to the movements that start after it. A step that continues an earlier step instead lasts
  the time since that step started, its cadence, whichever movement is in force, clamped
  between 1 ms and 500 ms or the step time when that is longer; a movement already in flight
  keeps its own duration as that limit's floor, so lowering the step time never cuts it
  short. The setting returns to its default when the plugin is disabled or enabled.
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

## 0.5.0-beta - 2026-09-04

- Set smoothstep movement tweens to 150 ms. Add optional linear easing with
  adaptive 150–500 ms durations based on the cadence between consecutive steps
  (`smooth-movement linear on`) and icons for boulders, bars, and wood hauled by units
  (`smooth-movement hauled on`). Both flags are off by default.

## 0.4.1 - 2026-08-06

Written after the fact from the commits between `v0.4.0` and `v0.4.1`: no section was
written when the tag was made, and the plugin still reported 0.3.0 at it.

- Stop overdrawing upper viewports, and leave a viewport the game cannot draw readably alone.
- Draw the z-level shading over the gliding sprites rather than under them.
- Add the tag release workflow.

## 0.4.0 - 2026-08-05

Written after the fact from the commits between `v0.3.0` and `v0.4.0`: no section was written
when the tag was made, and the plugin still reported 0.3.0 at it. The camera-pan fix listed
under 0.3.0 was refined by further commits in this range.

- Target DFHack 53.16-r1.
- Animate viewports on lower z-levels.
- Remove construction transitions.
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
  minecart sprite changes no longer interrupt a movement, and consecutive
  steps retarget from the current fractional position instead of snapping back
  to the previous tile center.
- Optional free camera (`smooth-movement camera on`, off by default): map scrolls
  glide with an exponential catch-up, middle-mouse drag pans pixel-perfectly and
  can rest between tiles, and `camera <fx> <fy>` sets a persistent sub-tile
  offset. Render-only; the game's tile camera is untouched.
- Fix sprites floating while the camera pans. The scroll variables change at input time but the
  viewports' per-tile arrays shift on a later render frame, where the shift used to read as a real
  creature move and started a bogus slide across the screen. The array shift is now detected directly
  (hypothesis-tested against the pending scroll delta): new-movement detection is suppressed while
  a pan is pending, and in-flight movements are translated on the frame the shift lands so they
  keep tracking the world. Zoom, Z-level, resize, and viewport changes still reset.

## 0.2.0 - 2026-07-28

- Add a Windows x86-64 build for DFHack 53.15-r2.
- Load SDL2 by its platform-specific library name.

## 0.1.0 - 2026-07-28

- Add smooth visual movement for adjacent creature steps.
- Preserve world layer ordering and render UI after animated creatures.
- Reset movement on camera, zoom, Z-level, resize, or viewport changes.
- Keep gameplay, simulation timing, and save data unchanged.
