#!/usr/bin/env python3
"""Assemble small explanatory context beside analysis tables; raw stays in S3."""
import argparse
import csv
import datetime
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT=Path(__file__).resolve().parents[4]


def main():
    p=argparse.ArgumentParser()
    p.add_argument('campaign',type=Path)
    args=p.parse_args()
    base=args.campaign
    out=base/'summary'
    refs=json.loads((base/'workers.json').read_text())
    checks=json.loads((out/'checks.json').read_text())
    resources=json.loads((base/'resources.json').read_text())
    instances={r['InstanceId']:r for r in resources['instances']}
    context=[]; counters=[]; costs=[]; start=[]; end=[]; source_hashes=set()
    rates={'m7i.xlarge':.2016,'i4i.xlarge':.343}
    for name,m in sorted(refs['members'].items()):
        folder=ROOT/'build/workers'/m['job']
        results=folder/'results'
        identity=json.loads((results/'identity.json').read_text())
        job=json.loads((folder/'job.json').read_text())
        status=json.loads((folder/'status.json').read_text())
        initial=json.loads((results/'initial.json').read_text())
        final=json.loads((results/'final.json').read_text())
        source_hashes.add(job['source']['digest'])
        context.append(dict(name=name,**identity,job=m['job'],source_sha256=job['source']['digest'],
                            ami=job['config']['ami'],threads_per_core=job['config']['threads_per_core'],
                            driver=final['driver']['stdout'],timestamping=final['timestamping']['stdout'],
                            clocksource=final['clocksource']['stdout'],clock_names=final['clock_names']['stdout'],
                            coalescing=final['coalescing']['stdout'],link=json.loads(final['link']['stdout'])))
        before={k:int(v) for k,v in re.findall(r'^\s*([^:\s]+):\s*(\d+)',initial['counters']['stdout'],re.M)}
        after={k:int(v) for k,v in re.findall(r'^\s*([^:\s]+):\s*(\d+)',final['counters']['stdout'],re.M)}
        for k in after:
            if any(s in k for s in ('phc_','allowance','drop','error')):
                counters.append(dict(node=identity['node'],counter=k,before=before.get(k),after=after[k],
                                     delta=after[k]-before[k] if k in before else None))
        instance=instances[m['worker']['instance_id']]
        launched=datetime.datetime.fromisoformat(instance['LaunchTime']).timestamp()
        seconds=status['updated_at']-launched
        costs.append(dict(instance=instance['InstanceId'],type=instance['InstanceType'],
                          launch=instance['LaunchTime'],completion=status['updated_at'],
                          elapsed_seconds=seconds,usd_per_hour=rates[instance['InstanceType']],
                          modeled_compute_usd=seconds/3600*rates[instance['InstanceType']]))
        cases=json.loads((results/'cases.json').read_text())
        for case in (cases[0],cases[-1]):
            with gzip.open(results/case['file'],'rt') as f: rows=list(csv.DictReader(f))
            start.append(min(int(r['sw_real']) for r in rows))
            end.append(max(int(r['sw_real']) for r in rows))
    assert len(source_hashes)==1
    (out/'hosts.json').write_text(json.dumps(context,indent=2)+'\n')
    for filename in ('workers.json','resources.json','pricing.json'):
        shutil.copyfile(base/filename,out/filename)
    def write(name,rows):
        with (out/name).open('w') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    write('counters.csv',counters)
    write('compute.csv',costs)
    sensitivity=[]
    for ppm,folder in [(50,base/'sensitivity-50'),(100,out),(1000,base/'sensitivity-1000')]:
        for r in csv.DictReader((folder/'pair-blocks.csv').open()):
            sensitivity.append(dict(ppm=ppm,**{k:r[k] for k in ['node_a','node_b','az_a','az_b','repeat','p50_us','p50_lo_us','p50_hi_us']}))
    write('sensitivity.csv',sensitivity)
    info=dict(measured_start_utc=datetime.datetime.fromtimestamp(min(start)/1e9,datetime.timezone.utc).isoformat(),
              measured_end_utc=datetime.datetime.fromtimestamp(max(end)/1e9,datetime.timezone.utc).isoformat(),
              source_sha256=next(iter(source_hashes)),compute_through_completion_usd=sum(c['modeled_compute_usd'] for c in costs),
              cross_az_probe_ipv4_bytes=checks['cross_az_ipv4_bytes'],modeled_cross_az_probe_usd=checks['cross_az_ipv4_bytes']/1e9*.02,
              cost_limits='Compute includes reused preflight lifetimes through archive completion, but excludes shutdown lag, EBS, public IPv4, S3 and tax. Transfer is a byte/rate model, not a bill.',
              analysis_sources={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in Path(__file__).parent.iterdir()
                                if f.name in ('analyze.py','plot.py','edges.py','assemble.py','check.py','recover.py')})
    (out/'campaign.json').write_text(json.dumps(info,indent=2)+'\n')
    print(json.dumps(info,indent=2))


if __name__=='__main__':main()
