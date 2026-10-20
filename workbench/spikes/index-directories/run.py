#!/usr/bin/env python3
"""Capture a source cut, check it, and measure sequentially on one Linux CPU."""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import statistics
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--operations', type=int, default=131072)
    p.add_argument('--repetitions', type=int, default=3)
    p.add_argument('--stage',choices=['baseline','composed','placement','pressure','staged','keys','locators','all'],default='all')
    p.add_argument('--writes',type=int,default=64)
    p.add_argument('--cpu', type=int)
    p.add_argument('--march', default='')
    p.add_argument('--tune', default='generic')
    p.add_argument('--output', type=Path)
    p.add_argument('--workspace', type=Path, default=ROOT/'build/workspaces/index-directories')
    args=p.parse_args()
    if not hasattr(os, 'sched_getaffinity'): p.error('run on Linux (orb -m ubuntu from macOS)')
    if args.operations < 1 or args.repetitions < 1 or args.writes<1: p.error('positive operations/repetitions/writes required')
    allowed=os.sched_getaffinity(0)
    cpu=min(allowed) if args.cpu is None else args.cpu
    if cpu not in allowed: p.error('CPU is outside permitted affinity')
    output=(args.output or ROOT/'build/experiments/index-directories'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')).resolve()
    config={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()} | {'cpu':cpu}
    run=Run(ROOT,output,config,workspace=args.workspace)
    error=None
    print(f'Run: {output}',flush=True)
    try:
        run.step('hardware',['lscpu'])
        run.step('compiler',['clang++-21','--version'])
        run.step('configure',['cmake','-S',str(run.source_root),'-B',str(run.build_dir),'-G','Ninja',
            '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_FLAGS_RELEASE=-O3 -g1',
            '-DSIXDB_SPIKES=index-directories',f'-DSIXDB_MARCH={args.march}',f'-DSIXDB_TUNE={args.tune}'])
        targets=['index_directory_'+s for s in (['baseline','composed','placement','pressure','staged','keys','locators'] if args.stage=='all' else [args.stage])]
        run.step('build',['cmake','--build',str(run.build_dir),'--target',*targets])
        for name in ['CMakeCache.txt','compile_commands.json']:
            shutil.copy2(run.build_dir/name,output/name)
        os.sched_setaffinity(0,{cpu})
        for target in targets:
            exe=run.build_dir/'workbench/spikes/index-directories'/target
            shutil.copy2(exe,output/exe.name)
            stage=target.removeprefix('index_directory_')
            run.step(stage+'-check',[str(exe),'--check'])
            run.step(stage,[str(exe),str(args.writes if stage=='placement' else args.operations),str(args.repetitions)],stage+'.csv')
            with (output/(stage+'.csv')).open() as h: rows=list(csv.DictReader(h))
            groups={}
            dimensions=['phase','entries','rows','J','key_bytes','auxiliary_parts','summary_grain','duplicates','spacing','trace','layer','projection','dependent','arm']
            columns=[c for c in dimensions if c in rows[0]]
            for row in rows:
                key=tuple(row[k] for k in columns)
                groups.setdefault(key,[]).append(float(row['ns_per_operation']))
            with (output/(stage+'-summary.csv')).open('w') as h:
                writer=csv.writer(h); writer.writerow([*columns,'median_ns','min_ns','max_ns','repetitions'])
                for key,values in groups.items():
                    if len(values)!=args.repetitions: raise ValueError(f'incomplete repetitions: {key}')
                    writer.writerow([*key,statistics.median(values),min(values),max(values),len(values)])
        print(f'Measurements: {output}',flush=True)
    except Exception as exc: error=exc
    finally:
        os.sched_setaffinity(0,allowed)
        error=run.finish(error)
    if error: print(f'FAILED: {error}',file=sys.stderr); return 1
    return 0


if __name__=='__main__': raise SystemExit(main())
