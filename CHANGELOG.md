# Changelog

## Unreleased

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
