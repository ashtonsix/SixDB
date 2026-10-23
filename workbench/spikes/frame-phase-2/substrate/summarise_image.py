# Medians of three repeats for frame_substrate_image output (ns per block or point).
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['column'], r['cell'], r['condition'], r['arm'])].append(r['ns_per_unit'])
print('isa column cell condition | arm=median ...')
for k in sorted({k[:4] for k in d}):
    arms = sorted(a for (*kk, a) in d if tuple(kk) == k)
    print(f"{k[0]:6} {k[1]:17} {k[2]:10} {k[3]:7} | " + "  ".join(f"{a}={st.median(d[k + (a,)]):.2f}" for a in arms))
