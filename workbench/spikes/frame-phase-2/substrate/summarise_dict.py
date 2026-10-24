# Medians of three repeats for frame_substrate_dict output, ns per block.
import json, sys, statistics as st, collections
d = collections.defaultdict(list); meta = {}
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['column'], r['op'], r['arm'])].append(r['ns_per_block'])
        meta[r['column']] = (r['coded_bytes'], r['dict_bytes'], r['integer_bytes'], r['mean_part_distinct'])
print('isa column op | dict / integer ns per block | coded+dict bytes vs integer bytes, mean distinct per part')
for k in sorted({k[:3] for k in d}):
    m = meta[k[1]]
    print(f"{k[0]:6} {k[1]:15} {k[2]:8} | {st.median(d[k + ('dict',)]):8.2f} / {st.median(d[k + ('integer',)]):8.2f} | {m[0] + m[1]:9d} vs {m[2]:9d}, {m[3]:.0f}")
