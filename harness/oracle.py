#!/usr/bin/env python3
"""Paint-operation oracle: two traces paint the same pixels when, for every screen cell,
the sequence of operations touching it is the same. A repaint whose interface value is
non-zero counts as the body repaint followed by an interface-only repaint, since the
engine paints that layer last. Cells are the 32-pixel screen grid and the grid shifted by
the frame's draw origin, so sprites at fractional positions are keyed the same way."""
import sys,math
from collections import defaultdict
TS=32
# Cells are the tile grid at the frame's draw origin; the harness draws at a fixed origin.
OX,OY=5,7
def cells_px(x,y,w,h):
    x-=OX;y-=OY
    x0=math.floor(x/TS);x1=math.floor((x+w-1e-6)/TS);y0=math.floor(y/TS);y1=math.floor((y+h-1e-6)/TS)
    return [(cx,cy) for cx in range(x0,x1+1) for cy in range(y0,y1+1)]
def load(path):
    per=defaultdict(list);frame=None;n=0
    for line in open(path):
        t=line.split()
        if not t:continue
        if t[0]=='#':frame=line.strip();continue
        n+=1
        if t[0]=='R':
            vp,x,y,h=t[1],int(t[2]),int(t[3]),t[4];iface=int(t[5][1:]);ox,oy=map(int,t[6][1:].split(','));assert (ox,oy)==(OX,OY),line
            px,py=TS*x+ox,TS*y+oy
            cells=cells_px(px,py,TS,TS)
            body=('R',vp,px,py,h)
            for c in cells:
                if h!='81d23fd7003c2305':per[c].append(body)   # the all-zero body paints nothing
                if iface:per[c].append(('I',vp,px,py,iface))
        elif t[0] in('C','X'):
            x,y,w,h=map(float,t[2:6]);op=tuple(t)
            for c in cells_px(x,y,w,h):per[c].append(op)
        elif t[0]=='F':
            x,y,w,h=map(int,t[1:5]);op=tuple(t)
            for c in cells_px(x,y,w,h):per[c].append(op)
        # K (clip) and D (draw colour) lines carry no paint of their own
    return per,n
a,na=load(sys.argv[1]);b,nb=load(sys.argv[2])
bad=0
for c in sorted(set(a)|set(b)):
    if a.get(c,[])!=b.get(c,[]):
        bad+=1
        if bad<=3:
            print('cell',c);import itertools
            for i,(p,q) in enumerate(itertools.zip_longest(a.get(c,[]),b.get(c,[]))):
                if p!=q:print('  first difference at op',i,'\n   ',p,'\n   ',q);break
print(f'{sys.argv[1]}: {na} ops, {sys.argv[2]}: {nb} ops, cells compared {len(set(a)|set(b))}, differing {bad}')
sys.exit(1 if bad else 0)
