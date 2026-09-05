#!/usr/bin/env python3
"""Checks the documented paint difference of dropping the previous-coverage blank-out.
Usage: oracle6.py <with.trace> <without.trace>
Per frame and per cell of the origin-aligned tile grid: a cell the "without" trace paints
must get exactly the ops the "with" trace gives it; a cell only the "with" trace paints
may carry only a blank-out fill and engine repaints (no sprite copies), which is the
re-blank of a tile a sprite covered the frame before."""
import sys
from collections import defaultdict
TS=32;OX,OY=5,7
ZERO='81d23fd7003c2305'
def cells_px(x,y,w,h):
    x0=int((x-OX)//TS);y0=int((y-OY)//TS);x1=int((x+w-1e-6-OX)//TS);y1=int((y+h-1e-6-OY)//TS)
    return [(cx,cy) for cx in range(x0,x1+1) for cy in range(y0,y1+1)]
def load(path):
    frames=[];per=None
    for line in open(path):
        t=line.split()
        if not t:continue
        if t[0]=='#':per=defaultdict(list);frames.append((line.strip(),per));continue
        if t[0]=='R':
            vp,x,y,h=t[1],int(t[2]),int(t[3]),t[4];iface=int(t[5][1:])
            px,py=TS*x+OX,TS*y+OY
            for c in cells_px(px,py,TS,TS):
                if h!=ZERO:per[c].append(('R',vp,px,py,h))
                if iface:per[c].append(('I',vp,px,py,iface))
        elif t[0] in('C','X'):
            x,y,w,h=map(float,t[2:6])
            for c in cells_px(x,y,w,h):per[c].append(tuple(t))
        elif t[0]=='F':
            x,y,w,h=map(int,t[1:5])
            for c in cells_px(x,y,w,h):per[c].append(tuple(t))
    return frames
a=load(sys.argv[1]);b=load(sys.argv[2])
assert len(a)==len(b),(len(a),len(b))
bad=0;dropped=0;same=0
for (fa,pa),(fb,pb) in zip(a,b):
    assert fa==fb
    for c in set(pa)|set(pb):
        if c in pb:
            if pa.get(c)!=pb[c]:
                bad+=1
                if bad<=3:print(fa,'cell',c,'\n  with   ',pa.get(c),'\n  without',pb[c])
            else:same+=1
        else:
            if any(op[0] in('C','X') for op in pa[c]):
                bad+=1
                if bad<=3:print(fa,'cell',c,'dropped ops include a sprite:',pa[c])
            else:dropped+=1
print(f'frames {len(a)}, cells painted alike {same}, re-blank-only cells dropped {dropped}, violations {bad}')
sys.exit(1 if bad else 0)
