from pathlib import Path
import csv,json,statistics,sys
src=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader((src/'samples.csv').open()));groups={}
for r in rows:
 key=(int(r['K']),int(r['kind']),int(r['arm']),int(r['A']),r['phase'])
 g=groups.setdefault(key,{})
 pair=(int(r['rep']),r['mode']);assert pair not in g
 g[pair]=r
summary=[]
for key,g in sorted(groups.items()):
 reps=sorted({p[0] for p in g});assert all((rep,mode) in g for rep in reps for mode in ('original','padded','full'))
 invariants=['operations','requested','owned_capacity','metadata','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']
 for rep in reps:
  for field in invariants:assert len({g[(rep,m)][field] for m in ('original','padded','full')})==1,(key,rep,field)
 values={m:[float(g[(r,m)]['ns'])/int(g[(r,m)]['operations']) for r in reps] for m in ('original','padded','full')}
 row=dict(zip(['K','kind','arm','A','phase'],key));row['reps']=len(reps)
 for m,v in values.items():
  row[m+'_median_ns']=statistics.median(v);row[m+'_min_ns']=min(v);row[m+'_max_ns']=max(v)
 for a,b in [('original','padded'),('padded','full'),('original','full')]:
  ratios=[x/y for x,y in zip(values[a],values[b])]
  row[a+'_over_'+b+'_paired_median']=statistics.median(ratios)
  row[a+'_over_'+b+'_full_wins']=sum(x>1 for x in ratios)
 summary.append(row)
with (out/'summary.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=summary[0]);w.writeheader();w.writerows(summary)
(out/'summary.json').write_text(json.dumps({'source':str(src),'samples':len(rows),'cases':len(summary),'memory_and_geometry_invariant':True,'method':'Per-operation ns; medians over 12 fresh processes per mode/case. Ratios pair by balanced round. Paired rows have identical geometry, population and memory. Order explicitly recorded.','limits':'A hardware performance result for matched full consumers only. Padded partial includes an extra NOP. Compare original separately for padding/placement effect; ordinary baseline instruction identity does not imply identical ASLR/hash seed/cache noise between processes.'},indent=2)+'\n')
for r in summary:
 if r['phase'] in ('hit','miss','serial_caller_cycle','serial_value_cycle','lifecycle'):
  print(r['kind'],r['arm'],r['A'],r['phase'],*(round(r[m+'_median_ns'],3) for m in ('original','padded','full')),round(r['padded_over_full_paired_median'],3))
