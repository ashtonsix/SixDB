#!/usr/bin/env python3
"""Audit the mutable-merge architecture comparison, including every mode/control."""
import argparse,csv,json,math,statistics,itertools,hashlib
from collections import defaultdict
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('root',type=Path);p.add_argument('--modes',type=int,nargs='+',required=True)
p.add_argument('--reducer-kind',type=int,choices=range(4),default=0);p.add_argument('--values',type=int,nargs='+',default=[0,8,64]);p.add_argument('--inputs',type=int,nargs='+',default=[8]);p.add_argument('--k14',action='store_true');p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.root
assert '21.1.8' in (root/'compiler.txt').read_text()
job=json.loads((root/'job.json').read_text());hardware=json.loads((root/'lscpu.json').read_text())
cpu=job['config']['env']['MERGE_CPU']
assert (root/'complete.txt').read_text().strip()==f'{cpu} completed'
assert (root/'check.txt').read_text().count('HISTORY_PASS,')>=12
assert 'PASS ordered mutable and bucket-wise K-way histories' in (root/'check.txt').read_text()
if (root/'sanitize.txt').exists():assert 'PASS ordered mutable and bucket-wise K-way histories' in (root/'sanitize.txt').read_text()
manifest=json.loads((root/'cases.json').read_text())if(root/'cases.json').exists()else None
owners_order=(manifest or{}).get('owners',['accumulate','kway','routed_before','abseil'])
repetitions=(manifest or{}).get('repetitions',3)
assert repetitions>0
owners=set(owners_order);assert len(owners)==4 and {'accumulate','abseil'}<=owners and bool({'scalar_reference','routed_before'}&owners)
march,tune={'zen5':('znver5','znver5'),'granite-rapids':('graniterapids','graniterapids'),'neoverse-v2':('armv8-a+simd','neoverse-v2')}[cpu]
commands=json.loads((root/'compile.json').read_text())
for name in ('bench.cpp','raw_hash_set.cc'):
 chosen=[r for r in commands if r['file'].endswith(name)];assert chosen
 for r in chosen:
  cmd=r.get('command',' '.join(r.get('arguments',[])))
  assert all(flag in cmd for flag in (f'-march={march}',f'-mtune={tune}','-DNDEBUG'))
  if name=='bench.cpp':
   flag=f'-DMERGE_REDUCER_KIND={a.reducer_kind}'
   assert flag in cmd or(a.reducer_kind==0 and '-DMERGE_REDUCER_KIND='not in cmd)
   if 'before_refactor'in owners:assert '-DMERGE_COMPARE_REFACTOR=1'in cmd
files=set();checksums={};cells=defaultdict(lambda:defaultdict(list));count=0
seeds=[713,3191,76543]
cases=[(30,b,m)for b in (10,15)for m in a.inputs]+([(14,10,m)for m in a.inputs]if a.k14 else [])
overlaps=[0,50,100]
if manifest:
 assert manifest['modes']==a.modes and manifest['values']==a.values
 assert manifest.get('reducer_kind',0)==a.reducer_kind
 cases=[tuple(x)for x in manifest['cases']];overlaps=manifest['overlaps'];assert len(set(cases))==len(cases)
for mode,v,(k,b,m),o,seed in itertools.product(a.modes,a.values,cases,overlaps,seeds):
 name=f'mode{mode}-v{v}-k{k}-b{b}-m{m}-o{o}-s{seed}.csv';files.add(name)
 rows=list(csv.DictReader((root/name).open()));assert len(rows)==4*repetitions
 assert {(r['owner'],int(r['rep']))for r in rows}==set(itertools.product(owners,range(repetitions)))
 if (manifest or{}).get('order_scheme')=='balanced_latin_square':
  assert repetitions%4==0
  assert manifest['source_conditioning']=='incoming_entry_prewalk'
  expected=[owners_order[(i+rep+seed%4)%4]for rep in range(repetitions)for i in (0,1,3,2)]
  assert [r['owner']for r in rows]==expected,(name,'unbalanced control order')
 source_budget=63 if k<=30 else 95;source_n=8*(source_budget//(k+1));n=2**b*source_n*85//100;common=n*o//100;union=common+m*(n-common);db=b+(0 if o==100 else 1 if m==2 else 2 if k==47 else 3);dk=k-(db-b);destination_b=64 if dk<=30 else 96;destination_n=8*((destination_b-1)//(dk+1))
 key=(k,v,b,m,o,seed);sums={r['checksum']for r in rows};assert len(sums)==1
 if key in checksums:assert checksums[key]==sums
 else:checksums[key]=sums
 for r in rows:
  for field,want in [('K',k),('V',v),('b',b),('inputs',m),('destination_b',db),('overlap',o),('seed',seed),('incoming',(m-1)*n),('union',union)]:assert int(r[field])==want
  for field in ('merge_ns','prepare_ns','total_ns'):assert math.isfinite(float(r[field]))and float(r[field])>0
  assert abs(float(r['merge_ns'])+float(r['prepare_ns'])-float(r['total_ns']))<2e-6
  if r['owner']!='abseil':assert int(r['primary_bytes'])==2**db*(destination_b+destination_n*v)
  if r['owner']!='kway':assert int(r['workspace_bytes'])==0
  cells[k,v,b,m,o,mode][r['owner']].append(r);count+=1
assert {x.name for x in root.glob('mode*.csv')}==files
summary=[]
for (k,v,b,m,o,mode),data in sorted(cells.items()):
 row={'K':k,'V':v,'b':b,'inputs':m,'overlap':o,'mode':mode,'samples':len(seeds)*repetitions,'measurements':{}}
 for owner,rows in data.items():
  assert len(rows)==len(seeds)*repetitions
  measures={}
  for field in ('merge_ns','prepare_ns','total_ns','primary_bytes','overflow_bytes','spills','workspace_bytes'):
   values=[float(r[field])for r in rows];med=statistics.median(values)
   measures[field]={'median':med,'mad':statistics.median(abs(x-med)for x in values)}
  row['measurements'][owner]=measures
 summary.append(row)
result={'owners':owners_order,'reducer_kind':a.reducer_kind,'manifest':manifest,'cpu':cpu,'job':job.get('id',root.parent.name),'source':job['source'],'rows':count,'modes':a.modes,'values':a.values,'hardware':hardware,'sanitizers':(root/'sanitize.txt').exists(),'summary':summary}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
with a.output.with_suffix('.csv').open('w') as f:
 fields=['K','V','b','inputs','overlap','mode','samples']+[f'{owner}_{phase}'for owner in owners_order for phase in ('merge_ns','prepare_ns','total_ns')]
 writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
 for row in summary:
  record={k:row[k]for k in ('K','V','b','inputs','overlap','mode','samples')}
  record.update({f'{owner}_{phase}':row['measurements'][owner][phase]['median']for owner in owners for phase in ('merge_ns','prepare_ns','total_ns')})
  writer.writerow(record)
print(f'Audited {count} rows, {len(summary)} complete variant cells, source-order/full-payload oracles, actual Abseil/matching flags; sanitizers={result["sanitizers"]}')
