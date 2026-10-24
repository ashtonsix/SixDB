import json,sys
rows=[json.loads(l) for l in open(sys.argv[1])]
cols=[]; [cols.append(r["column"]) for r in rows if r["column"] not in cols]
meths=[]; [meths.append(r["method"]) for r in rows if r["method"] not in meths]
print(f"{'regret % of exact bytes':24}"+"".join(f"{m[:14]:>15}" for m in meths[1:]))
for c in cols:
  d={r["method"]:r for r in rows if r["column"]==c}
  e=d["exact"]
  print(f"{c:16}{(e['blocks']-e['constant']):6} {e['exact_bytes']*8/((e['blocks']-e['constant'])*256):5.2f}b"+"".join(f"{d[m]['regret_pct']:15.2f}" for m in meths[1:]))
