# DF Smooth Movement

A visual plugin for Dwarf Fortress that makes movement smoother.

## Features

- **Smooth movement:** creatures, hauled items, and vehicles glide between tiles.
- **Synced icons:** status icons follow their creature while it moves.
- **Animated carts:** wheelbarrows and minecarts move smoothly too.
- **Sprites flip** creatures can optionally face the direction they are walking.
- **Walk bob:** creatures can optionally hop twice per step, like two footfalls, while they glide.
- **Adjustable step time:** how long a one-tile glide takes is a runtime setting.
- **Free camera:** the camera can optionally glide and be dragged with the mouse (WIP).

## Installation

1. Download the release archive for your operating system and DFHack version.
2. Extract it into the Dwarf Fortress/DFHack folder.
3. Check that the plugin is in one of these locations:
   - Linux: `hack/plugins/smooth-movement.plug.so`
   - Windows: `hack/plugins/smooth-movement.plug.dll`
4. Start Dwarf Fortress through DFHack and run this in the console:

```text
load smooth-movement
enable smooth-movement
```

## Useful commands

```text
smooth-movement             # show plugin status
disable smooth-movement     # disable the plugin
smooth-movement flip on     # enable sprites flip
smooth-movement camera on   # enable the free camera
smooth-movement timestep 150  # one-tile glide takes 150 ms (default 100, range 20-2000)
smooth-movement bob on      # enable the walk bob
smooth-movement bob 0.15    # bob height as a fraction of a tile (default 0.10); does not turn it on
smooth-movement bobmult 1 2.4 2.7  # bob multipliers for horizontal, diagonal, vertical steps
smooth-movement hops 1      # one hop per step instead of two
smooth-movement stats on    # count and time the plugin's frame work (stats to print, stats reset)
smooth-movement snapshot    # save the next painted frame(s) as a BMP in the game folder
smooth-movement trace 100   # log the next 100 detected movements to smooth-movement-trace.txt
```

### Walk bob

The bob is off by default and does not change how creatures glide; it only lifts the sprite
along the way. The height is `bob <amount>` times a per-direction multiplier: a step with a
vertical component already moves the sprite a whole tile up or down, which drowns a small hop,
so diagonal and straight vertical steps get larger multipliers by default. Every combination of
amount and multipliers is capped so the lift stays under one tile, because the plugin repaints
exactly one row above a bobbing sprite. On a row that is off the top of the screen, or on fire,
the whole creature glides without the bob. Status icons and carried items bob with their
creature. Like `flip` and `camera`, the bob settings return to their defaults when the plugin
is disabled.

## Development

The behaviour lives in headers that compile without DFHack or SDL, so it can be tested on any
machine: `visual_animation.h` (movement detection), `frame_render.h` (the paint pass),
`free_camera.h`, `view_context.h` (what resets the animation context) and `sdl_canvas.h` (the
batching in front of SDL). `smooth-movement.cpp` only binds them to the engine and parses the
console commands.

`tests/run.sh` builds and runs the unit tests, the paint-op oracle fuzzer and a render benchmark
with any C++17 compiler. The oracle under `tests/oracle/` is a regression fixture: the render
pass from before the redesign, ported onto the canvas interface, which the fuzzer requires to
paint every random frame identically to the current pass. It shows the redesign changed nothing
about what is painted; it does not check either pass against the engine's own draw order.

In game, `smooth-movement stats on` followed by `stats` after a while prints per-frame timings;
`stats detail on` adds timers around every engine repaint and SDL call at some cost of its own.
`smooth-movement snapshot [after] [count] [file]` saves the next frame the plugin paints, before
the interface is drawn over it (or after the engine's UI stage with `after`), as a BMP, to check
the result without a screen capture; `count` saves that many consecutive frames as `file-1`,
`file-2`, ... The path is relative to the game's working directory, an existing file is
overwritten, the request stays armed until a frame with a readable map viewport comes along,
and a new request is refused while one is armed. `smooth-movement trace [count]` appends
the next `count` detected movements (viewport, layer, texpos, from, to, resulting facing and
whether the game was paused) to `smooth-movement-trace.txt` in the same directory.

## Compatibility

Requires DFHack 53.16-r1.1 and the SDL 2D renderer. Always download the plugin version that matches your DFHack version.

## License

MIT. See [LICENSE](LICENSE).
