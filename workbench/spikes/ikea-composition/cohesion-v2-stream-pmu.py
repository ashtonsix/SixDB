#!/usr/bin/env python3
"""Diagnose the frozen peripheral StreamPack consumers with their existing PMU hook.

The immutable R2 mode patches no benchmark or measured loop. Optional permuted
qualification applies an identical projection-only harness patch to both arms.
Reader kernels and compiler flags remain unchanged. The old 'shuffled' case is a
rotation at both retained population sizes.
"""
import argparse
import csv
import difflib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
STUDY = Path('workbench/spikes/ikea-composition')
SPEC = importlib.util.spec_from_file_location('cohesion_native', ROOT/STUDY/'cohesion-v2-native.py')
native = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(native)
ROOTS = [
    'workbench/spikes/streampack/implementation_bench.cpp',
    'workbench/spikes/streampack/prefix.h',
    'ikea/src/streampack/layout.cpp', 'ikea/src/streampack/view.cpp',
    'ikea/src/streampack/compact/description.cpp', 'ikea/src/streampack/compact/view.cpp',
    'ikea/src/intervals/search.cpp', 'workbench/tools/pmu/pmu.cpp',
]
KEY = ('family','maximum','rows','path','operation','repeat')
BENCHMARK = 'sixdb/workbench/spikes/streampack/streampack_implementation_bench'
PROBE = 'sixdb/workbench/tools/pmu/sixdb_pmu_probe'
LIMITS = [
    "Existing warm complete consumers; no cold-cache, random-access or DRAM claim.",
    "The R2 case named shuffled is (i*65537+17)&(rows-1): rotation by 17 at rows=256 and 65536.",
    "PMU includes calling-thread user-space execution between start/stop, including clock reads; counts are unscaled.",
    "Required cycles/instructions and optional branch/cache misses retain availability, errors and multiplexing; missing is not zero.",
    "Generic cache misses are not a qualified L1/LLC/DRAM residency event. No frontend/uop-cache event is added.",
    "Equal instructions can exclude some extra-work explanations but do not prove code alignment is causal.",
    "ASLR policy is recorded, not changed. Rebuilt binaries retain full linked disassembly and hashes.",
]


LEGACY_PROTOCOL = {'name':'R2-original','records_per_invocation':108,
    'projection_operations':['ordered','shuffled'],
    'indices':{'ordered':'i','shuffled':'(i*65537u+17)&(rows-1)'}}
PERMUTED_PROTOCOL = {'name':'ordered-rotated-permuted','records_per_invocation':132,
    'projection_operations':['ordered','rotated','permuted'],
    'indices':{'ordered':'i','rotated':'(i+17)&(rows-1)',
               'permuted':'(((i ^ (i >> 7))*0x9e3779b1u)+17)&(rows-1)'}}
BENCHMARK_SOURCE = 'workbench/spikes/streampack/implementation_bench.cpp'


def upgrade_projection(original,current):
    """Apply the identical private traversal change across the two public APIs."""
    start = original.index('template <class Read> [[gnu::noinline]] std::uint64_t projection(')
    end = original.index('volatile std::uint64_t observed;',start)
    new_start = current.index('enum class projection_order')
    new_end = current.index('volatile std::uint64_t observed;',new_start)
    patched = original[:start]+current[new_start:new_end]+original[end:]
    patched = patched.replace('projection(p.rows, rounds, false,',
                              'projection(p.rows, rounds, projection_order::ordered,')
    start = patched.index('    time(p, name, "shuffled",')
    end = patched.index('\n}\nvoid run(',start)
    new_start = current.index('    time(p, name, "rotated",')
    new_end = current.index('\n}\nvoid run(',new_start)
    patched = patched[:start]+current[new_start:new_end]+patched[end:]
    marker = '    assert(rows >= 256 && std::has_single_bit(rows));\n'
    assert patched.count(marker)==1
    return patched.replace(marker,marker+'    validate_projection_orders(rows);\n')


def affine_successor(start,steps,rows):
    # Independent modular composition of the fixture's known successor law.
    mask = rows-1
    a,b,m,c = 1664525 & mask,1013904223 & mask,1,0
    while steps:
        if steps & 1: m,c = (a*m)&mask,(a*c+b)&mask
        a,b = (a*a)&mask,(a*b+b)&mask
        steps >>= 1
    return (m*start+c)&mask


def expected_work(identity,protocol):
    family,maximum,rows,path,operation,repeat = identity
    n = int(rows)
    assert n in [256,65536] and int(maximum)==4095
    visits = 1 << 20
    if operation in ['chain1','chain8']:
        chains = 1 if operation=='chain1' else 8
        rounds = visits//chains
        checksum = sum(affine_successor(c*17,rounds,n) for c in range(chains))
        work = {'kind':'dependent_successor_chains','chains':chains,'rounds_per_chain':rounds,
                'successor':'(rank*1664525u+1013904223u)&(rows-1)',
                'checksum':'sum of terminal ranks','initial_ranks':[c*17 for c in range(chains)]}
    else:
        assert operation in [*protocol['projection_operations'],'block-shuffled']
        rounds = max(1,visits//n)
        visits = rounds*n
        # Odd affine successor is a bijection; every full row pass has this sum.
        checksum = rounds*n*(n-1)//2
        work = {'kind':'full_population_projection','rounds':rounds,'rows_per_round':n,
                'checksum':'sum of every row successor once per round',
                'index':protocol['indices'].get(operation,'base+(slot*137+17)%256 per block')}
    return visits,checksum,{**work,'values':visits,'expected_checksum':checksum}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def binary_layout(binary,sections,disassembly):
    section = re.search(r'^\s*\d+\s+\.text\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)',sections,re.M)
    assert section, 'missing linked .text section'
    text_size,text_start = (int(x,16) for x in section.groups())
    symbols = []
    for address,size,name in re.findall(r'^([0-9a-fA-F]+)\s+.*?\bF\s+\.text\s+([0-9a-fA-F]+)\s+(.*)$',sections,re.M):
        symbols.append({'name':name,'address':int(address,16),'size':int(size,16),
                        'start_mod64':int(address,16)%64})
    assert symbols
    intervals = sorted({(s['address'],s['address']+s['size']) for s in symbols if s['size']})
    cursor,gaps = text_start,[]
    for start,end in intervals:
        if start>cursor:gaps.append({'address':cursor,'size':start-cursor})
        cursor=max(cursor,end)
    if cursor<text_start+text_size:gaps.append({'address':cursor,'size':text_start+text_size-cursor})
    nop_bytes=nop_instructions=0
    in_text=False
    for line in disassembly.splitlines():
        if line.startswith('Disassembly of section '):
            in_text=line=='Disassembly of section .text:'
        if not in_text or '\t' not in line:continue
        fields=line.split('\t')
        if len(fields)<2 or ':' not in fields[0]:continue
        mnemonic=fields[1].strip().split()
        if not mnemonic or not mnemonic[-1].startswith('nop'):continue
        encoded=fields[0].split(':',1)[1].split()
        if not encoded or not all(re.fullmatch(r'[0-9a-fA-F]+',part) and len(part)%2==0 for part in encoded):continue
        nop_instructions+=1
        nop_bytes+=sum(len(part)//2 for part in encoded)
    hot=[s for s in symbols if 'projection<' in s['name'] or 'chase<' in s['name']]
    return {'binary_bytes':binary.stat().st_size,'text_bytes':text_size,'text_start':text_start,
        'text_nop_bytes':nop_bytes,'text_nop_instructions':nop_instructions,
        'unclaimed_text_bytes':sum(g['size'] for g in gaps),'unclaimed_text_ranges':gaps,
        'padding_limits':'Unclaimed ranges use sized function symbols; they can include unsized code. NOP totals include interior alignment, not only inter-function padding.',
        'function_start_mod64_histogram':{str(m):sum(s['start_mod64']==m for s in symbols)
                                        for m in sorted({s['start_mod64'] for s in symbols})},
        'hot_projection_chase_functions':hot}


def closure(base):
    """Project-include superset, intentionally including every conditional arm."""
    pending, seen = [Path(p) for p in ROOTS], set()
    include = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]',re.M)
    for_dynamic = re.compile(r'^\s*#\s*include\s+(?![<"])(\S+)',re.M)
    while pending:
        rel = pending.pop()
        if rel in seen: continue
        seen.add(rel)
        text = (base/rel).read_text()
        assert not for_dynamic.search(text), ('computed project include needs explicit review',str(rel))
        for delimiter,name in include.findall(text):
            candidates = ([rel.parent/name] if delimiter=='"' else [])
            candidates += [Path('ikea/include')/name,Path('workbench/tools')/name]
            for candidate in candidates:
                candidate = (base/candidate).resolve().relative_to(base.resolve())
                if (base/candidate).is_file():
                    pending.append(candidate)
                    break
    return {str(p):digest(base/p) for p in sorted(seen)}


def prepare(args):
    base = args.base_capture.resolve()
    frozen = base/'cohesion-inputs/after'
    hashes = closure(frozen)
    original_manifest = base/'cohesion-inputs/manifest.json'
    manifest = json.loads(original_manifest.read_text())
    protocol = LEGACY_PROTOCOL
    replacements = []
    if args.permuted:
        protocol = PERMUTED_PROTOCOL
        scratch = args.scratch.resolve()
        scratch.mkdir(parents=True,exist_ok=False)
        current = (ROOT/BENCHMARK_SOURCE).read_text()
        for arm in ['before','after']:
            original = (base/'cohesion-inputs'/arm/BENCHMARK_SOURCE).read_text()
            patched = upgrade_projection(original,current)
            if arm=='after': assert patched==current, 'unexpected maintained harness differences'
            path = scratch/(arm+'.cpp')
            path.write_text(patched)
            patch = ''.join(difflib.unified_diff(original.splitlines(True),patched.splitlines(True),
                fromfile=arm+'/'+BENCHMARK_SOURCE,tofile=arm+'-projection/'+BENCHMARK_SOURCE))
            patch_file = scratch/('stream-projection-'+arm+'.patch')
            patch_file.write_text(patch)
            manifest['source_sha256'][arm][BENCHMARK_SOURCE] = digest(path)
            manifest['harness_patches'].append({'arm':arm,'path':BENCHMARK_SOURCE,
                'original_sha256':native.sha(original.encode()),'patched_sha256':digest(path),
                'patch_sha256':digest(patch_file),
                'scope':'paired projection-only harness change: honest rotation, bijective permutation, cold property validation; no reader/kernel change'})
            replacements += [f'cohesion-inputs/{arm}/{BENCHMARK_SOURCE}={path}',
                             f'cohesion-inputs/{patch_file.name}={patch_file}']
        hashes[BENCHMARK_SOURCE] = digest(ROOT/BENCHMARK_SOURCE)
        assert set(closure(ROOT))==set(hashes), 'new project dependency needs explicit review'
        manifest['source_manifest_sha256'] = {arm:native.sha(json.dumps(files,sort_keys=True).encode())
                                             for arm,files in manifest['source_sha256'].items()}
        manifest['before_label'] += ' + paired Stream projection-only harness patch'
        manifest['stream_projection_protocol'] = protocol
        manifest['frozen_peripheral_manifest_sha256'] = digest(original_manifest)
        native.json_file(scratch/'manifest.json',manifest)
        replacements += [f'cohesion-inputs/manifest.json={scratch/"manifest.json"}',
                         f'{BENCHMARK_SOURCE}={ROOT/BENCHMARK_SOURCE}']
    differences = [name for name,wanted in hashes.items()
                   if not (ROOT/name).is_file() or digest(ROOT/name)!=wanted]
    kernel_differences = [name for name in differences if name!=BENCHMARK_SOURCE]
    assert not kernel_differences, ('current Stream kernel dependencies differ',kernel_differences)
    assert not differences or args.frozen_r2 and differences==[BENCHMARK_SOURCE], (
        'current Stream dependency closure differs beyond declared harness patch',differences)
    proof = {'method':'recursive literal project include superset; all conditional branches',
             'roots':ROOTS,'source_closure':hashes,'current_differences':differences,
             'current_kernel_differences':kernel_differences,'frozen_R2_timed_harness':args.frozen_r2,
             'excluded_changed_writer_headers':['ikea/include/ikea/seriespack/writer.h',
                'ikea/include/ikea/seriespack/erased_writer.h','ikea/include/ikea/tuplepack/detail/mutation.h'],
             'frozen_peripheral_manifest_sha256':digest(original_manifest),'protocol':protocol,
             'limits':'System headers and compiler are qualified by the native command/toolchain receipt, not this source-only closure.'}
    args.proof.parent.mkdir(parents=True,exist_ok=True)
    native.json_file(args.proof,proof)
    capture = native.tool_module('capture')
    result = capture.capture(base,args.capture.resolve(),replacements=[
        f'{STUDY}/cohesion-v2-stream-pmu.py={Path(__file__).resolve()}',
        f'{STUDY}/cohesion-v2-stream-pmu.sh={ROOT/STUDY/"cohesion-v2-stream-pmu.sh"}',
        f'cohesion-inputs/stream-current-equivalence.json={args.proof.resolve()}',*replacements])
    print(json.dumps({'capture':str(args.capture.resolve()),'source_digest':result['source_digest'],
                      'project_dependency_files':len(hashes),'protocol':protocol,'proof':str(args.proof)},indent=2))


def key(row):
    return tuple(str(row[x]) for x in KEY)


def read_trial(csv_path,pmu_path,protocol=LEGACY_PROTOCOL):
    rows = list(csv.DictReader(io.StringIO(csv_path.read_text())))
    measurements = [json.loads(line) for line in pmu_path.read_text().splitlines()]
    assert len(rows)==len(measurements)==protocol['records_per_invocation'], (str(csv_path),len(rows),len(measurements))
    measured = {key(row):row for row in measurements}
    assert len(measured)==protocol['records_per_invocation']
    result = {}
    for row in rows:
        identity = key(row)
        assert identity not in result
        pmu = measured[identity]
        values,checksum,work = expected_work(identity,protocol)
        assert int(pmu['values'])==values and int(row['checksum'])==checksum, (identity,values,checksum)
        counters = pmu['pmu']
        assert counters['scope']=='calling_thread_user_space' and counters['scaled'] is False
        complete = counters['status']=='complete' and counters['multiplexed'] is False
        counts = {event['name']:(event['count']/pmu['values']
                  if complete and event['status']=='measured' else None)
                  for event in counters['events']}
        result[identity] = {'checksum':int(row['checksum']),'values':pmu['values'],'work':work,
            'ns_per_value':float(row['ns_per_value']),'pmu_status':counters['status'],
            'coverage':counters['coverage'],'multiplexed':counters['multiplexed'],
            'per_value':counts}
    return result


def summarize(out,profiles,protocol=LEGACY_PROTOCOL):
    paired = []
    for profile in profiles:
        for rows in [256,65536]:
            trials = {arm:[read_trial(out/profile/arm/f'stream-{rows}-trial-{i}.csv',
                         out/profile/arm/f'stream-{rows}-trial-{i}.pmu.jsonl',protocol) for i in range(5)]
                      for arm in ['before','after']}
            identities = set(trials['before'][0])
            assert all(set(trial)==identities for arm in trials.values() for trial in arm)
            for identity in sorted(identities):
                records = {arm:[trial[identity] for trial in trials[arm]] for arm in trials}
                assert len({r['checksum'] for arm in records.values() for r in arm})==1
                assert len({r['values'] for arm in records.values() for r in arm})==1
                metrics = {}
                names = ['ns_per_value',*records['before'][0]['per_value']]
                for metric in names:
                    values = {arm:[r[metric] if metric=='ns_per_value' else r['per_value'][metric]
                                   for r in records[arm]] for arm in records}
                    valid = all(x is not None for arm in values.values() for x in arm)
                    metrics[metric] = {'trials':values,'medians':
                        {arm:statistics.median(values[arm]) for arm in values} if valid else None,
                        'paired_after_before':
                        [a/b if b else None for b,a in zip(values['before'],values['after'])] if valid else None}
                paired.append({'profile':profile,**dict(zip(KEY,identity)),'records':records,'metrics':metrics})
    native.json_file(out/'paired-pmu.json',{'limits':LIMITS,'protocol':protocol,'cases':paired})


def run(args):
    inputs = ROOT/'cohesion-inputs'
    manifest = json.loads((inputs/'manifest.json').read_text())
    proof = json.loads((inputs/'stream-current-equivalence.json').read_text())
    assert not proof.get('current_kernel_differences',proof['current_differences'])
    protocol = proof.get('protocol',LEGACY_PROTOCOL)
    assert digest(ROOT/STUDY/'cohesion-native/CMakeLists.txt')==manifest['shared_cmake_sha256']
    for arm,files in manifest['source_sha256'].items():
        for name,wanted in files.items():
            assert digest(inputs/arm/name)==wanted, (arm,name)
    cpu = int(os.environ['SIXDB_CPU'])
    assert cpu in os.sched_getaffinity(0)
    profiles = args.profile or (['neon'] if os.uname().machine=='aarch64' else ['avx2','avx512'])
    out = Path(os.environ['SIXDB_RESULTS']).resolve()
    out.mkdir(parents=True,exist_ok=True)
    receipt = {'scope':__doc__,'limits':LIMITS,'profiles':profiles,'cpu':cpu,'trials':5,
       'source_manifest_sha256':manifest['source_manifest_sha256'],
       'source_manifest_file_sha256':digest(inputs/'manifest.json'),
       'runner_sha256':digest(Path(__file__)),'commands':[],'binaries':[],
       'function_alignment':args.function_alignment,
       'alignment_scope':'Matched function-start alignment control only; no production/default tuning change' if args.function_alignment else None,
       'projection_protocol':protocol,
       'historical_R2_shuffle_semantics':{'rows':[256,65536],'index':'(i*65537u+17)&(rows-1)','actual':'rotation_by_17'},
       'maximum':4095,'expected_values_per_measurement':1048576,
       'aslr':Path('/proc/sys/kernel/randomize_va_space').read_text().strip()}
    native.ACTIVE_RECEIPT,native.RECEIPT_PATH = receipt,out/'receipt.json'
    native.json_file(out/'receipt.json',receipt)
    shutil.copy2(inputs/'manifest.json',out/'source-manifest.json')
    shutil.copy2(inputs/'stream-current-equivalence.json',out/'current-equivalence.json')
    (out/'cpuinfo.txt').write_text(Path('/proc/cpuinfo').read_text())
    (out/'compiler.txt').write_text(subprocess.check_output(['clang++-21','--version'],text=True))
    (out/'lscpu.txt').write_text(subprocess.check_output(['lscpu'],text=True))
    for profile in profiles:
        march,tune,_ = native.PROFILES[profile]
        binaries = {}
        for arm in ['before','after']:
            source = inputs/arm
            build = ROOT/'build/ikea-cohesion-v2-stream-pmu'/profile
            if args.function_alignment:build /= 'align'+str(args.function_alignment)
            build /= arm
            logs = out/profile/arm
            def command(argv,log,cwd=ROOT):
                native.run_command(argv,cwd=cwd,log=log,receipt=receipt['commands'])
            command(['cmake','-S',str(ROOT/STUDY/'cohesion-native'),'-B',str(build),'-G','Ninja',
               '-DCOHESION_SOURCE_ROOT='+str(source),'-DCOHESION_BEFORE='+str(int(arm=='before')),
               '-DCMAKE_BUILD_TYPE=Release','-DSIXDB_BENCHMARKS=seriespack;tuplepack;bec256;hashtable',
               '-DSIXDB_SPIKES=streampack','-DIKEA_COMPARISONS=OFF',
               '-DSIXDB_MARCH='+march,'-DSIXDB_TUNE='+tune,
               *(['-DCMAKE_CXX_FLAGS=-falign-functions='+str(args.function_alignment)]
                 if args.function_alignment else [])],logs/'configure.log')
            command(['cmake','--build',str(build),'--target','streampack_implementation_bench',
                     'sixdb_pmu_probe','-j',str(args.jobs)],logs/'build.log')
            shutil.copy2(build/'compile_commands.json',logs/'compile_commands.json')
            shutil.copy2(build/'CMakeCache.txt',logs/'CMakeCache.txt')
            binary = logs/'streampack_implementation_bench'
            shutil.copy2(build/BENCHMARK,binary)
            binaries[arm] = binary
            receipt['binaries'].append({'profile':profile,'arm':arm,'sha256':digest(binary)})
            command(['llvm-objdump-21','-d','-r','--demangle',str(binary)],logs/'stream-disassembly.txt')
            command(['llvm-objdump-21','--section-headers','--syms','--demangle',str(binary)],
                    logs/'stream-sections-symbols.txt')
            layout = binary_layout(binary,(logs/'stream-sections-symbols.txt').read_text(),
                                   (logs/'stream-disassembly.txt').read_text())
            native.json_file(logs/'stream-layout.json',layout)
            receipt['binaries'][-1]['layout'] = {k:v for k,v in layout.items()
                if k not in ['unclaimed_text_ranges','hot_projection_chase_functions']}
            if args.function_alignment:
                hot=layout['hot_projection_chase_functions']
                assert hot and all(s['start_mod64']==0 for s in hot), 'hot function alignment control failed'
                commands=json.loads((build/'compile_commands.json').read_text())
                assert all('-falign-functions='+str(args.function_alignment) in c['command']
                    for c in commands if c['file'].endswith(BENCHMARK_SOURCE))

            if arm=='after':
                command(['python3',str(source/'workbench/tools/pmu/probe.py'),str(build/PROBE),
                    '--cpu',str(cpu),'--output',str(out/profile/'pmu-availability'),
                    '--event','branch_misses:0:5','--event','cache_misses:0:3'],logs/'pmu-probe.log')
        # All builds/probes finish before the paired measured trials.
        for trial in range(5):
            for arm in (['before','after'] if trial%2==0 else ['after','before']):
                for rows in [256,65536]:
                    logs = out/profile/arm
                    csv_path = logs/f'stream-{rows}-trial-{trial}.csv'
                    pmu_path = logs/f'stream-{rows}-trial-{trial}.pmu.jsonl'
                    assert not pmu_path.exists(), 'PMU appends: use a fresh results directory'
                    native.run_command(['env','STREAMPACK_PMU='+str(pmu_path),'taskset','-c',str(cpu),
                        str(binaries[arm]),str(rows),'4095'],cwd=inputs/arm,log=csv_path,
                        stderr_log=logs/f'stream-{rows}-trial-{trial}.stderr',receipt=receipt['commands'])
                    read_trial(csv_path,pmu_path,protocol)
            native.json_file(out/'progress.json',{'profile':profile,'completed_trial':trial+1,'trials':5})
    summarize(out,profiles,protocol)
    receipt['measurement_sha256'] = {str(p.relative_to(out)):digest(p)
        for p in out.rglob('*') if p.suffix in ['.csv','.jsonl']}
    native.json_file(out/'receipt.json',receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode',required=True)
    p = sub.add_parser('prepare')
    p.add_argument('capture',type=Path)
    p.add_argument('--base-capture',type=Path,default=Path('build/captures/ikea-cohesion-v2-peripheral'))
    p.add_argument('--proof',type=Path,default=Path('build/ikea-cohesion-v2/stream-pmu-dependencies.json'))
    mode=p.add_mutually_exclusive_group()
    mode.add_argument('--permuted',action='store_true',help='paired corrected projection harness; immutable R2 capture stays separate')
    mode.add_argument('--frozen-r2',action='store_true',help='retain original timed caller; separately prove current kernel equivalence')
    p.add_argument('--scratch',type=Path,default=Path('build/ikea-cohesion-v2/stream-permuted-inputs'))
    p = sub.add_parser('run')
    p.add_argument('--jobs',type=int,default=int(os.environ.get('SIXDB_BUILD_JOBS','2')))
    p.add_argument('--profile',action='append',choices=list(native.PROFILES))
    p.add_argument('--function-alignment',type=int,choices=[64],default=None,help='isolated matched alignment control; default flags unchanged')
    args = parser.parse_args()
    if args.mode=='prepare': prepare(args)
    else: run(args)


if __name__=='__main__': main()
