#!/usr/bin/env python3
"""Summarize raw paired filters measurements without mixing latency/throughput."""
import argparse,collections,csv,pathlib,statistics
p=argparse.ArgumentParser();p.add_argument('inputs',nargs='+',help='label=CSV');p.add_argument('--output',required=True)
a=p.parse_args();rows=[]
for item in a.inputs:
    label,name=item.split('=',1);groups=collections.defaultdict(list)
    for r in csv.DictReader(open(name)):
        key=tuple(r[x] for x in ('module','width','op','metric','candidate'))
        groups[key].append(float(r['ns_window']))
    for key,values in sorted(groups.items()):
        reference=statistics.median(groups[key[:-1]+('fused',)])
        rows.append([label,*key,len(values),min(values),statistics.median(values),max(values),reference])
output=pathlib.Path(a.output);output.parent.mkdir(parents=True,exist_ok=True)
with output.open('w',newline='') as f:
    writer=csv.writer(f);writer.writerow(['host_isa','module','width','op','metric','candidate',
        'repeats','min_ns_window','median_ns_window','max_ns_window','fused_median_ns_window']);writer.writerows(rows)
