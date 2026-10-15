#!/usr/bin/env python3
"""Check matched stored formats, hint accounting and extra value-stage work."""
import argparse,csv,json,hashlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);a=p.parse_args()
rows=list(csv.DictReader((a.root/'summary.csv').open()))
groups={}
fields=['K','W','kind','A','B','unit','secondary_unit','keys','capacity_family','mixed','concurrency','owner','method']
for r in rows:
    assert r['word_control']=='0' and r['graph_hints']=='1'
    key=tuple(r[f] for f in fields);mode=int(r['read_windows'])
    assert mode in [0,1,2] and mode not in groups.setdefault(key,{})
    groups[key][mode]=r
for key,modes in groups.items():
    assert set(modes)=={0,1,2},key
    whole,prefix,split=(modes[m] for m in range(3))
    for field in ['requested','backing','metadata','graph_bytes','jobs_bytes','ring_bytes','requests','samples','inline_bytes','primary_slots','secondary_slots']:
        assert len({r[field] for r in modes.values()})==1,(key,field)
    b=lambda r:int(r['hint_bytes_per_graph'])
    w=lambda r:int(r['hint_windows_per_graph'])
    v=lambda r:int(r['ring_visits'])
    assert v(whole)==v(prefix) and w(whole)==w(prefix) and b(prefix)<=b(whole)
    if whole['owner']=='abseil':
        assert all(not b(r) and not w(r) and not v(r) for r in modes.values())
    elif int(whole['kind'])==0:
        assert v(split)==v(prefix) and w(split)==w(prefix) and b(split)==b(prefix)
    else:
        assert b(split)>=b(prefix) and w(split)>=w(prefix) and v(split)>=v(prefix)
        if whole['method'] in ['ring','ring_hints']:
            requests=int(whole['requests']);n=int(whole['keys']);graphs=requests//(n*(1+int(whole['mixed'])))
            assert (v(split)-v(prefix))%graphs==0
            located=(v(split)-v(prefix))//graphs
            assert 0<located<=n
            # These exact-plane candidates place one row-byte tag adjacent to
            # the stored descriptor/inline bytes. Overflow rows are unchanged.
            width=int(whole['W']) if int(whole['kind'])==1 else int(whole['inline_bytes'])+1
            assert b(split)-b(prefix)==located*width,(key,'located byte hints')
            assert w(split)-w(prefix)==located,(key,'one coalesced value/tag hint')
print(f'{len(groups)} matched operation groups: same stored bytes, same prefix visits, exact additional value-stage/hint work; all sets skip the value stage.')
(a.root/'window-inputs.json').write_text(json.dumps({'summary.csv':hashlib.sha256((a.root/'summary.csv').read_bytes()).hexdigest(),'groups':len(groups)},indent=2)+'\n')
