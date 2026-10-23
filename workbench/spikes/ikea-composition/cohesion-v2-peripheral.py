#!/usr/bin/env python3
"""Paired maintained Bec256/Hashtable/StreamPack consumers, including owner writes.

Reuse the primary capture/command machinery. No kernel, timed loop or native body
is substituted. The added Bec fixture oracle runs outside its measured loop.
"""
import argparse
import difflib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

SPEC = importlib.util.spec_from_file_location('cohesion_native', Path(__file__).with_name('cohesion-v2-native.py'))
native = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(native)
ROOT = native.ROOT
TARGETS = {
    'bec256': 'sixdb/workbench/benchmarks/bec256/ikea_bec256_bench',
    'htable8': 'sixdb/workbench/benchmarks/hashtable/ikea_hashtable_operations_k14_b10_v8',
    'htable64': 'sixdb/workbench/benchmarks/hashtable/ikea_hashtable_operations_k30_b10_v64',
    'stream': 'sixdb/workbench/spikes/streampack/streampack_implementation_bench',
}
BEC_FILTER = r'^codec/(random|runs|terminal)/(read_exact|read_padded|encode_checked_exact|prepare_write|native_encode_checked_exact)$'


def amend(before, after, scratch, manifest):
    # Identify the Ikea implementation so the primary cohort can be matched to it.
    expected = {k:v for k,v in manifest['source_sha256']['after'].items() if k.startswith('ikea/')}
    name = 'workbench/benchmarks/bec256/fixture.h'
    oracle = '''            // Independent plain-value oracle, outside every timed loop.
            std::array<bc::byte, 32> roundtrip{};
            bc::decode(*a, roundtrip);
            assert(roundtrip == plain[i]);
'''
    replacements = []
    for arm, source in [('before',before),('after',after)]:
        target = source / name
        original = target.read_text()
        marker = '            assert(a && b);\n'
        assert original.count(marker) == 1
        patched = original.replace(marker, marker + oracle)
        target.write_text(patched)
        patch = ''.join(difflib.unified_diff(original.splitlines(True),patched.splitlines(True),fromfile=arm+'/'+name,tofile=arm+'-oracle/'+name))
        output = scratch / ('bec-fixture-'+arm+'.patch')
        output.write_text(patch)
        manifest['harness_patches'].append({'arm':arm,'path':name,'original_sha256':native.sha(original.encode()),'patched_sha256':native.sha(patched.encode()),'patch_sha256':native.sha(patch.encode()),'scope':'same independent pre-timing decode/plain equality oracle; no timed-loop or Ikea change'})
        manifest['source_sha256'][arm][name] = native.sha(patched.encode())
        replacements.append(f'{native.INPUTS}/{output.name}={output}')
    # The after StreamPack harness replaced its projection-order loop. Give the
    # before arm the identical timed projection code so only Ikea differs.
    name = 'workbench/spikes/streampack/implementation_bench.cpp'
    target = before / name
    original = target.read_text()
    current = (after / name).read_text()
    start = current.index('enum class projection_order')
    end = current.index('volatile std::uint64_t observed;')
    patched = original
    old_projection = patched[patched.index('template <class Read> [[gnu::noinline]] std::uint64_t projection('):
                             patched.index('volatile std::uint64_t observed;')]
    patched = patched.replace(old_projection, current[start:end])
    for a, b in [('projection(p.rows, rounds, false, ', 'projection(p.rows, rounds, projection_order::ordered, '),
                 ('''    time(p, name, "shuffled", rep, std::uint64_t(rounds) * p.rows, projection_sum,
        [&] { return projection(p.rows, rounds, true, read); });''',
                  '''    time(p, name, "rotated", rep, std::uint64_t(rounds) * p.rows, projection_sum,
        [&] { return projection(p.rows, rounds, projection_order::rotated, read); });
    time(p, name, "permuted", rep, std::uint64_t(rounds) * p.rows, projection_sum,
        [&] { return projection(p.rows, rounds, projection_order::permuted, read); });''')]:
        assert a in patched, a
        patched = patched.replace(a, b)
    target.write_text(patched)
    patch = ''.join(difflib.unified_diff(original.splitlines(True),patched.splitlines(True),fromfile='before/'+name,tofile='before-harness/'+name))
    output = scratch / 'stream-harness-before.patch'
    output.write_text(patch)
    manifest['harness_patches'].append({'arm':'before','path':name,'original_sha256':native.sha(original.encode()),'patched_sha256':native.sha(patched.encode()),'patch_sha256':native.sha(patch.encode()),'scope':'after-arm projection-order timed loop transplanted; Ikea calls unchanged'})
    manifest['source_sha256']['before'][name] = native.sha(patched.encode())
    replacements.append(f'{native.INPUTS}/{output.name}={output}')
    manifest['before_label'] += ' + Bec fixture oracle-only patch + StreamPack projection harness'
    manifest['primary_ikea_sha256'] = native.sha(json.dumps(expected,sort_keys=True).encode())
    manifest['scope'] = __doc__
    return replacements


def run(args):
    inputs = (ROOT / native.INPUTS).resolve()
    out = Path(os.environ['SIXDB_RESULTS']).resolve()
    manifest = json.loads((inputs/'manifest.json').read_text())
    for arm, files in manifest['source_sha256'].items():
        for name,wanted in files.items():
            assert native.sha((inputs/arm/name).read_bytes()) == wanted, (arm,name)
    cpu = int(os.environ['SIXDB_CPU'])
    profiles = ['neon'] if os.uname().machine == 'aarch64' else ['avx2','avx512']
    receipt = {'baseline_commit':native.BASELINE,'source_manifest_sha256':manifest['source_manifest_sha256'],
               'before_label':manifest['before_label'],'primary_ikea_sha256':manifest['primary_ikea_sha256'],
               'trials':5,'profiles':profiles,'cpu':cpu,'commands':[],'binaries':[],
               'runner_sha256':native.sha(Path(__file__).read_bytes()),
               'limits':'Warm complete consumers; no cold/DRAM claim. HT construction includes allocation. Per-phase HT rows retain round order; no pooling across phases. No compilation overlaps timing.'}
    native.ACTIVE_RECEIPT, native.RECEIPT_PATH = receipt, out/'receipt.json'
    native.json_file(out/'receipt.json',receipt)
    shutil.copy2(inputs/'manifest.json',out/'source-manifest.json')
    (out/'cpuinfo.txt').write_text(Path('/proc/cpuinfo').read_text())
    (out/'compiler.txt').write_text(subprocess.check_output(['clang++-21','--version'],text=True))
    def command(argv,cwd,log,pad=None):
        native.run_command(argv,cwd=cwd,log=log,receipt=receipt['commands'],pad=pad)
    for profile in profiles:
        march,tune,_ = native.PROFILES[profile]
        binaries = {}
        for arm in ['before','after']:
            source = inputs/arm
            build = ROOT/'build/ikea-cohesion-v2-peripheral'/profile/arm
            logs = out/profile/arm
            logs.mkdir(parents=True,exist_ok=True)
            command(['cmake','-S',str(ROOT/native.STUDY/'cohesion-native'),'-B',str(build),'-G','Ninja',
                     '-DCOHESION_SOURCE_ROOT='+str(source),'-DCOHESION_BEFORE='+str(int(arm=='before')),
                     '-DCMAKE_BUILD_TYPE=Release','-DSIXDB_BENCHMARKS=seriespack;tuplepack;bec256;hashtable',
                     '-DSIXDB_SPIKES=streampack','-DIKEA_COMPARISONS=OFF',
                     '-DSIXDB_MARCH='+march,'-DSIXDB_TUNE='+tune,
                     '-DCMAKE_CXX_FLAGS='+os.environ.get('COHESION_CXXFLAGS','')],ROOT,logs/'configure.log')
            command(['cmake','--build',str(build),'--target',*[Path(p).name for p in TARGETS.values()],'-j',str(args.jobs)],ROOT,logs/'build.log')
            shutil.copy2(build/'compile_commands.json',logs/'compile_commands.json')
            shutil.copy2(build/'CMakeCache.txt',logs/'CMakeCache.txt')
            binaries[arm] = {}
            for family,relative in TARGETS.items():
                original = build/relative
                binary = logs/original.name
                shutil.copy2(original,binary)
                binaries[arm][family] = binary
                receipt['binaries'].append({'profile':profile,'arm':arm,'family':family,'sha256':native.sha(binary.read_bytes())})
                command(['llvm-objdump-21','-d','-r','--demangle',str(binary)],ROOT,logs/(family+'-disassembly.txt'))
            command([str(binaries[arm]['bec256']),'--benchmark_list_tests=true','--benchmark_filter='+BEC_FILTER],source,logs/'bec-cases.txt')
            assert (logs/'bec-cases.txt').read_text().strip()
        for trial in range(5):
            order = ['before','after'] if trial%2==0 else ['after','before']
            for arm in order:
                source,logs = inputs/arm,out/profile/arm
                command(['taskset','-c',str(cpu),str(binaries[arm]['bec256']),'--benchmark_filter='+BEC_FILTER,
                         '--benchmark_min_time=0.1s','--benchmark_repetitions=1','--benchmark_out_format=json',
                         '--benchmark_out='+str(logs/f'bec-trial-{trial}.json')],source,logs/f'bec-trial-{trial}.log',native.STACK_PADS[trial])
                for family in ['htable8','htable64']:
                    for operation in ['read','write','modify','grow']:
                        command(['taskset','-c',str(cpu),str(binaries[arm][family]),'--operation',operation,
                                 '--owner','ikea','--requests','1048576','--occupancy-percent','85',
                                 '--new-percent','50','--seed','713','--rep',str(trial)],source,
                                logs/f'{family}-{operation}-trial-{trial}.csv',native.STACK_PADS[trial])
                for rows in [256,65536]:
                    command(['taskset','-c',str(cpu),str(binaries[arm]['stream']),str(rows),'4095'],source,
                            logs/f'stream-{rows}-trial-{trial}.csv',native.STACK_PADS[trial])
            native.json_file(out/'progress.json',{'profile':profile,'completed_trial':trial+1,'trials':5})
    native.json_file(out/'receipt.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='mode',required=True)
    prepare=sub.add_parser('prepare')
    prepare.add_argument('capture',type=Path)
    prepare.add_argument('--scratch',type=Path,required=True)
    execute=sub.add_parser('run')
    execute.add_argument('--jobs',type=int,default=2)
    args=p.parse_args()
    if args.mode=='prepare':native.prepare(args,amend)
    else:run(args)

if __name__=='__main__':main()
