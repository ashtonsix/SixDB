# The bounds stage in the no-RLE pipeline (pipelines.cpp): per ISA and
# column, the inline control (lo = minimum, every width) against the stage and
# its exception-penalty variants, compact preset; bytes, construction and
# bounds-only time per block, and scan and point cost (medians over repeats).
import json, sys, statistics as st, collections
d = collections.defaultdict(list); b = collections.defaultdict(list)
for f in sys.argv[1:]:
    isa = 'avx512' if 'avx512' in f else 'native'
    for l in open(f):
        r = json.loads(l)
        if 'bounds' in r:
            b[(r.get('isa', isa), r['column'], r['bounds'])].append(r['ns_per_block'])
        elif r['arm'].startswith('no_rle_compact'):
            d[(r.get('isa', isa), r['column'], r['arm'])].append(r)
arms = ['no_rle_compact', 'no_rle_compact_stage', 'no_rle_compact_stage_penalty4', 'no_rle_compact_stage_penalty16']
for isa, col in sorted({k[:2] for k in d}):
    bi, bs = (st.median(b[(isa, col, x)]) if b.get((isa, col, x)) else float('nan') for x in ('inline_min_exhaustive', 'stage'))
    print(f"\n{isa} {col}: bounds alone ns/block inline {bi:.0f} / stage {bs:.0f}")
    print("  arm | bits/value | build ns/block | SUM ns/block | COUNT ns/block | ns/point")
    for a in arms:
        rs = d.get((isa, col, a))
        if not rs:
            continue
        m = lambda f: st.median(r[f] for r in rs)
        print(f"  {a:32} {rs[0]['bits_per_value']:7.3f} | {m('build_ns_per_block'):7.0f} | {m('sum_ns_per_block'):7.2f} | {m('count_ns_per_block'):7.2f} | {m('ns_per_point'):6.2f}")
