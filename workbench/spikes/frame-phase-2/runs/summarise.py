# Run boundaries as composed stages: per ISA and column, bits per run-block
# row, ns per point and ns per block of SUM over runs for each form (medians
# over repeats), with ratios to the u8 ends baseline.
import json, sys, statistics as st, collections
d = collections.defaultdict(list); bits = {}
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        k = (r.get('isa', 'local'), r['column'], r['arm'])
        d[k].append((r['ns_per_point'], r['sum_ns_per_block'])); bits[k] = r['bits_per_run_block_row']
base = 'ends_u8_run_end_source'
for isa, col in sorted({k[:2] for k in d}):
    b = (isa, col, base)
    bp, bs = st.median(x[0] for x in d[b]), st.median(x[1] for x in d[b])
    print(f"\n{isa} {col}: form | bits/row (vs ends) | ns/point (vs ends) | SUM ns/block (vs ends)")
    for k in sorted((k for k in d if k[:2] == (isa, col)), key=lambda k: bits[k]):
        p, s = st.median(x[0] for x in d[k]), st.median(x[1] for x in d[k])
        print(f"  {k[2]:34} {bits[k]:.3f} ({bits[k] / bits[b]:.2f}) | {p:6.2f} ({p / bp:5.2f}) | {s:6.2f} ({s / bs:4.2f})")
