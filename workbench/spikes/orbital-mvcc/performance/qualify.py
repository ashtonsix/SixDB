#!/usr/bin/env python3
"""Capture and qualify complete read/publication/retained-reader adapters."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

STUDY=Path(__file__).resolve().parent
ROOT=STUDY.parents[3]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from experiment import Run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--workspace',type=Path)
    parser.add_argument('--cpu',type=int,default=2)
    args=parser.parse_args()
    if args.cpu not in os.sched_getaffinity(0): parser.error('require an allowed Linux CPU')
    label='sanitize' if args.sanitize else 'native'
    run=Run(ROOT,args.output.resolve(),dict(scope='correctness only; clocks not performance evidence',sanitize=args.sanitize),
            workspace=args.workspace or ROOT/'build/workspaces/orbital-mvcc-qualification'/label)
    error=None
    try:
        flags=['-fsanitize=address,undefined','-fsanitize-address-use-after-return=always','-fno-omit-frame-pointer'] if args.sanitize else []
        command=['cmake','-S',str(run.source_root),'-B',str(run.build_dir),'-G','Ninja',
                 '-DCMAKE_BUILD_TYPE=Release','-DSIXDB_SPIKES=orbital-mvcc','-DSIXDB_BENCHMARKS=',
                 '-DSIXDB_MARCH=','-DSIXDB_TUNE=generic']
        if flags: command += ['-DCMAKE_CXX_FLAGS='+' '.join(flags),'-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run.step('configure',command)
        targets=['orbital_versioned_performance_check','orbital_versioned_performance_bench']
        run.step('build',['cmake','--build',str(run.build_dir),'--target',*targets,'-j2'])
        built=run.build_dir/STUDY.relative_to(ROOT)
        prefix=['env','ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1:halt_on_error=1',
                'UBSAN_OPTIONS=halt_on_error=1','taskset','-c',str(args.cpu)]
        run.step('independent-check',prefix+[str(built/targets[0]),str(run.output/'check-db')],'independent-check.json')
        shutil.rmtree(run.output/'check-db')
        outcomes=[]
        backends=['orbital','lmdb','candidate8','candidate16','candidate32']
        cases=[]
        for backend in backends:
            for batch in [1,64]:
                for age in [0,1,8,64]:
                    for mode in ['throughput','latency']:
                        cases.append(dict(backend=backend,batch=batch,**{'retention-age':age},mode=mode,
                                          rows=68,objects=4,ops=131,workload='point-update'))
                for rows,objects in [(512,64),(32768,4096)]:
                    for codec in ['legacy','consume16']:
                        cases.append(dict(backend=backend,batch=batch,mode='throughput',rows=rows,objects=objects,
                                          ops=8195,workload='point-hit',codec=codec,**{'metadata-bytes':16*1024*1024}))
        for index,case in enumerate(cases):
            name=f'case-{index:03d}';db=run.output/f'{name}-db'
            argv=prefix+[str(built/targets[1]),'--db',str(db)]
            for key,value in case.items(): argv += [f'--{key}',str(value)]
            run.step(name,argv,f'{name}.json')
            result=json.loads((run.output/f'{name}.json').read_text())
            if not result['passed']: raise ValueError(f'{name} failed')
            if case['workload']=='point-update':
                groups=(case['ops']+case['batch']-1)//case['batch'];age=case['retention-age']
                if result['timed_retained_first_discoveries']!=(groups//age if age else 0):
                    raise ValueError('old-cut first-discovery extent differs')
                if result['release_and_drain']['process_cpu_ns']<=0: raise ValueError('release CPU not sampled')
            if result['process_resources']['peak_rss_kib']<=0: raise ValueError('own process VmHWM unavailable')
            outcomes.append(dict(case=case,passed=True,digest=result['outcome_digest']))
            if db.exists(): shutil.rmtree(db)
        refusals=[]
        for backend in ['orbital','candidate8','candidate16','candidate32']:
            name=f'quota-{backend}'
            argv=prefix+[str(built/targets[1]),'--backend',backend,'--rows','32768','--objects','4096',
                         '--metadata-bytes',str(1024*1024),'--ops','1']
            run.step(name,argv,check=False)
            status=run.receipt['commands'][-1]['returncode']
            stderr=(run.output/f'{name}.stderr').read_text()
            if status!=1 or 'refused' not in stderr: raise ValueError(f'quota refusal missing: {backend}: {stderr}')
            refusals.append(dict(backend=backend,returncode=status,stderr=stderr.strip()))
        # Exact captured project inputs used by the actual object compilation.
        # Standard-library/toolchain identity and flags remain separate receipts.
        dependencies=subprocess.check_output(['ninja','-C',str(run.build_dir),'-t','deps'],text=True)
        compiled={}
        for line in dependencies.splitlines():
            if not line.startswith('    '): continue
            path=Path(line.strip())
            if path.is_relative_to(run.source_root):
                name=str(path.relative_to(run.source_root))
                compiled[name]=run.receipt['source_files_sha256'][name]
        if not compiled or not any(n.endswith('/performance/main.cpp') for n in compiled):
            raise ValueError('compiled input identity unavailable')
        receipt=dict(format=2,passed=True,source_digest=run.receipt['source_digest'],source_sha256=compiled,
            compiled_source_digest=hashlib.sha256(json.dumps(compiled,sort_keys=True).encode()).hexdigest(),
            binary_root=str(built),build_root=str(run.build_dir),
            binary_sha256={t:hashlib.sha256((built/t).read_bytes()).hexdigest() for t in targets},
            sanitizer_flags=flags,independent_check=json.loads((run.output/'independent-check.json').read_text()),
            cli_cases=outcomes,capacity_refusals=refusals,
            scope='real candidate publication/old discovery/partial private failure; rolling retained windows; collision-safe bounded read caches through4096 Objects')
        (run.output/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
        run.compact(['qualification.json'],[])
    except Exception as exc:
        error=exc
    finally:
        error=run.finish(error)
    if error: raise error
    print(run.output/'qualification.json')


if __name__=='__main__': main()
