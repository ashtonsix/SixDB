#!/usr/bin/env python3
"""Capture and run the prefix/bucket native comparisons sequentially on one CPU."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import sys
from datetime import datetime, timezone

STUDY=Path(__file__).resolve().parent
ROOT=STUDY.parents[2]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from experiment import Run
sys.path.insert(0,str(ROOT/'workbench/spikes/memory-characterisation'))
from hardware import discover


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--families',nargs='+',choices=['prefix','bucket'],default=['prefix','bucket'])
    ap.add_argument('--rows',type=int,nargs='+',default=[4096,262144])
    ap.add_argument('--seeds',type=int,nargs='+',default=[918273,712367])
    ap.add_argument('--reps',type=int,default=3)
    ap.add_argument('--ms',type=int,default=10)
    ap.add_argument('--filter',default='')
    ap.add_argument('--cpu',type=int)
    ap.add_argument('--march',default='')
    ap.add_argument('--tune',default='generic')
    ap.add_argument('--output',type=Path)
    ap.add_argument('--workspace',type=Path,default=ROOT/'build/workspaces/layout-consumers')
    args=ap.parse_args()
    if not hasattr(os,'sched_getaffinity'):ap.error('Run on Linux; use orb -m ubuntu on macOS')
    if any(n<32 or ('bucket' in args.families and n>=524288) for n in args.rows):
        ap.error('rows must be >=32; this bucket fixture requires rows<524288 for 20-bit hit/miss keys')
    allowed=os.sched_getaffinity(0)
    cpu=min(allowed) if args.cpu is None else args.cpu
    if cpu not in allowed:ap.error('CPU is outside allowed affinity')
    output=(args.output or ROOT/'build/experiments/layout-consumers'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')).resolve()
    config={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()}|{'cpu':cpu}
    run=Run(ROOT,output,config,workspace=args.workspace.resolve())
    selected=['hardware.json','cases.json','summary.md','summary.csv']
    error=None
    try:
        hardware=discover(cpu)
        (output/'hardware.json').write_text(json.dumps(hardware,indent=2)+'\n')
        run.step('compiler',['clang++-21','--version'])
        run.step('configure',['cmake','-S',str(run.source_root),'-B',str(run.build_dir),'-G','Ninja',
            '-DCMAKE_BUILD_TYPE=Release','-DSIXDB_SPIKES=layout-analyser',
            f'-DSIXDB_MARCH={args.march}',f'-DSIXDB_TUNE={args.tune}'])
        targets=[f'layout_{family}_probe' for family in args.families]
        run.step('build',['cmake','--build',str(run.build_dir),'--target',*targets,'-j','1'])
        (output/'bin').mkdir()
        for name in ['compile_commands.json','CMakeCache.txt']:
            shutil.copy2(run.build_dir/name,output/name)
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        os.sched_setaffinity(0,{cpu})
        cases=[]
        for family in args.families:
            binary=output/'bin'/f'layout_{family}_probe'
            shutil.copy2(run.build_dir/STUDY.relative_to(ROOT)/family/binary.name,binary)
            run.step(f'{family}-check',[str(binary),'--check'],f'{family}-check.csv')
            selected += [f'{family}-check.stderr']
            for rows in args.rows:
                for seed in args.seeds:
                    label=f'{family}-{rows}-{seed}'
                    queries=max(32768,min(rows,1048576))
                    command=[str(binary),'--rows',str(rows),'--queries',str(queries),'--seed',str(seed),
                        '--reps',str(args.reps),'--ms',str(args.ms)]
                    if args.filter:command+=['--filter',args.filter]
                    run.step(label,command,label+'.csv')
                    selected += [label+'.csv',label+'.stderr']
                    cases.append({'family':family,'rows':rows,'queries':queries,'seed':seed,
                        'samples':label+'.csv','accounting':label+'.stderr',
                        'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()})
        (output/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
        run.step('analyse',[sys.executable,str(run.source_root/STUDY.relative_to(ROOT)/'analyze.py'),str(output)])
        run.compact(selected,['python3','workbench/spikes/layout-analyser/analyze.py','{evidence}'])
    except Exception as exc:error=exc
    finally:
        os.sched_setaffinity(0,allowed)
        error=run.finish(error)
    if error:raise error
    print(output)


if __name__=='__main__':main()
