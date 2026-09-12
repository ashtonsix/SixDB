#!/usr/bin/env python3
"""Run the bounded shared-dirty-buffer experiment on Linux."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from experiment import Run

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case',choices=['small','spread','hot','roots','points','batch256','batch4096','hot4096','hot_roots'])
    p.add_argument('--filter-kib',type=int,default=32)
    p.add_argument('--repetitions',type=int,default=3)
    p.add_argument('--min-time',type=float,default=.03)
    p.add_argument('--filter',default='^(small|spread|hot)/t4/(eager_atomic|eager_striped|word_check_span|append_scan|append_flush)/')
    p.add_argument('--march',default='')
    a=p.parse_args()
    if a.repetitions<1 or a.min_time<=0: p.error('repetitions and min-time must be positive')
    if not hasattr(os,'sched_getaffinity'): p.error('Run on Linux, e.g. with orb -m ubuntu')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    out=ROOT/'build/experiments/aggregate-maintenance/dirty-buffer'/stamp
    build=ROOT/'build/clang/dirty-buffer'
    run=Run(ROOT,out,vars(a)|{'allowed_cpus':sorted(os.sched_getaffinity(0))}, workspace=ROOT / 'build/workspaces/dirty-buffer')
    build = run.build_dir
    print(f'Run: {out}',flush=True)
    error=None
    try:
        run.step('git-head',['git','rev-parse','HEAD'])
        run.step('git-status',['git','status','--short'])
        run.step('compiler',['clang++-21','--version'])
        run.step('hardware',['lscpu'])
        run.step('configure',['cmake','-S',str(run.source_root),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DSIXDB_SPIKES=aggregate-maintenance',f'-DSIXDB_MARCH={a.march}','-DSIXDB_TUNE=generic'])
        run.step('build',['cmake','--build',str(build),'--target','dirty_buffer_probe','-j','4'])
        for name in ('CMakeCache.txt','compile_commands.json'): shutil.copy2(build/name,out/name)
        binary=out/'dirty_buffer_probe'
        shutil.copy2(build/'workbench/spikes/aggregate-maintenance/dirty-buffer/dirty_buffer_probe',binary)
        common=[f'--filter-kib={a.filter_kib}']+([f'--case={a.case}'] if a.case else [])
        run.step('check',[str(binary),'--check',*common],'accounting.csv')
        run.step('benchmark',[str(binary),*common,f'--benchmark_filter={a.filter}',f'--benchmark_repetitions={a.repetitions}',f'--benchmark_min_time={a.min_time}s','--benchmark_enable_random_interleaving=false','--benchmark_report_aggregates_only=false','--benchmark_display_aggregates_only=true','--benchmark_color=false',f'--benchmark_out={out/"benchmark.json"}','--benchmark_out_format=json'])
        run.step('analyse',[sys.executable,str(run.source_root / HERE.relative_to(ROOT) / 'analyze.py'),str(out)])
    except Exception as e: error=e
    error=run.finish(error)
    if error: print(f'FAILED: {error}',file=sys.stderr); return 1
    print(f'Complete: {out/"summary.md"}')
    print(f'To keep this run: python3 workbench/tools/artifacts.py retain {out.relative_to(ROOT)} '
          'workbench/spikes/aggregate-maintenance/dirty-buffer/evidence/NAME')
    return 0
if __name__=='__main__': raise SystemExit(main())
