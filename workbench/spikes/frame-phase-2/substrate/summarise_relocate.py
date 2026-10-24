# Medians of three repeats for frame_substrate_relocate output, by relocated share.
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['column'], r['placement'], r['condition'], r['arm'], r['relocated'])].append(r['ns_per_unit'])
shares = sorted({k[5] for k in d})
print('isa column placement condition arm | ns per unit at relocated ' + ' / '.join(str(s) for s in shares))
for k in sorted({k[:5] for k in d}):
    print(f"{k[0]:6} {k[1]:10} {k[2]:8} {k[3]:7} {k[4]:15} | " + "  ".join(f"{st.median(d[k + (s,)]):7.2f}" for s in shares))
