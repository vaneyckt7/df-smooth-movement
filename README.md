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

## Compatibility

Requires DFHack 53.16-r1.1 and the SDL 2D renderer. Always download the plugin version that matches your DFHack version.

## License

MIT. See [LICENSE](LICENSE).
