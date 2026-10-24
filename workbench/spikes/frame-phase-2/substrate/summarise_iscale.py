# Per-stream medians by stream count for concurrent shared-image SUM (ns per block).
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        if r['condition'] != 'warm':
            continue
        d[(r['isa'], r['column'], r['arm'], r['streams'])].append(r['ns_per_unit'])
counts = sorted({k[3] for k in d})
print('isa column arm | warm ns per block per stream at ' + ' / '.join(f'{c} streams' for c in counts))
for k in sorted({k[:3] for k in d}):
    print(f"{k[0]:6} {k[1]:10} {k[2]:26} | " + "  ".join(f"{st.median(d[k + (c,)]):7.1f}" for c in counts if k + (c,) in d))
