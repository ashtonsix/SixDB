# Per-stream medians by stream count for concurrent full-slice scans (ns per block).
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['column'], r['op'], r['arm'], r['streams'])].append(r['ns_per_block'])
arms = ['phase1_owner', 'batched', 'native_for']
counts = sorted({k[4] for k in d})
print('isa column op arm | ns per block per stream at ' + ' / '.join(f'{c} streams' for c in counts))
for k in sorted({k[:4] for k in d}):
    if k[3] not in arms:
        continue
    print(f"{k[0]:6} {k[1]:10} {k[2]:8} {k[3]:13} | " + "  ".join(f"{st.median(d[k + (c,)]):7.1f}" for c in counts if k + (c,) in d))
