#!/usr/bin/env python3
"""Captured independent-TU integer kernel checks and sequential measurements."""
import argparse
from datetime import datetime,timezone
import os
from pathlib import Path
import shutil
import sys
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from experiment import Run
import prepare_prior

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',choices=['native','zen5','granite-rapids','neoverse-v2','avx2-qemu'],default='native')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--check-only',action='store_true')
    parser.add_argument('--kernels-only',action='store_true')
    parser.add_argument('--scan-reader',choices=['fragment-classes','constant-offsets'],default='fragment-classes')
    parser.add_argument('--scan-register-masks',action='store_true',help='Screen register-broadcast masks in x86 5/7-bit encoders')
    parser.add_argument('bench',nargs=argparse.REMAINDER)
    args=parser.parse_args()
    variant=args.target+('-sanitize' if args.sanitize else '')
    if args.scan_reader!='fragment-classes': variant+='-'+args.scan_reader
    if args.scan_register_masks: variant+='-register-masks'
    output=Path(os.environ.get('SIXDB_RESULTS',ROOT/'build/experiments/ikea-integers'))/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+variant)
    run=Run(ROOT,output,vars(args),workspace=ROOT/'build/workspaces'/('ikea-integers-'+variant))
    error=None
    try:
        prior=run.input('inputs/prior',prepare_prior.get())
        cmake=[]; cxx=[]
        if args.scan_reader=='constant-offsets': cxx+=['-DIP_SCAN_CONSTANT_OFFSETS=1']
        if args.scan_register_masks: cxx+=['-DIP_SCAN_REGISTER_MASKS=1']
        if args.target in ['zen5','granite-rapids']:
            cmake+=['-DSIXDB_MARCH='+('znver5' if args.target=='zen5' else 'graniterapids'),'-DSIXDB_TUNE='+args.target]
        if args.target=='neoverse-v2': cxx+=['-mcpu=neoverse-v2'];cmake+=['-DSIXDB_TUNE=neoverse-v2']
        if args.target=='avx2-qemu':
            cxx+=['--target=x86_64-linux-gnu','-march=x86-64-v3']
            cmake+=['-DCMAKE_SYSTEM_NAME=Linux','-DCMAKE_SYSTEM_PROCESSOR=x86_64']
        if args.sanitize: cxx+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        if cxx: cmake+=['-DCMAKE_CXX_FLAGS='+' '.join(cxx)]
        run.step('compiler',['clang++-21','--version'],'compiler.txt')
        run.step('cpu',['lscpu'],'cpu.txt')
        run.step('cache-context',['python3','-c',
            'from pathlib import Path; import json; roots=[Path("/sys/devices/system/cpu/cpu0/cache"),Path("/sys/kernel/mm/transparent_hugepage")]; print(json.dumps({str(p):p.read_text().strip() for root in roots for p in root.rglob("*") if p.is_file() and p.name in {"size","level","type","coherency_line_size","shared_cpu_list","enabled","defrag"}},indent=2))'], 'cache-context.json')
        run.step('configure',['cmake','-S',str(run.source_root),'-B',str(run.build_dir),'-G','Ninja',
            '-DCMAKE_BUILD_TYPE='+('RelWithDebInfo' if args.sanitize else 'Release'),
            '-DSIXDB_SPIKES=ikea-integers','-DIKEA_INTEGER_PRIOR_DIR='+str(prior)]+cmake)
        targets=['ikea_integer_check']
        if not args.kernels_only: targets+=['ikea_integer_composition_check']
        if not args.check_only and not args.sanitize and args.target!='avx2-qemu': targets+=['ikea_integer_bench']
        if 'ikea_integer_bench' in targets:
            targets+=['ikea_integer_pairs_bench']
            if not args.kernels_only: targets+=['ikea_integer_composition_bench']
        run.step('build',['cmake','--build',str(run.build_dir),'--target',*targets,'-j',os.environ.get('SIXDB_BUILD_JOBS','1')])
        binaries=run.build_dir/'workbench/spikes/ikea-integers'
        prefix=['qemu-x86_64','-cpu','max','-L','/usr/x86_64-linux-gnu'] if args.target=='avx2-qemu' else []
        run.step('check',prefix+[str(binaries/'ikea_integer_check')],'checks.csv')
        run.step('assembly',['llvm-objdump-21','-dr','-C',str(binaries/'ikea_integer_check')],'assembly.txt')
        compact=['checks.csv']
        if 'ikea_integer_composition_check' in targets:
            run.step('composition-check',prefix+[str(binaries/'composition/ikea_integer_composition_check')],'composition-checks.txt')
            run.step('composition-assembly',['llvm-objdump-21','-dr','-C',str(binaries/'composition/ikea_integer_composition_check')],'composition-assembly.txt')
            compact+=['composition-checks.txt']
        if 'ikea_integer_bench' in targets:
            cpu=int(os.environ.get('SIXDB_CPU',min(os.sched_getaffinity(0))))
            run.receipt['pinned_cpu']=cpu;run.save()
            bench=args.bench[1:] if args.bench[:1]==['--'] else args.bench
            run.step('benchmark',['taskset','-c',str(cpu),str(binaries/'ikea_integer_bench'),*bench],'timings.csv')
            compact+=['timings.csv']
            run.step('pairs-benchmark',['taskset','-c',str(cpu),str(binaries/'ikea_integer_pairs_bench')],'pairs-timings.csv')
            compact+=['pairs-timings.csv']
            if not args.kernels_only:
                run.step('composition-benchmark',['taskset','-c',str(cpu),str(binaries/'composition/ikea_integer_composition_bench')],'composition-timings.csv')
                compact+=['composition-timings.csv']
        for name in targets:
            shutil.copyfile((binaries/'composition'/name) if 'composition' in name else binaries/name,output/name)
        run.compact(compact,[])
    except Exception as exc: error=exc
    error=run.finish(error)
    print(output,flush=True)
    if error: raise error

if __name__=='__main__': main()
