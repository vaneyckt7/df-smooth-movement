#!/usr/bin/env python3
"""Prints one line per frame of a `smooth-movement record` file, without the per-tile arrays.
Usage: recinfo.py <recording> [first frame] [last frame]

Per line: frame number; t, the frame clock in ms; the plugin's three switches;
movement, the movement by name; settings, the movement's settings as name=value pairs,
or - for none; step, the one-tile step time in ms; sim, the simulation's frame counter when
the game last filled the per-tile arrays, or -1 when no fill was seen since the previous frame; w, the window position x,y,z;
P when the
game was paused, - otherwise; follow, the followed unit id or -1;
mouse, the mouse position with M when the middle button was down; zoom; o, the drawing origin
in tiles; grid, the screen size in tiles; rest, the free camera's offset in tiles; units, how
many units were in view; painted or skipped, whether the hook drew this frame; repaints, the
tiles it asked the game to repaint; changed, the array entries the game changed while the
hook ran; vps, each active viewport as slot:width x height[clip x range,clip y range] and how
many of its 50 per-tile arrays were present. Slots 0-7 are lower-level viewports, 8 the main
one.

This script only reads the file; it runs none of the plugin's render code. A recording holds
two kinds of data. The frame header, the unit list and the end-of-hook record are small and
are what this script prints. The per-tile arrays of every viewport are the bulk of the file
and exist for the replay: `bench replay` (the harness in this directory) decodes them into a
stub viewport and runs the plugin's real render hook on them, then compares the repaints it
makes with the repaints recorded here. This script decodes each array's run lengths only to find
where it ends, and discards the entries. Use it to see what a recording contains before
replaying it: its length, whether the game was paused, which frames painted, and whether
anything changed under the hook."""
import struct, sys

TILE_ARRAYS = 50  # per-tile arrays per viewport, current then old, in for_each_tile_array order
SETTINGS = ['flip', 'hauled', 'camera']  # the frame header's switches, in file order
WORD = [4, 8, 4, 4, 4, 4, 8, 4] + [4] * 17  # element size per current array, repeated for old
WORD = WORD + WORD


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def i64(self):
        v = struct.unpack_from('<q', self.d, self.p)[0]
        self.p += 8
        return v

    def f64(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]
        self.p += 8
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]
        self.p += 4
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]
        self.p += 4
        return v

    def varint(self):
        v = shift = 0
        while True:
            b = self.d[self.p]
            self.p += 1
            v |= (b & 0x7f) << shift
            if not b & 0x80:
                return v
            shift += 7

    def skip_words(self, count, size):
        done = 0
        while done < count:
            v = self.varint()
            length, op = v >> 2, v & 3
            if op == 2:
                self.p += length * size
            done += length


def main():
    data = open(sys.argv[1], 'rb').read()
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    last = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
    r = Reader(data)
    assert data[:4] == b'SMRC', 'not a recording'
    r.p = 4
    version = r.u32()
    assert version == 7, f'recording version {version}, this script reads version 7 only'
    print('version', version)
    n = 0
    while r.p < len(data):
        assert r.u8() == ord('F')
        flags = [r.u8() for _ in range(3)]
        settings = ' '.join(f'{name}={value}' for name, value in zip(SETTINGS, flags))
        def name():
            length = r.u8()
            text = data[r.p:r.p + length].decode()
            r.p += length
            return text
        movement = name()
        movement_settings = [(name(), r.f32()) for _ in range(r.u8())]
        step = r.u32()
        sim = r.i64()
        settings += f' movement={movement}'
        settings += ' settings=' + (','.join(f'{n}={v:g}' for n, v in movement_settings) or '-')
        tick = r.u32()
        wx, wy, wz = r.i32(), r.i32(), r.i32()
        paused = r.u8()
        follow = r.i32()
        mx, my = r.i32(), r.i32()
        mbut = r.u8()
        zoom, ox, oy, dimx, dimy = (r.i32() for _ in range(5))
        rx, ry = r.f64(), r.f64()
        vps = []
        for _ in range(r.u8()):
            slot = r.u8()
            dx, dy, cx0, cx1, cy0, cy1, sx, sy = (r.i32() for _ in range(8))
            present = 0
            for b in range(TILE_ARRAYS):
                if r.u8():
                    present += 1
                    r.skip_words(dx * dy, WORD[b])
            vps.append(f'{slot}:{dx}x{dy}[{cx0}-{cx1},{cy0}-{cy1}]b{present}')
        units = r.u32()
        r.p += units * 17
        assert r.u8() == ord('E')
        repaints = r.u32()
        painted = r.u8()
        changed = r.u32()
        if first <= n <= last:
            print(f'{n:5d} t={tick} {settings} step={step} sim={sim} w={wx},{wy},{wz} '
                  f'{"P" if paused else "-"} '
                  f'follow={follow} mouse={mx},{my}{"M" if mbut else ""} zoom={zoom} o={ox},{oy} '
                  f'grid={dimx}x{dimy} rest={rx:g},{ry:g} units={units} {"painted" if painted else "skipped"} '
                  f'repaints={repaints} changed={changed} vps={" ".join(vps)}')
        n += 1
    print('frames', n)


if __name__ == '__main__':
    main()
