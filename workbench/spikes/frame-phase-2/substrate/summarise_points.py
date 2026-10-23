# Medians of three repeats for frame_substrate_points output, ns per point.
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['rows'], r['column'], r['distance'], r['arm'])].append(r['ns_per_point'])
arms = ['native', 'native_prefetch', 'frame_direct', 'frame_staged']
print('isa rows column distance | ' + ' / '.join(arms) + ' (ns per point)')
for k in sorted({k[:4] for k in d}):
    print(f"{k[0]:6} {k[1]:7} {k[2]:10} {k[3]:3} | " + "  ".join(f"{st.median(d[k + (a,)]):7.2f}" for a in arms))
