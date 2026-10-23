# Medians of three repeats for frame_substrate_select output, in microseconds.
import json, sys, statistics as st, collections
d = collections.defaultdict(list); meta = {}
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        k = (r['isa'], r['pair'], r['layout'], r.get('condition', 'warm'), r['q'])
        d[k + (r['arm'],)].append(r['ns'] / 1e3)
        meta[k] = (r['density'], r['blocks_touched'])
arms = ['frame_staged', 'frame_fused', 'native_staged', 'native_fused']
print('isa pair layout condition q density blocks | ' + ' / '.join(arms) + ' (us) | best native / frame_fused')
for k in sorted(meta):
    v = [st.median(d[k + (a,)]) for a in arms]
    print(f"{k[0]:6} {k[1]:17} {k[2]:9} {k[3]:7} {k[4]:<6} {meta[k][0]:.4f} {meta[k][1]:.3f} | " + "  ".join(f"{x:8.1f}" for x in v) + f" | {min(v[2], v[3]) / v[1]:5.2f}")
