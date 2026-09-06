#!/usr/bin/env python3
"""Paint oracle for two harness traces that are allowed to differ.

    oracle.py fold   <a.trace> <b.trace>
    oracle.py reblank <with.trace> <without.trace>

Both checks key every draw operation to the 32-pixel screen cells it touches, on the tile
grid at the harness's fixed draw origin, so a sprite at a fractional position lands in the
same cells in both traces. A tile repaint carrying a non-zero interface value counts as the
body repaint followed by an interface-only repaint, since the engine paints that layer last.
A repaint of a tile whose buffers are all zero paints nothing and is dropped.

`fold` says two traces paint the same pixels: for every cell, the sequence of operations
over the whole run must be identical. Use it for a change that reorders or merges draws.

`reblank` checks the one documented difference of dropping the previous-coverage blank-out:
per frame, a cell the "without" trace paints must get exactly the operations the "with"
trace gives it, and a cell only the "with" trace paints may carry nothing but a blank-out
fill and engine repaints, never a sprite copy."""
import itertools,math,sys
from collections import defaultdict

TS=32
# The harness draws at a fixed origin; the cell grid is anchored there.
OX,OY=5,7
# Hash of a tile whose buffers are all zero.
ZERO='81d23fd7003c2305'

def cells_px(x,y,w,h):
    x-=OX;y-=OY
    x0=math.floor(x/TS);x1=math.floor((x+w-1e-6)/TS);y0=math.floor(y/TS);y1=math.floor((y+h-1e-6)/TS)
    return [(cx,cy) for cx in range(x0,x1+1) for cy in range(y0,y1+1)]

def load(path):
    """Returns [(frame header, {cell: [op,...]})] in trace order, plus the op count."""
    frames=[];per=None;n=0
    for line in open(path):
        t=line.split()
        if not t:continue
        if t[0]=='#':
            per=defaultdict(list);frames.append((line.strip(),per));continue
        n+=1
        if t[0]=='R':
            vp,x,y,h=t[1],int(t[2]),int(t[3]),t[4];iface=int(t[5][1:])
            ox,oy=map(int,t[6][1:].split(','));assert (ox,oy)==(OX,OY),line
            px,py=TS*x+ox,TS*y+oy
            for c in cells_px(px,py,TS,TS):
                if h!=ZERO:per[c].append(('R',vp,px,py,h))
                if iface:per[c].append(('I',vp,px,py,iface))
        elif t[0] in('C','X'):
            x,y,w,h=map(float,t[2:6])
            for c in cells_px(x,y,w,h):per[c].append(tuple(t))
        elif t[0]=='F':
            x,y,w,h=map(int,t[1:5])
            for c in cells_px(x,y,w,h):per[c].append(tuple(t))
        # K (clip) and D (draw colour) lines carry no paint of their own
    return frames,n

def flatten(frames):
    per=defaultdict(list)
    for _,p in frames:
        for c,ops in p.items():per[c].extend(ops)
    return per

def fold(a_path,b_path):
    fa,na=load(a_path);fb,nb=load(b_path)
    a=flatten(fa);b=flatten(fb);bad=0
    for c in sorted(set(a)|set(b)):
        if a.get(c,[])!=b.get(c,[]):
            bad+=1
            if bad<=3:
                print('cell',c)
                for i,(p,q) in enumerate(itertools.zip_longest(a.get(c,[]),b.get(c,[]))):
                    if p!=q:print('  first difference at op',i,'\n   ',p,'\n   ',q);break
    print(f'{a_path}: {na} ops, {b_path}: {nb} ops, cells compared {len(set(a)|set(b))}, differing {bad}')
    return bad

def reblank(with_path,without_path):
    a,_=load(with_path);b,_=load(without_path)
    assert len(a)==len(b),(len(a),len(b))
    bad=dropped=same=0
    for (fa,pa),(fb,pb) in zip(a,b):
        assert fa==fb,(fa,fb)
        for c in set(pa)|set(pb):
            if c in pb:
                if pa.get(c)!=pb[c]:
                    bad+=1
                    if bad<=3:print(fa,'cell',c,'\n  with   ',pa.get(c),'\n  without',pb[c])
                else:same+=1
            elif any(op[0] in('C','X') for op in pa[c]):
                bad+=1
                if bad<=3:print(fa,'cell',c,'dropped ops include a sprite:',pa[c])
            else:dropped+=1
    print(f'frames {len(a)}, cells painted alike {same}, re-blank-only cells dropped {dropped}, violations {bad}')
    return bad

if len(sys.argv)!=4 or sys.argv[1] not in('fold','reblank'):
    sys.exit(__doc__)
sys.exit(1 if {'fold':fold,'reblank':reblank}[sys.argv[1]](sys.argv[2],sys.argv[3]) else 0)
