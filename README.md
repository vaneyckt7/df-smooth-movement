# DF Smooth Movement

A visual plugin for Dwarf Fortress that makes movement smoother.

## Features

- **Smooth movement:** creatures, hauled raw materials, and vehicles glide between tiles.
- **Sprites flip** creatures can optionally face the direction they are walking.
- **Smooth native follow:** Dwarf Fortress's unit follow glides automatically.
- **Adjustable step time:** how long the first step of a walk takes is a runtime setting; the steps after follow the creature's own pace.
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
smooth-movement status      # show the plugin's settings; bare `smooth-movement` prints the usage
disable smooth-movement     # disable the plugin
smooth-movement all on      # enable flip and hauled (not the interpolation, the WIP free camera or stats)
smooth-movement all off     # disable flip and hauled
smooth-movement flip on     # enable sprites flip
smooth-movement camera on   # enable the free camera
smooth-movement interpolation hop  # how a step is spread over frames: smoothstep|linear|hop
smooth-movement interpolation linear  # constant speed on the line
smooth-movement timestep 200 # the first step of a walk takes 200 ms (default 150, range 20-2000)
smooth-movement hauled on   # show icons for carried boulders, bars, and wood
smooth-movement hop 0.15    # hop height in tiles (default 0.10); does not pick the hop
smooth-movement hopmult 1 2.4 2.7  # hop multipliers for horizontal, diagonal, vertical steps
smooth-movement hops 1      # one hop per step instead of two
smooth-movement stats on    # time the render hook; `smooth-movement stats` prints the numbers
smooth-movement record f.rec # write what the render hook sees for the next 900 frames to f.rec
```

### Interpolation

The game moves a creature a whole tile at once; the plugin spreads that step over frames. An
interpolation is a movement: given the moment of the step and the step as a tile delta, it says
where the sprite is, as an offset in tiles from the old tile. There are three: `smoothstep`, the
default, slows at both ends of the step and stays on the line between the tiles; `linear` keeps
one speed on the line; `hop` starts and lands like `smoothstep` and hops above the line on the
way, like footfalls. `interpolation <name>` picks one; bare `interpolation` prints the current
one and the names. The interpolation is kept when the plugin is disabled and enabled again.

Whatever the interpolation, a creature's walk keeps its own pace. The first step of a walk
takes the `timestep`; a step that continues an earlier one takes the time since that step
started, its cadence, up to 500 ms or the timestep when that is longer. So a creature that
steps every 100 ms glides each tile in 100 ms and lands as the next step arrives, whatever the
timestep, and a creature that steps every 300 ms glides without a pause between its steps.

A movement owns its settings, named numbers the console sets by name and a recording stores by
name; `smoothstep` and `linear` have none. A movement also declares its overshoot, how far beyond
the straight path it can take the sprite in each direction, and the plugin erases and repaints
that many extra tiles around every moving sprite. When a tile within the overshoot is off the
edge of the screen, or on fire, the creature glides on the line instead for that step, at the
movement's own pace. A movement may also reach the tile before the step's time is up and hold
there; the plugin then stops drawing the step, since the game draws the creature on its tile.

### Walk hop

The hop is off by default (the interpolation is `smoothstep`); `interpolation hop` picks it.
Its settings are `amount`, the height of a hop in tiles (`hop <amount>`), `horizontal`, `diagonal`
and `vertical`, the multipliers for the step's direction (`hopmult`), and `hops` per step. A step
with a vertical component already moves the sprite a whole tile up or down, which drowns a small
hop, so diagonal and straight vertical steps get larger multipliers by default. The overshoot of
the hop is the amount times its largest multiplier.
Carried item icons hop with their creature; vehicles never hop. `all on|off` switches `flip`
and `hauled` and leaves the interpolation alone. Like `flip` and `hauled`, the hop's settings
return to their defaults when the plugin is disabled; the interpolation, `hop` included, is kept.

## Compatibility

Requires DFHack 53.16-r1.1 and the SDL 2D renderer. Always download the plugin version that matches your DFHack version.

## License

MIT. See [LICENSE](LICENSE).
