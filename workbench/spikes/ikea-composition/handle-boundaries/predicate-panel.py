#!/usr/bin/env python3
"""One-image op2 before/after/duplicate control using literal frozen consumers.

The driver only orders arm calls. Each namespace-isolated arm compiles the
unchanged actual runtime.cpp, extra.cpp, extra_tuple_2.cpp and all nine TP owner TUs.
The original 48 cells/arm retain their byte oracles, 512-by-eight own-arm conditioning,
request trace, exact masks and timing loop. This is not the earlier independent-
process comparison, a production placement policy or a broad new tournament.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
INPUTS = Path('panel-inputs')
PAIR_SHA = '3b4f6992bc7f256675534d70c5140eb8e2b540ac1b1ae1dfea45542daf24e937'
MAP_SHA = {'before':'929b17889f350d24e2e740c052fc28411fcfaf589261c0216cc9b881b9f2cde6',
           'after':'a4388c9a6695826bec2703d1da8ff1e56062b8c9f81bfeb489b047a2d0ef1d7d'}
ARMS = ('before','after','duplicate')
TP = ('layout','initialize','prepare','kernels','gpr','routes','packet_layout','packet_prepare','packet')
BENCH = ('runtime','extra','extra_tuple_2')
FLAGS = ['-O3','-g','-std=c++23','-fvisibility=hidden','-fvisibility-inlines-hidden',
         '-Wall','-Wextra','-Wpedantic','-fno-fast-math','-ffp-contract=off','-fdenormal-fp-math=ieee',
         '-ffunction-sections','-fdata-sections','-mtune=znver5',
         '-DSIXDB_TUNE_GENERIC=0','-DSIXDB_TUNE_GRANITE_RAPIDS=0','-DSIXDB_TUNE_NEOVERSE_V2=0',
         '-DSIXDB_TUNE_NEOVERSE_V3=0','-DSIXDB_TUNE_ZEN4=0','-DSIXDB_TUNE_ZEN5=1']
SHARED = ('predicate-panel.cpp','predicate-panel.py','predicate-panel.sh','predicate-panel-audit.py','predicate-panel-results.py',
          'projection-linked-filter.py','linked-filter.py')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True).encode()).hexdigest()


def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')


def verify_pair(pair):
    assert sha(pair/'manifest.json')==PAIR_SHA
    manifest=json.loads((pair/'manifest.json').read_text())
    assert manifest['source_manifest_sha256']==MAP_SHA
    for arm,mapping in manifest['source_sha256'].items():
        assert digest(mapping)==MAP_SHA[arm]
        assert all(sha(pair/arm/p)==h for p,h in mapping.items())
    return manifest


def source_manifest(args):
    pair=args.source_pair.resolve()
    parent=verify_pair(pair)
    original=json.loads((args.original_results/'receipt.json').read_text())
    assert original['complete'] and original['variant']=='copied-projection-final'
    assert original['source_manifest_sha256']==MAP_SHA
    references={}
    replacements=[f'{INPUTS}/{arm}={pair/arm}' for arm in ('before','after')]
    for profile in ('avx2','avx512'):
        for arm in ('before','after'):
            binary=args.original_results/profile/arm/'filter_composition_extra'
            record=next(r for r in original['binaries'] if (r['profile'],r['arm'],r['family'])==(profile,arm,'filters'))
            assert sha(binary)==record['sha256'] and binary.stat().st_size==record['bytes']
            name=str(Path('references')/profile/arm/'filter_composition_extra')
            references[name]={'sha256':record['sha256'],'bytes':record['bytes']}
            replacements.append(f'{INPUTS}/{name}={binary.resolve()}')
    manifest={'scope':__doc__,'parent_manifest_sha256':PAIR_SHA,'source_manifest_sha256':MAP_SHA,
        'source_sha256':parent['source_sha256'],'reference_receipt_sha256':sha(args.original_results/'receipt.json'),
        'reference_binaries':references,'shared_sha256':{str((HERE/name).relative_to(ROOT)):sha(HERE/name) for name in SHARED},
        'orders':[['before','after','duplicate'],['after','duplicate','before'],['duplicate','before','after'],
                  ['duplicate','after','before'],['after','before','duplicate']],
        'namespace_clones':{arm:{'ikea':'panel_'+arm+'_ikea','runtime':'panel_'+arm+'_runtime','extras':'panel_'+arm+'_extras'} for arm in ARMS}}
    return manifest,replacements


def stage(args):
    """Cheap local identity preflight; symlink immutable sources, no capture."""
    manifest,replacements=source_manifest(args)
    assert not args.output.exists()
    args.output.mkdir(parents=True)
    for replacement in replacements:
        destination,source=replacement.split('=',1)
        relative=Path(destination).relative_to(INPUTS)
        link=args.output/relative
        link.parent.mkdir(parents=True,exist_ok=True)
        link.symlink_to(Path(source).resolve(),target_is_directory=Path(source).is_dir())
    save(args.output/'manifest.json',manifest)
    verify(args.output)
    print(json.dumps({'inputs':str(args.output),'manifest_sha256':sha(args.output/'manifest.json')},indent=2))


def prepare(args):
    manifest,replacements=source_manifest(args)
    assert not args.metadata.exists()
    args.metadata.mkdir(parents=True)
    path=args.metadata/'manifest.json';save(path,manifest)
    replacements.append(f'{INPUTS}/manifest.json={path.resolve()}')
    helper=ROOT/'workbench/spikes/ikea-composition/cohesion-v2-native.py'
    spec=importlib.util.spec_from_file_location('cohesion_native',helper)
    native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
    result=native.tool_module('capture').capture(ROOT,args.capture.resolve(),replacements=replacements,
                                                 expected=manifest['shared_sha256'])
    print(json.dumps({'capture':str(args.capture),'source_digest':result['source_digest'],
                      'manifest_sha256':sha(path)},indent=2))


def verify(inputs):
    manifest=json.loads((inputs/'manifest.json').read_text())
    assert manifest['parent_manifest_sha256']==PAIR_SHA and manifest['source_manifest_sha256']==MAP_SHA
    for arm,mapping in manifest['source_sha256'].items():
        assert digest(mapping)==MAP_SHA[arm]
        assert all(sha(inputs/arm/name)==wanted for name,wanted in mapping.items())
    for name,record in manifest['reference_binaries'].items():
        assert sha(inputs/name)==record['sha256']
    for name,wanted in manifest['shared_sha256'].items():
        assert sha(ROOT/name)==wanted
    return manifest


def compile_object(task):
    command,source,obj,log,dep=task
    log.parent.mkdir(parents=True,exist_ok=True)
    cache=obj.with_suffix('.compile.json')
    if cache.exists():
        old=json.loads(cache.read_text())
        if old['command']==command and obj.exists() and sha(obj)==old['object_sha256'] and all(
                Path(p).exists() and sha(Path(p))==h for p,h in old['dependencies_sha256'].items()):
            return old|{'reused':True}
    start=time.monotonic()
    with log.open('w') as output:
        process=subprocess.run(command,cwd=ROOT,stdout=output,stderr=subprocess.STDOUT)
    result={'command':command,'source':str(source),'source_sha256':sha(source),
            'exit_code':process.returncode,'seconds':time.monotonic()-start,'log':str(log),'log_sha256':sha(log)}
    if process.returncode:
        return result
    paths=shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1])
    result.update({'object':str(obj),'object_sha256':sha(obj),
        'dependencies_sha256':{str(Path(p).resolve()):sha(Path(p).resolve()) for p in paths}})
    save(cache,result)
    return result


def command(record,name,argv,output,separate_stderr=False):
    start=time.monotonic()
    with output.open('w') as log:
        if separate_stderr:
            error_path=output.with_suffix('.stderr.log')
            with error_path.open('w') as error:
                result=subprocess.run(argv,cwd=ROOT,stdout=log,stderr=error)
        else:
            result=subprocess.run(argv,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    row={'name':name,'command':argv,'exit_code':result.returncode,'seconds':time.monotonic()-start,
         'log':str(output),'log_sha256':sha(output)}
    if separate_stderr:
        row.update({'stderr_log':str(error_path),'stderr_sha256':sha(error_path)})
    record['commands'].append(row)
    result.check_returncode()


def build(args,inputs,manifest,out):
    profiles=args.profile or ['avx2','avx512']
    record={'scope':__doc__,'complete':False,'source_manifest_sha256':MAP_SHA,
        'manifest_file_sha256':sha(inputs/'manifest.json'),'runner_sha256':sha(Path(__file__)),
        'profiles':profiles,'objects':{},'commands':[],'binaries':{},
        'compiler':subprocess.check_output(['clang++-21','--version'],text=True),
        'limits':['A distinct single-image source/namespace control; original process-isolated timings remain separate.',
                  'No LTO or production header change. No per-window barrier, timer or dispatch was added to the original loops.',
                  'Before duplicate is a separately linked code copy, not a byte-identical whole-ELF duplicate.']}
    tasks=[];by_profile={}
    for profile in profiles:
        march='x86-64-v3' if profile=='avx2' else 'znver5'
        profile_flags=FLAGS+['-march='+march]
        if os.uname().machine not in ('x86_64','amd64'):
            profile_flags+=['--target=x86_64-linux-gnu','--gcc-toolchain=/usr']
        by_profile[profile]=[]
        for arm in ARMS:
            tree=inputs/('before' if arm=='duplicate' else arm)
            prefix='panel_'+arm+'_'
            common=['clang++-21',*profile_flags,'-I'+str(tree/'ikea/include'),
                    '-ffile-prefix-map='+str(tree)+'=.',
                    '-Dikea='+prefix+'ikea','-Druntime='+prefix+'runtime','-Dextras='+prefix+'extras',
                    '-Dtuples_2='+prefix+'tuples_2']
            sources=[(str(Path('ikea/src/tuplepack')/(name+'.cpp')),False) for name in TP]
            sources += [(str(Path('workbench/spikes/filter-composition')/(name+'.cpp')),True) for name in BENCH]
            for name,is_bench in sources:
                source=tree/name
                obj=out/profile/arm/((('bench-' if is_bench else 'tuplepack-')+Path(name).stem)+'.o')
                dep=obj.with_suffix('.d');log=obj.with_suffix('.log')
                flags=['-fno-vectorize','-fno-slp-vectorize','-DFILTER_SEAMS_NO_MAIN=1','-DFILTER_EXTRAS_NO_MAIN=1'] if is_bench else []
                argv=[*common,*flags,'-MD','-MF',str(dep),'-c',str(source),'-o',str(obj)]
                tasks.append((argv,source,obj,log,dep));by_profile[profile].append(obj)
        source=HERE/'predicate-panel.cpp';obj=out/profile/'driver.o';dep=obj.with_suffix('.d')
        argv=['clang++-21',*profile_flags,'-MD','-MF',str(dep),'-c',str(source),'-o',str(obj)]
        tasks.append((argv,source,obj,obj.with_suffix('.log'),dep));by_profile[profile].append(obj)
    out.mkdir(parents=True,exist_ok=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending=[pool.submit(compile_object,t) for t in tasks]
        failed=[]
        for i,future in enumerate(as_completed(pending),1):
            row=future.result();record['objects'][row['source']+'|'+str(row['command'][-1])]=row
            save(out/'build-receipt.json',record)
            print(f'compiled {i}/{len(tasks)} '+Path(row['source']).name+(' (cache)' if row.get('reused') else ''),flush=True)
            if row['exit_code']:
                failed.append(row['log'])
    assert not failed,failed
    for profile,objects in by_profile.items():
        binary=out/profile/'predicate-panel'
        argv=['clang++-21','-O3','-g','-fno-fast-math','-Wl,--gc-sections','-Wl,-Map,'+str(binary)+'.map',
              *map(str,objects),'-o',str(binary)]
        if os.uname().machine not in ('x86_64','amd64'):
            argv[1:1]=['--target=x86_64-linux-gnu','--gcc-toolchain=/usr']
        command(record,'link-'+profile,argv,out/profile/'link.log')
        assembly=out/profile/'panel-disassembly.txt'
        command(record,'objdump-'+profile,['llvm-objdump-21','-d','-r','--demangle',str(binary)],assembly)
        record['binaries'][profile]={'sha256':sha(binary),'bytes':binary.stat().st_size,
                                    'disassembly_sha256':sha(assembly)}
        save(out/'build-receipt.json',record)
    record['complete']=True;save(out/'build-receipt.json',record)
    return record


def run(args):
    inputs=(ROOT/args.inputs).resolve();out=args.output.resolve();manifest=verify(inputs)
    assert args.jobs>0
    if args.dry_run:
        print(json.dumps({'profiles':args.profile or ['avx2','avx512'],'jobs':args.jobs,
            'objects_per_profile':37,'namespace_arms':list(ARMS),'mode':args.mode,
            'source_manifest_sha256':MAP_SHA,'manifest_file_sha256':sha(inputs/'manifest.json'),
            'checks_before_timing':True,'timing_iterations':300000,'timing_trials':5,
            'timed_cells_per_arm':48,'cpu':args.cpu},indent=2))
        return
    build(args,inputs,manifest,out)
    audit=['python3',str(HERE/'predicate-panel-audit.py'),'--inputs',str(inputs),'--output',str(out)]
    subprocess.run(audit,cwd=ROOT,check=True)
    if args.mode=='build':
        return
    assert os.uname().machine in ('x86_64','amd64') and args.jobs==8
    assert args.cpu in os.sched_getaffinity(0)
    record={'scope':__doc__,'complete':False,'build_receipt_sha256':sha(out/'build-receipt.json'),
            'identity_receipt_sha256':sha(out/'identity.json'),'commands':[],'cpu':args.cpu}
    for profile in args.profile or ['avx2','avx512']:
        binary=out/profile/'predicate-panel'
        command(record,'check-'+profile,['taskset','-c',str(args.cpu),str(binary),'--check'],out/profile/'check.log')
    for profile in args.profile or ['avx2','avx512']:
        binary=out/profile/'predicate-panel'
        command(record,'timing-'+profile,['taskset','-c',str(args.cpu),str(binary),'300000','5'],out/profile/'panel.csv',separate_stderr=True)
        save(out/'receipt.json',record)
    record['complete']=True;save(out/'receipt.json',record)
    try:
        subprocess.run(['python3',str(HERE/'predicate-panel-results.py'),'--results',str(out)],cwd=ROOT,check=True)
    except subprocess.CalledProcessError:
        record['complete']=False
        save(out/'receipt.json',record)
        raise


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='mode',required=True)
    p=sub.add_parser('prepare')
    p.add_argument('capture',type=Path);p.add_argument('--source-pair',type=Path,required=True)
    p.add_argument('--original-results',type=Path,required=True);p.add_argument('--metadata',type=Path,required=True)
    p=sub.add_parser('stage')
    p.add_argument('--source-pair',type=Path,required=True);p.add_argument('--original-results',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    for mode in ('build','run'):
        p=sub.add_parser(mode)
        p.add_argument('--inputs',type=Path,default=INPUTS)
        p.add_argument('--output',type=Path,default=Path(os.environ.get('SIXDB_RESULTS','build/ikea-cohesion-v2/predicate-panel')))
        p.add_argument('--jobs',type=int,default=int(os.environ.get('SIXDB_BUILD_JOBS','1')))
        p.add_argument('--cpu',type=int,default=int(os.environ.get('SIXDB_CPU','8')))
        p.add_argument('--profile',action='append',choices=('avx2','avx512'))
        p.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    if args.mode=='prepare':prepare(args)
    elif args.mode=='stage':stage(args)
    else:run(args)


if __name__=='__main__':
    main()
