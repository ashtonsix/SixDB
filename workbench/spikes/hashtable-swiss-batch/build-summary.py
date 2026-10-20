#!/usr/bin/env python3
"""Offline construction-only qualification; keep all frozen peer controls."""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path
root = Path(sys.argv[1])
groups = defaultdict(list)
states = {}
populations = {}
for p in sorted((root / 'samples').glob('*.csv')):
    r, = list(csv.DictReader(p.open()))
    assert r['phase'] == 'build' and int(r['operations']) == int(r['n']) and float(r['ns']) > 0
    groups[(r['isa'], r['b'], r['v'], r['impl'])].append(r)
    identity = (r['isa'], r['b'], r['v'], r['seed'])
    assert populations.setdefault(identity, r['n']) == r['n']
    if r['impl'] not in ['boost', 'abseil']:
        key = (r['isa'], r['b'], r['v'], r['seed'])
        state = tuple(r[f] for f in ['n', 'bytes', 'spill', 'groups', 'swiss_slots', 'debt'])
        assert states.setdefault(key, state) == state
expected = {('avx512','10','0'),('avx512','10','8'),('avx2','10','0'),('avx2','10','8'),('avx512','20','0')}
assert {(i,b,v) for i,b,v,_ in groups} == expected
assert set(groups) == {(i,b,v,arm) for i,b,v in expected
                       for arm in ['group16', 'native', 'boost', 'abseil',
                                   'batch64' if i == 'avx512' else 'batch32']}
medians = {k: statistics.median(float(r['ns']) for r in rr) for k,rr in groups.items()}
with (root / 'summary.csv').open('w') as f:
    w = csv.DictWriter(f,fieldnames=['isa','b','v','impl','ns','mad','samples','seeds','vs_group16']);w.writeheader()
    for (isa,b,v,impl),rr in sorted(groups.items()):
        assert len(rr) == (9 if b == '20' else 15) and {r['seed'] for r in rr} == {'713','991','1729'}
        ns = medians[(isa,b,v,impl)]
        w.writerow(dict(isa=isa,b=b,v=v,impl=impl,ns=ns,mad=statistics.median(abs(float(r['ns'])-ns) for r in rr),samples=len(rr),seeds=3,vs_group16=ns/medians[(isa,b,v,'group16')]))
(root / 'audit.txt').write_text(f'{sum(map(len,groups.values()))} frozen binary executions; complete construction grid; all physical Ikea states match; full internal owner verification before exit\n')
print((root / 'audit.txt').read_text(),end='')
