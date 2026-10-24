# SeriesPack remeasure (measure.cpp): per ISA, condition and operation, the
# median ns per 256 rows (points: ns per point) for each arm and width, and
# each SeriesPack preset's ratio to the native array. The native masked-sum
# control does not vectorise (23-30 ns per 256 rows on NEON for every lane),
# so masked sums are shown as a ratio to the same arm's unmasked sum instead.
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        v = r['ns_per_unit'] if r['op'] in ('points', 'short_reads') else r['ns_per_256_rows']
        d[(r.get('isa', 'local'), r['condition'], r['op'], r['k'], r['arm'])].append(v)
arms = ['native', 'seriespack_compact', 'seriespack_bulk_x86', 'seriespack_bulk_arm', 'plane256_reference']
short = {'native': 'native', 'seriespack_compact': 'compact', 'seriespack_bulk_x86': 'bulk_x86', 'seriespack_bulk_arm': 'bulk_arm', 'plane256_reference': 'plane256'}
for group in sorted({k[:3] for k in d}):
    isa, cond, op = group
    basis = "ratio to the arm's own unmasked sum" if op.startswith('masked') else 'ratio to native'
    print(f"\n{isa} {cond} {op} ({'ns per call' if op in ('points', 'short_reads') else 'ns per 256 rows'}; {basis} in brackets)")
    print('  K | ' + ' | '.join(f"{short[a]:>15}" for a in arms))
    for k in sorted({kk[3] for kk in d if kk[:3] == group}):
        nat = st.median(d[group + (k, 'native')]) if d.get(group + (k, 'native')) else None
        cells = []
        for a in arms:
            x = d.get(group + (k, a))
            if not x:
                cells.append(f"{'-':>15}")
                continue
            m = st.median(x)
            if op.startswith('masked'):
                own = d.get((isa, cond, 'sum', k, a))
                cells.append(f"{m:7.2f} ({m / st.median(own):4.2f})" if own else f"{m:15.2f}")
            else:
                cells.append(f"{m:7.2f} ({m / nat:4.2f})" if nat and a != 'native' else f"{m:15.2f}")
        print(f"{k:3} | " + ' | '.join(cells))
