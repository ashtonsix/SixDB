import json,sys,glob,statistics as st,collections
d=collections.defaultdict(list); meta={}
for f in sys.argv[1:]:
    for l in open(f):
        r=json.loads(l); k=(r['isa'],r['rows'],r['column'],r['op'],r['arm']); d[k].append(r['ns_per_block'])
arms=['per_block','per_block_prefetch8','per_block_parts','native_for','native_for_parts']
keys=sorted({k[:4] for k in d})
print('isa rows column op | '+' / '.join(arms)+' | ns per block (median of 3 repeats), spread')
for k in keys:
    vals=[d.get(k+(a,),[float('nan')]) for a in arms]
    print(f"{k[0]:6} {k[1]:7} {k[2]:15} {k[3]:8} | "+"  ".join(f"{st.median(v):7.1f}" for v in vals)+"  | max/min "+ " ".join((f"{max(v)/min(v):.2f}" if min(v)>0 else "-") for v in vals))
