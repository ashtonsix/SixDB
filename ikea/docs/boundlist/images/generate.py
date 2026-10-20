#!/usr/bin/env python3
"""Render the Boundlist teaching figures after imagegen composition studies.

The raster concepts explored ordered ranges, staged dependencies and three
encodings; the final mathematical labels are checked here, independently of
raster text. Shared Figure keeps the Ikea palette and accessible SVG descriptions.
Run: orb -m ubuntu python3 ikea/docs/boundlist/images/generate.py
Previews belong in ignored build/diagrams/boundlist/.
"""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).parent
STYLE = runpy.run_path(str(ROOT / 'ikea/docs/images/generate.py'))
Figure = STYLE['Figure']
BLUE, GREEN, AMBER, MUTED, RULE = (STYLE[k] for k in ('BLUE','GREEN','AMBER','MUTED','RULE'))


def encodings():
    sizes = [2]*15 + [4,3,0,5]
    ends=[]
    for size in sizes: ends.append((ends[-1] if ends else 0)+size)
    assert ends[13:]==[28,30,34,37,37,42]
    relative=[end-(ends[r//16*16-1] if r>=16 else 0) for r,end in enumerate(ends)]
    assert relative[14:]==[30,34,3,3,8]
    f=Figure(590,'Three ways to store the same boundaries',
        'Excerpt at ranks14 through18. The G16 boundary lies between15 and16. '
        'Absolute ends30,34,37,37,42; sizes2,4,3,0,5; relative ends30,34,3,3,8. '
        'The second group origin is34. All three locate rank18 at37 with size5.')
    f.heading(32,42,'Three ways to store the same boundaries')
    f.text(32,77,'An excerpt across a 16-rank group boundary. All numbers are decimal.',20,color=MUTED)
    for i,r in enumerate(range(14,19)):
        x=350+i*88
        f.text(x,125,f'rank {r}',18,anchor='middle')
        f.text(x,151,f'size {sizes[r]}',17,color=MUTED,anchor='middle')
    for y,title,sub,values in (
        (175,'Absolute Ends','Global end per rank',ends[14:]),
        (290,'Sizes','Size per rank + group origin',sizes[14:]),
        (405,'Relative Ends','Local end + group origin',relative[14:])):
        f.rect(32,y,730,92,'#f7f9fc',radius=6)
        f.text(50,y+34,title,23,'bold');f.text(50,y+65,sub,17,color=MUTED)
        for i,value in enumerate(values):
            f.rect(312+i*88,y+22,76,48,GREEN,radius=3)
            f.text(350+i*88,y+54,value,24,anchor='middle',mono=True)
        if y!=175:
            f.rect(784,y+22,70,48,BLUE,radius=3)
            f.text(819,y+54,34,24,anchor='middle',mono=True)
            f.text(819,y+89,'origin',16,color=MUTED,anchor='middle')
    f.path('M482,105 V506',dash=True)
    f.rect(883,175,365,322,'#fff8e6',radius=8)
    f.heading(905,214,'Locate rank 18')
    for y,title,lines in (
        (250,'Absolute Ends',('start = previous end = 37','end = current end = 42')),
        (335,'Sizes',('start = 34 + 3 + 0 = 37','end = 37 + 5 = 42')),
        (420,'Relative Ends',('start = 34 + 3 = 37','end = 34 + 8 = 42'))):
        f.text(905,y,title,21,'bold');f.lines(905,y+27,lines,18,25)
    f.path('M32,530 H1248',color=RULE)
    f.text(32,565,'Stored widths follow schema bounds. Zero-size ranks remain assigned.',21,color=MUTED)
    f.save(OUT/'encodings.svg')


def ranges():
    f=Figure(650,'A range query bounds the children worth visiting',
        'Closed occupied ranges at ranks0..5 are3..7,10..10,18..24,26..31,35..40,46..52. '
        'Rank2 is ineligible. Query9 inclusive to36 exclusive returns candidate ranks1..5 exclusive. '
        'The singleton at rank1 qualifies; the internal ineligible rank is skipped by the consumer.')
    f.heading(32,42,'A range query bounds the children worth visiting')
    x=lambda k:170+k*17
    f.rect(x(9),110,x(36)-x(9),440,'#edf8ef',stroke='none')
    f.path(f'M{x(9)},110 V550',dash=True,color='#15834b')
    f.path(f'M{x(36)},110 V550',dash=True,color='#15834b')
    f.text(x(22),95,'query [9, 36)',22,'bold',color='#15834b',anchor='middle')
    bounds=[(3,7),(10,10),(18,24),(26,31),(35,40),(46,52)]
    for r,(lo,hi) in enumerate(bounds):
        y=160+r*68
        if 1<=r<5:f.rect(32,y-28,1184,54,'#fff4d9',stroke='none',radius=4)
        f.text(50,y+5,f'rank {r}',21,'bold')
        f.path(f'M170,{y} H1150',color=RULE)
        color='#8d99a8' if r==2 else '#175ca7'
        f.path(f'M{x(lo)},{y} H{x(hi)}',dash=r==2,color=color)
        for point in set((lo,hi)):
            f.items.append(f'<circle cx="{x(point)}" cy="{y}" r="6" fill="{color}"/>')
        f.text(x(lo),y-12,f'[{lo}, {hi}]',18,'bold',color=color)
        if r==1:f.text(x(lo)+28,y+21,'populated singleton',17,color=MUTED)
        if r==2:f.text(x(lo),y+24,'ineligible',17,color=MUTED)
    f.text(32,590,'Eligibility',20,'bold')
    for r,bit in enumerate([1,1,0,1,1,1]):
        f.rect(180+r*85,564,78,42,'#f0f3f7');f.text(219+r*85,593,bit,21,anchor='middle',mono=True)
    f.text(735,588,'Candidate ranks [1, 5)',23,'bold')
    f.text(735,618,'Test eligibility inside the window.',19,color=MUTED)
    f.save(OUT/'ranges.svg')


def phases():
    f=Figure(510,'Expose a dependency while retaining one version',
        'Checkpoint search identifies a sixteen-field local group, which the owner may prefetch '
        'before finishing. Numeric binding proposes an endpoint bracket and verifies or repairs it. '
        'Metadata, eligibility and backing are one retained version; a hint is not a read grant.')
    f.heading(32,42,'Expose the next dependency; retain one data version')
    f.text(32,82,'Ready arithmetic stays inline. The caller decides whether an unresolved read warrants a Loom wait.',20,color=MUTED)
    rows=[(126,'Checkpointed source','search checkpoints','16 local fields','compare / prefix + select'),
          (262,'Bound numeric source','propose bracket','endpoint bracket','verify; correct if needed')]
    for y,label,prepare,window,finish in rows:
        f.text(32,y+38,label,20,'bold')
        for x,w,title,body,fill in [(275,230,'prepare',prepare,BLUE),(540,190,'local_window',window,'#f0f3f7'),
                                   (765,190,'caller policy','prefetch / optional wait',AMBER),
                                   (990,258,'finish',finish,GREEN)]:
            f.rect(x,y,w,88,fill,radius=6)
            f.text(x+w/2,y+32,title,20,'bold',anchor='middle')
            f.text(x+w/2,y+61,body,16,anchor='middle')
        for a,b in [(505,540),(730,765),(955,990)]:f.path(f'M{a+3},{y+44} H{b-5}',arrow=True)
    f.rect(32,385,1216,58,'#f0f3f7',radius=6)
    f.text(640,421,'One retained version: boundaries + eligibility + backing + derived search state',21,'bold',anchor='middle')
    f.text(32,469,'The hint window is not the complete protection footprint. Finishing may need selection or bracket repair.',18,color=MUTED)
    f.save(OUT/'phases.svg')


if __name__=='__main__':
    encodings();ranges();phases()
