# DF Smooth Movement

A visual plugin for Dwarf Fortress that makes movement smoother.

## Features

- **Smooth movement:** creatures, hauled raw materials, and vehicles glide between tiles.
  Which glide they use is a setting with four choices — `none`, `smoothstep`, `linear` and
  `hop` — and a freshly loaded plugin starts at `none`, which is the game's own behaviour.
  Pick one with `smooth-movement movement <name>`; see [Movements](#movements).
- **Sprites flip:** creatures can optionally face the direction they are walking.
- **Smooth native follow:** Dwarf Fortress's unit follow glides automatically.
- **Adjustable step time:** how long a one-tile glide takes is a runtime setting.
- **Walk hop:** the `hop` movement lifts creatures as they glide, like footfalls.
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
smooth-movement movement smoothstep
```

The third line is what turns the smoothing on. Without it the plugin is enabled but its
movement is `none`, so creatures step as the game draws them.

## Commands

Every command starts with `smooth-movement`. A bare `smooth-movement` prints the usage. A
command naming a setting without a value prints that setting's current value instead of
changing it.

Run these after `enable smooth-movement`, not before. The console accepts them while the
plugin is disabled and says it did, but enabling the plugin puts every setting back to its
default, so anything set beforehand is silently lost. The one setting that survives is the
choice of movement.

```text
smooth-movement status              # print every setting and its current value
disable smooth-movement             # disable the plugin
```

### Movements

```text
smooth-movement movement            # print the current movement, and the names of all four
smooth-movement movement none       # the game's own behaviour: creatures step tile to tile
smooth-movement movement smoothstep # glide, easing in and out (the usual choice)
smooth-movement movement linear     # glide at a constant speed
smooth-movement movement hop        # glide, lifting the sprite like footfalls
```

A movement keeps its own settings while another one is current, and the choice of movement
survives disabling and re-enabling the plugin. The settings do not: those go back to their
defaults with everything else. Setting a value on a movement does not select that movement.

```text
smooth-movement movement hop hop-height        # print one setting's value
smooth-movement movement hop hop-height 0.15   # set it
```

| setting           | what it does                              | default | accepted        |
| ----------------- | ----------------------------------------- | ------- | --------------- |
| `hop-height`      | how far the sprite lifts, in tiles        | 0.10    | over 0, up to 1 |
| `horizontal-mult` | multiplies the lift on a sideways step    | 1.0     | 0 to 5          |
| `diagonal-mult`   | multiplies the lift on a diagonal step    | 2.4     | 0 to 5          |
| `vertical-mult`   | multiplies the lift on an up or down step | 2.7     | 0 to 5          |
| `hops-per-step`   | how many hops one tile of walking takes   | 2       | 1 or 2          |

`hop` is the only movement with settings of its own. `none`, `smoothstep` and `linear` have
none.

### Everything else

```text
smooth-movement timestep            # print the one-tile step time
smooth-movement timestep 200        # a one-tile glide takes 200 ms (default 150, range 20-2000)
smooth-movement flip                # print whether sprites flip
smooth-movement flip on|off         # creatures face the direction they walk
smooth-movement hauled              # print whether hauled icons are shown
smooth-movement hauled on|off       # show icons for carried boulders, bars, and wood
smooth-movement all on|off          # flip and hauled together; leaves everything else alone
smooth-movement camera              # print the free camera's state
smooth-movement camera on|off       # the free camera: map scrolls glide, middle-mouse drag pans
smooth-movement camera reset        # clear the sub-tile offset
smooth-movement camera 0.25 -0.25   # rest this far east and south of the tile grid, in tiles
smooth-movement stats               # print the render hook's frame timings
smooth-movement stats on|off        # start or stop timing the render hook
smooth-movement stats reset         # clear the numbers collected so far
smooth-movement record f.rec        # write what the render hook sees for 900 frames to f.rec
smooth-movement record f.rec 300    # the same, for 300 frames (1 or more)
smooth-movement record stop         # end a running recording early
smooth-movement record status       # whether a recording is running, and how far along
```

`all on|off` covers `flip` and `hauled` and nothing else: not the movement, not the free
camera, not the stats, not a recording. `record` is the one command that needs the plugin
already enabled; it refuses otherwise. A `camera <fx> <fy>` offset is a fraction of a tile
between -0.99 and 0.99 on each axis, and setting one turns the free camera on.

### Walk hop

The hop does not change how far or how fast a creature glides; it only lifts the sprite
along the way. The lift is `hop-height` tiles times a per-direction multiplier. A step with
a vertical component already moves the sprite a whole tile up or down, which drowns a small
hop, so diagonal and straight vertical steps get larger multipliers by default.

A hopping sprite rises above the tile it is drawn on, so the plugin repaints as many rows
above it as the tallest lift its current settings can produce, rounded up to a whole row.
At the defaults that is one row; at the largest values the settings accept — `hop-height 1`
with a multiplier of 5 — it is five. On a row that is off the top of the screen, or on
fire, the whole creature glides without the hop. Carried item icons hop with their creature;
vehicles never hop.

## Compatibility

Requires the SDL 2D renderer. Each release is built against the DFHack release that was
current when it was tagged, and the DFHack version is part of the archive's file name.
Always download the plugin archive that names your DFHack version.

## License

MIT. See [LICENSE](LICENSE).
