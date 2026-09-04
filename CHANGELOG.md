# Changelog

## Unreleased

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
