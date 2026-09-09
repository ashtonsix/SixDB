#!/usr/bin/env python3
"""Captured independent-TU codec checks and pinned sequential comparisons."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import sys
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from experiment import Run
import prepare_prior
import prepare_bench

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target',choices=['native','zen5','granite-rapids','neoverse-v2'],default='native')
    p.add_argument('--sanitize',action='store_true')
    p.add_argument('--synthetic',action='store_true')
    p.add_argument('--check-only',action='store_true')
    a=p.parse_args()
    variant=a.target+('-sanitize' if a.sanitize else '')
    parent=Path(os.environ.get('SIXDB_RESULTS',ROOT/'build/experiments/ikea-bec'))
    out=parent/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+variant)
    run=Run(ROOT,out,vars(a),workspace=ROOT/'build/workspaces'/('ikea-bec-'+variant))
    error=None
    try:
        prior=run.input('inputs/prior',prepare_prior.get())
        data=None if a.synthetic or a.check_only else run.input('inputs/bench',prepare_bench.get())
        flags=[]
        if a.target in ['zen5','granite-rapids']:
            flags += ['-DSIXDB_MARCH='+('znver5' if a.target=='zen5' else 'graniterapids'),'-DSIXDB_TUNE='+a.target]
        if a.target=='neoverse-v2': flags += ['-DCMAKE_CXX_FLAGS=-mcpu=neoverse-v2','-DSIXDB_TUNE=neoverse-v2']
        if a.sanitize:
            flags += ['-DCMAKE_CXX_FLAGS=-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer',
                      '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run.step('compiler',['clang++-21','--version'],'compiler.txt')
        run.step('cpu',['lscpu'],'cpu.txt')
        run.step('configure',['cmake','-S',str(run.source_root),'-B',str(run.build_dir),'-G','Ninja',
                             '-DCMAKE_BUILD_TYPE='+('RelWithDebInfo' if a.sanitize else 'Release'),
                             '-DSIXDB_SPIKES=ikea-blocks','-DIKEA_PRIOR_DIR='+str(prior)]+flags)
        run.step('build',['cmake','--build',str(run.build_dir),'--target','ikea_bitsets_check','ikea_bitsets_bench',
                          'ikea_composition_check','ikea_composition_bench',
                          '-j',os.environ.get('SIXDB_BUILD_JOBS','1')])
        binaries=run.build_dir/'workbench/spikes/ikea-blocks'
        run.step('check',[str(binaries/'ikea_bitsets_check')],'checks.csv')
        run.step('composition-check',[str(binaries/'composition/ikea_composition_check')],'composition-checks.txt')
        run.step('assembly',['llvm-objdump-21','-d','-C',str(binaries/'ikea_bitsets_check')],'assembly.txt')
        run.step('composition-assembly',['llvm-objdump-21','-d','-C',str(binaries/'composition/ikea_composition_check')],'composition-assembly.txt')
        compact=['checks.csv','composition-checks.txt']
        if not a.check_only and not a.sanitize:
            cpu=int(os.environ.get('SIXDB_CPU',min(os.sched_getaffinity(0))))
            run.receipt['pinned_cpu']=cpu; run.save()
            args=['taskset','-c',str(cpu),str(binaries/'ikea_bitsets_bench')]
            if data: args.append(str(data))
            run.step('benchmark',args,'timings.csv')
            run.step('composition-benchmark',['taskset','-c',str(cpu),str(binaries/'composition/ikea_composition_bench')],'composition-timings.csv')
            compact+=['timings.csv','composition-timings.csv']
        for name in ['ikea_bitsets_check','ikea_bitsets_bench']:
            shutil.copyfile(binaries/name,out/name)
        for name in ['ikea_composition_check','ikea_composition_bench']:
            shutil.copyfile(binaries/'composition'/name,out/name)
        regenerate=['python3','workbench/spikes/ikea-blocks/report.py','{evidence}']
        run.compact(compact,regenerate)
    except Exception as exc:
        error=exc
    error=run.finish(error)
    print(out,flush=True)
    if error: raise error

if __name__=='__main__': main()
