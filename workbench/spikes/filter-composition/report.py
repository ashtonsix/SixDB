#!/usr/bin/env python3
"""Absolute complete-consumer ns, with separately paired latency/throughput."""
import argparse,collections,csv,pathlib,statistics
p=argparse.ArgumentParser();p.add_argument('inputs',nargs='+');p.add_argument('--output',required=True);a=p.parse_args()
rows=[]
for arg in a.inputs:
 label,path=arg.split('=',1);g=collections.defaultdict(list)
 for r in csv.DictReader(open(path)):
  key=tuple(r[k] for k in ('workload','width_or_cases','op','active','metric','candidate'))
  g[key].append(float(r['ns_window']))
 for key,v in sorted(g.items()):
  def median(name):
   x=g.get(key[:-1]+(name,));return statistics.median(x) if x else ''
  rows.append([label,*key,len(v),min(v),statistics.median(v),max(v),median('inline_fused'),median('bound_fused')])
out=pathlib.Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
with out.open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['host_isa','workload','width_or_cases','op','active','metric','candidate','repeats','min_ns','median_ns','max_ns','inline_ns','bound_ns']);w.writerows(rows)
