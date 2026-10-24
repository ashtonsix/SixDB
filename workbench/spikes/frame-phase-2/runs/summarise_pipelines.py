# Pipelines with and without RLE (pipelines.cpp): per ISA and column, block
# mix, bits per value, and median ns per block (SUM, COUNT) and per point.
import json, sys, statistics as st, collections
d = collections.defaultdict(list); meta = {}
for f in sys.argv[1:]:
    for l in open(f):
        r = json.loads(l)
        k = (r.get('isa', 'local'), r['column'], r['arm'])
        d[k].append(r); meta[k] = r
order = ['no_rle_compact', 'no_rle_bulk_x86', 'no_rle_bulk_arm', 'rle_then_compact', 'rle_then_bulk_x86', 'rle_then_bulk_arm']
for isa, col in sorted({k[:2] for k in d}):
    print(f"\n{isa} {col}: arm | constant/runs/packed blocks | bits per value | SUM ns/block | COUNT ns/block | ns/point")
    for a in order:
        k = (isa, col, a)
        if k not in d:
            continue
        m = meta[k]; med = lambda f: st.median(x[f] for x in d[k])
        print(f"  {a:18} {m['constant_blocks']:5}/{m['run_blocks']:5}/{m['packed_blocks']:5} | {m['bits_per_value']:7.3f} | "
              f"{med('sum_ns_per_block'):7.2f} | {med('count_ns_per_block'):7.2f} | {med('ns_per_point'):6.2f}")
