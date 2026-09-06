#!/usr/bin/env python3
"""Prints one line per frame of a `smooth-movement stats record` file: the settings, window,
viewports and unit count the render hook saw, and the repaints it made. Buffer contents are
skipped. Usage: recinfo.py <recording> [first frame] [last frame]"""
import struct, sys

BUFFERS = 50
WORD = [4, 8, 4, 4, 4, 4, 8, 4] + [4] * 17  # element size per current buffer, repeated for old
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
    print('version', r.u32())
    n = 0
    while r.p < len(data):
        assert r.u8() == ord('F')
        flags = [r.u8() for _ in range(4)]
        tick = r.u32()
        wx, wy, wz = r.i32(), r.i32(), r.i32()
        paused = r.u8()
        follow = r.i32()
        mx, my = r.i32(), r.i32()
        mbut = r.u8()
        zoom, ox, oy, dimx, dimy = (r.i32() for _ in range(5))
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
            print(f'{n:5d} t={tick} f{"".join(map(str, flags))} w={wx},{wy},{wz} {"P" if paused else "-"} '
                  f'follow={follow} mouse={mx},{my}{"M" if mbut else ""} zoom={zoom} o={ox},{oy} '
                  f'grid={dimx}x{dimy} units={units} {"painted" if painted else "skipped"} '
                  f'repaints={repaints} changed={changed} vps={" ".join(vps)}')
        n += 1
    print('frames', n)


if __name__ == '__main__':
    main()
