#!/usr/bin/env python3
"""Paired barriers at Swiss helper entry; production headers remain untouched."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import time

p=argparse.ArgumentParser()
p.add_argument('--binaries',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cpu',required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
records=[]
for trial in range(2):
    for si,seed in enumerate([713,991,1729]):
        for w,v,b in [(32,0,11),(32,8,11),(40,0,12),(40,8,11)]:
            variants=['base','lfence']+(['cpuid'] if (w,v)==(40,0) else [])
            if (trial+si)%2: variants.reverse()
            for order in [0,1,3]:
                for sink in [0,1]:
                    for variant in variants:
                        binary=a.binaries/f'{variant}-memory_ikea_{w}_{b}_{v}'
                        stem=f'w{w}-v{v}-s{seed}-t{trial}-{variant}-o{order}-sink{sink}'
                        cmd=['taskset','-c',a.cpu,str(binary),'--impl','ikea','--n','13926',
                             '--seed',str(seed),'--requests','1000000','--repetitions','3',
                             '--writes','0','--read-prefetch','0','--miss-only','50',
                             '--query-order',str(order),'--hits-only',str(sink),'--pmu','1']
                        digest=hashlib.sha256(binary.read_bytes()).hexdigest();started=time.time()
                        with (a.output/(stem+'.csv')).open('w') as out,(a.output/(stem+'.jsonl')).open('w') as err:
                            child=subprocess.run(cmd,stdout=out,stderr=err)
                        record=dict(w=w,v=v,b=b,impl=variant,seed=seed,trial=trial,miss=50,
                                    order=order,sink=sink,binary_sha256=digest,command=cmd,
                                    start=started,end=time.time(),exit=child.returncode,
                                    file=stem+'.csv',counters=stem+'.jsonl')
                        records.append(record)
                        (a.output/'runs.json').write_text(json.dumps(records,indent=2)+'\n')
                        print(json.dumps(record),flush=True)
                        assert child.returncode==0,record
                        rows=list(csv.DictReader((a.output/record['file']).open()))
                        assert len([r for r in rows if r['phase']=='read'])==3
                        # CPUID is used only where no real helper calls are
                        # needed, eliminating its large retired cost.
                        if variant=='cpuid': assert all(int(r['spill'])==0 for r in rows)
                        counters=[json.loads(x) for x in (a.output/record['counters']).read_text().splitlines()]
                        assert len(counters)==3
assert len(records)==324
print('324 paired overflow-speculation processes complete',flush=True)
