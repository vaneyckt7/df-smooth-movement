#!/usr/bin/env python3
"""Per-frame digest of a harness trace: digest.py <trace> [<trace>]

With one trace, prints one line per replayed frame: `<frame> <painted> <repaints> <draws>
<digest>`. painted and repaints are what the frame's end line in the trace reports, repaints
counting blank repaints too, draws is how many trace lines the frame wrote (its visible
repaints and SDL draws) and digest is the SHA-256 (first 16 hex digits) of those lines. The
expected output for every recording in recordings/ is kept in expected/<name>.digest; test.sh
compares a replay against it, so what a change alters is visible frame by frame in the diff of
that file rather than in the recording, which stays fixed. With two traces, prints the frames
whose lines differ and exits 1 when any do. Exits 2 on a trace with lines outside a frame.
"""
import hashlib
import sys


def frames(path):
    out = []
    lines = None
    with open(path, "rb") as f:
        for line in f:
            words = line.split()
            if line.startswith(b"# frame replay "):
                lines = []
                out.append([int(words[3]), None, None, lines])
            elif line.startswith(b"# end frame "):
                if lines is None or int(words[3]) != out[-1][0]:
                    sys.exit(f"{path}: end line without its frame: {line.decode().strip()}")
                out[-1][1], out[-1][2] = int(words[5]), int(words[7])
                lines = None
            elif lines is None:
                sys.exit(f"{path}: line outside a frame: {line.decode().strip()}")
            else:
                lines.append(line)
    if lines is not None:
        sys.exit(f"{path}: last frame has no end line")
    return [(n, p, r, len(ls), hashlib.sha256(b"".join(ls)).hexdigest()[:16])
            for n, p, r, ls in out]


def main(argv):
    if len(argv) == 2:
        for row in frames(argv[1]):
            print(*row)
        return 0
    if len(argv) == 3:
        a, b = frames(argv[1]), frames(argv[2])
        differ = [(x, y) for x, y in zip(a, b) if x != y]
        for x, y in differ[:10]:
            print(f"frame {x[0]}: painted {x[1]} repaints {x[2]} draws {x[3]} {x[4]}"
                  f" vs painted {y[1]} repaints {y[2]} draws {y[3]} {y[4]}")
        if len(a) != len(b):
            print(f"frame counts differ: {len(a)} vs {len(b)}")
        print(f"{len(differ)} of {min(len(a), len(b))} frames differ")
        return 1 if differ or len(a) != len(b) else 0
    print(__doc__.strip(), file=sys.stderr)
    return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv))
