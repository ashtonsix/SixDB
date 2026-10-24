# Medians of three repeats for frame_substrate_geometry, ns per selected row.
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['column'], r['condition'], r['layout'], r['rows_in_block'], r['spread'], r['arm'])].append(r['ns_per_selected_row'])
arms = ['frame_routed', 'frame_routed_sparse16', 'frame_routed_sparse64', 'native_gather', 'native_full_pass']
print('isa column condition layout rows spread | ' + ' / '.join(arms) + ' (ns per selected row)')
for k in sorted({k[:6] for k in d}):
    print(f"{k[0]:6} {k[1]:8} {k[2]:7} {k[3]:9} {k[4]:10} {k[5]:3} | " + "  ".join(f"{st.median(d[k + (a,)]):6.2f}" for a in arms))
