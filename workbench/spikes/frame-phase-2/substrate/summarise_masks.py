# Medians of three repeats for frame_substrate_masks output, ns per selected block.
import json, sys, statistics as st, collections
d = collections.defaultdict(list)
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        d[(r['isa'], r['column'], r['pattern'], round(r['density'], 3), r['condition'], r['arm'])].append(r['ns_per_selected_block'])
arms = ['frame_owner', 'frame_bits', 'frame_bits_random', 'frame_bits_random_by_kind', 'frame_bits_random_staged', 'native_spans', 'native_spans_random', 'native_spans_random_staged']
print('isa column pattern density condition | ' + ' / '.join(arms) + ' (ns per selected block) | native / frame_bits')
for k in sorted({k[:5] for k in d}):
    v = [st.median(d[k + (a,)]) for a in arms]
    print(f"{k[0]:6} {k[1]:15} {k[2]:9} {k[3]:<6} {k[4]:7} | " + "  ".join(f"{x:7.1f}" for x in v) + f" | {v[5] / v[1]:5.2f}")
