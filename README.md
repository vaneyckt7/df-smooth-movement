# DF Smooth Movement

A visual plugin for Dwarf Fortress that makes movement smoother.

## Features

- **Smooth movement:** creatures, hauled raw materials, and vehicles glide between tiles.
- **Sprites flip** creatures can optionally face the direction they are walking.
- **Smooth native follow:** Dwarf Fortress's unit follow glides automatically.
- **Adjustable step time:** how long a one-tile glide takes is a runtime setting.
- **Walk hop:** creatures can optionally hop twice per step, like footfalls, while they glide.
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
smooth-movement all on      # enable flip, linear and hauled (not the WIP free camera or stats)
smooth-movement all off     # disable flip, linear and hauled
smooth-movement flip on     # enable sprites flip
smooth-movement camera on   # enable the free camera
smooth-movement linear on   # use linear easing with adaptive <timestep>–500 ms movement tweens
smooth-movement timestep 200 # a one-tile glide takes 200 ms (default 150, range 20-2000)
smooth-movement hauled on   # show icons for carried boulders, bars, and wood
smooth-movement hop on      # enable the walk hop
smooth-movement hop 0.15    # hop height in tiles (default 0.10); does not turn the hop on
smooth-movement hopmult 1 2.4 2.7  # hop multipliers for horizontal, diagonal, vertical steps
smooth-movement hops 1      # one hop per step instead of two
smooth-movement stats on    # time the render hook; `smooth-movement stats` prints the numbers
smooth-movement record f.rec # write what the render hook sees for the next 900 frames to f.rec
```

### Walk hop

The hop is off by default and does not change how creatures glide; it only lifts the sprite
along the way. The height is `hop <amount>` times a per-direction multiplier: a step with a
vertical component already moves the sprite a whole tile up or down, which drowns a small hop,
so diagonal and straight vertical steps get larger multipliers by default. Every combination of
amount and multipliers is capped so the lift stays under one tile, because the plugin repaints
exactly one row above a hopping sprite. On a row that is off the top of the screen, or on fire,
the whole creature glides without the hop. Carried item icons hop with their creature; vehicles
never hop. `all on|off` leaves the hop alone. Like `flip` and `hauled`, the hop settings return
to their defaults when the plugin is disabled.

## Compatibility

Requires DFHack 53.16-r1.1 and the SDL 2D renderer. Always download the plugin version that matches your DFHack version.

## License

MIT. See [LICENSE](LICENSE).
