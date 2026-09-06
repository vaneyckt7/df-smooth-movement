#!/usr/bin/env python3
"""Prints one line per frame of a `smooth-movement record` file, without the per-tile arrays.
Usage: recinfo.py <recording> [first frame] [last frame]

    $ harness/recinfo.py harness/out/fortress.rec 0 9
    version 2
        0 t=706920620 flip=1 hauled=0 camera=0 linear=0 w=76,83,158 P follow=-1 mouse=-1,-1 zoom=192 o=0,4 grid=150x66 rest=0,0 units=10 skipped repaints=0 changed=0 vps=0:25x17[0-24,0-16]b50 ... 8:25x17[0-24,0-16]b50
        2 t=706921277 flip=1 hauled=0 camera=0 linear=0 w=76,83,158 - follow=-1 mouse=-1,-1 zoom=192 o=0,4 grid=150x66 rest=0,0 units=10 skipped repaints=0 changed=0 vps=0:25x17[0-24,0-16]b50 ... 8:25x17[0-24,0-16]b50
        9 t=706921460 flip=1 hauled=0 camera=0 linear=0 w=76,83,158 - follow=-1 mouse=-1,-1 zoom=192 o=0,4 grid=150x66 rest=0,0 units=10 painted repaints=38 changed=0 vps=0:25x17[0-24,0-16]b50 ... 8:25x17[0-24,0-16]b50
    frames 600

Per line: frame number; t, the frame clock in ms; the plugin's four settings; w, the window
position x,y,z; P when the game was paused, - otherwise; follow, the followed unit id or -1;
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
and exist for a replay tool, which decodes them into a viewport and runs the plugin's real
render hook on them, then compares the repaints it makes with the repaints recorded here.
This script decodes each array's run lengths only to find
where it ends, and discards the entries. Use it to see what a recording contains before
replaying it: its length, whether the game was paused, which frames painted, and whether
anything changed under the hook."""
import struct, sys

BUFFERS = 50
SETTINGS = ['flip', 'hauled', 'camera', 'linear']  # the frame header's settings, in file order
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

    def f64(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]
        self.p += 8
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
    assert version == 2, f'recording version {version}, this script reads 2'
    print('version', version)
    n = 0
    while r.p < len(data):
        assert r.u8() == ord('F')
        flags = [r.u8() for _ in range(4)]
        settings = ' '.join(f'{name}={value}' for name, value in zip(SETTINGS, flags))
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
            for b in range(BUFFERS):
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
            print(f'{n:5d} t={tick} {settings} w={wx},{wy},{wz} {"P" if paused else "-"} '
                  f'follow={follow} mouse={mx},{my}{"M" if mbut else ""} zoom={zoom} o={ox},{oy} '
                  f'grid={dimx}x{dimy} rest={rx:g},{ry:g} units={units} {"painted" if painted else "skipped"} '
                  f'repaints={repaints} changed={changed} vps={" ".join(vps)}')
        n += 1
    print('frames', n)


if __name__ == '__main__':
    main()
