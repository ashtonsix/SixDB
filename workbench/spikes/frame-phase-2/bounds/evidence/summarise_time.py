import json,sys,collections,statistics
rows=[json.loads(l) for f in sys.argv[1:] for l in open(f)]
d=collections.defaultdict(list); meta={}
for r in rows:
    d[(r['isa'],r['column'],r['method'])].append(r['ns_per_block']); meta[(r['isa'],r['column'],r['method'])]=r
isas=sorted(set(k[0] for k in d)); cols=[]; [cols.append(k[1]) for k in d if k[1] not in cols]
meths=[]; [meths.append(k[2]) for k in d if k[2] not in meths]
for isa in isas:
    print(f"== {isa}: ns per block (median of 3 repeats)")
    print(f"{'':16}"+"".join(f"{m[:13]:>14}" for m in meths))
    for c in cols:
        print(f"{c:16}"+"".join(f"{statistics.median(d[(isa,c,m)]):14.1f}" if (isa,c,m) in d else f"{'':14}" for m in meths))
