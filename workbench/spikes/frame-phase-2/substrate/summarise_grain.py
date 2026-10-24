# Fold and reclaim grain (grain.cpp): per column, ISA, condition and op, the
# median ns per block (SUM, COUNT), per point, or per fold of one group
# (fold_one_group, global = whole-image rebuild) for each grain; then the
# semantic witness rows once per column.
import json, sys, statistics as st, collections
d = collections.defaultdict(list); witness = {}
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        if 'witness' in r:
            witness[(r['isa'], r['column'], r['witness'])] = r
        elif 'grain' in r:
            d[(r['isa'], r['column'], r['condition'], r['op'], r['grain'])].append(r['ns_per_unit'])
grains = sorted({k[4] for k in d}, key=lambda g: (g != 'global', len(g), g))
print('isa column condition op |', ' / '.join(grains))
for k in sorted({k[:4] for k in d}):
    print(*k, '|', ' / '.join(f"{st.median(d[k + (g,)]):.2f}" if d.get(k + (g,)) else '-' for g in grains))
for k, r in sorted(witness.items()):
    print(*k, {x: y for x, y in r.items() if x not in ('repeat', 'isa', 'column', 'witness')})
