#!/usr/bin/env python3
"""Prepare frozen paired sources, or build/run complete native consumers.

No synthetic kernel or scalar point timing loop is introduced. The actual
SeriesPack/TuplePack maintained suites and filter-composition_extra own inputs,
independent validation, useful work, operation grain and result consumption.
"""
import argparse
import difflib
import re
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
STUDY = Path('workbench/spikes/ikea-composition')
INPUTS = Path('cohesion-inputs')
BASELINE = 'f153420002520213e1496a97559f0118bbd1ad94'
ACTIVE_RECEIPT = None
RECEIPT_PATH = None
PROFILES = {
    'neon': ('armv8-a+simd', 'neoverse-v2', ''),
    'avx2': ('x86-64-v3', 'zen5', ''),
    'avx512': ('znver5', 'zen5', ''),
}
TARGETS = {
    'seriespack': 'sixdb/workbench/benchmarks/seriespack/ikea_seriespack_bench',
    'tuplepack': 'sixdb/workbench/benchmarks/tuplepack/ikea_tuplepack_bench',
    'filters': 'sixdb/workbench/spikes/filter-composition/filter_composition_extra',
    'initialize': 'ikea_cohesion_initialize',
}
FILTERS = {
    'seriespack': r'^(ordinary/|placed/k(20|31|63)h.*(/sum-native|/sum-materialized|/mutation-bound|/mutation-materialized)$)',
    'tuplepack': r'^(composition/128/|point/(1|2|4)/(read|write)/(control|body|ordinary|control_effects)$)',
    'initialize': r'^initialize<',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def json_file(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def tool_module(name):
    p = ROOT / 'workbench/tools' / (name + '.py')
    sys.path.insert(0, str(p.parent))
    spec = importlib.util.spec_from_file_location('cohesion_' + name, p)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def backport_point_oracle(before, after, scratch):
    name = 'workbench/benchmarks/tuplepack/main.cpp'
    original = (before / name).read_text()
    current = (after / name).read_text()
    start = current.index('    // Derive decoded slots and replacement bytes directly from the schema.')
    end = current.index('    for (auto _ : state) {', start)
    block = current[start:end]
    # Preserve the baseline APIs: this patch adds only the same independent
    # pre-timing oracle/reset. Never migrate a baseline lowering or timed loop.
    block = re.sub(r'reinterpret_cast<const std::byte\*>\(\s*(bytes\.data\(\) \+ checked_row \* 16)\)', r'\1', block)
    block = re.sub(r'reinterpret_cast<std::byte\*>\(\s*(bytes\.data\(\) \+ checked_row \* 16)\)', r'\1', block)
    block = block.replace('effects.before(v, 0, checked_row * 16, N == 1 ? 1u : 2u);',
                          'effects.before(v, ikea::byte_write{0, checked_row * 16, N == 1 ? 1u : 2u});')
    for field in ['plane','offset','size']:
        block = block.replace('event.' + field, 'event.bytes.' + field)
    assert 'std::byte' not in block and 'event.plane' not in block
    point = original.index('template <unsigned N> void point(')
    insertion = original.index('    for (auto _ : state) {', point)
    patched = original[:insertion] + block + original[insertion:]
    (before / name).write_text(patched)
    patch = ''.join(difflib.unified_diff(original.splitlines(True), patched.splitlines(True),
                                       fromfile='f1534200/' + name, tofile='baseline-plus-oracle/' + name))
    patch_path = scratch / 'tuplepack-point-oracle.patch'
    patch_path.write_text(patch)
    return {'path':name, 'original_sha256':sha(original.encode()), 'patched_sha256':sha(patched.encode()),
            'patch_sha256':sha(patch.encode()), 'current_oracle_sha256':sha(current[start:end].encode()),
            'scope':'same independent point oracle/reset only; baseline APIs and timed loop retained'}, patch_path


def prepare(args, amend=None):
    """One existing source-capture receipt transports both immutable treatments."""
    capture = tool_module('capture')
    experiment = tool_module('experiment')
    scratch = args.scratch.resolve()
    assert not scratch.exists(), 'choose a fresh scratch directory'
    before, after = scratch / 'before', scratch / 'after'
    before.mkdir(parents=True)
    after.mkdir()
    rows = subprocess.check_output(['git', 'ls-tree', '-rz', BASELINE], cwd=ROOT).split(b'\0')
    entries = []
    for row in filter(None, rows):
        prefix, path = row.split(b'\t', 1)
        mode, kind, object_id = prefix.decode().split()
        name = path.decode()
        if experiment.source_output(name):
            continue
        if kind != 'blob' or mode not in ('100644', '100755'):
            raise ValueError(f'unsupported baseline source: {mode} {kind} {name}')
        entries.append((name, mode, object_id))
    data = subprocess.run(['git', 'cat-file', '--batch'], cwd=ROOT,
                          input=''.join(oid + '\n' for _,_,oid in entries).encode(),
                          stdout=subprocess.PIPE, check=True).stdout
    offset, hashes = 0, {'before': {}, 'after': {}}
    for name, mode, oid in entries:
        end = data.index(b'\n', offset)
        actual_oid, kind, length = data[offset:end].decode().split()
        assert actual_oid == oid and kind == 'blob'
        offset = end + 1
        body = data[offset:offset + int(length)]
        offset += int(length) + 1
        target = before / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
        target.chmod(0o755 if mode == '100755' else 0o644)
        hashes['before'][name] = sha(body)
    assert offset == len(data)
    after_commit = os.environ.get('COHESION_AFTER_COMMIT')
    if after_commit:
        # A committed after arm, for a like-for-like control of an earlier cut.
        # Study tooling still comes from the working tree so both cuts share it.
        rows = subprocess.check_output(['git', 'ls-tree', '-rz', after_commit], cwd=ROOT).split(b'\0')
        listed = []
        for row in filter(None, rows):
            prefix, path = row.split(b'\t', 1)
            mode, kind, object_id = prefix.decode().split()
            name = path.decode()
            if kind == 'blob' and not experiment.source_output(name):
                listed.append((name, object_id))
        blob = subprocess.run(['git', 'cat-file', '--batch'], cwd=ROOT,
                              input=''.join(o + '\n' for _, o in listed).encode(),
                              stdout=subprocess.PIPE, check=True).stdout
        current = experiment.source_files(ROOT)
        at = 0
        for name, object_id in listed:
            end = blob.index(b'\n', at)
            length = int(blob[at:end].split()[2])
            body = blob[end + 1:end + 1 + length]
            at = end + 2 + length
            if name.startswith(str(STUDY) + '/') and name in current:
                body = current[name]
            target = after / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
            hashes['after'][name] = sha(body)
    else:
        for name, body in experiment.source_files(ROOT).items():
            target = after / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
            target.chmod(0o755 if (ROOT / name).stat().st_mode & 0o111 else 0o644)
            hashes['after'][name] = sha(body)
    harness_patch, patch_path = backport_point_oracle(before, after, scratch)
    hashes['before'][harness_patch['path']] = harness_patch['patched_sha256']
    manifest = {'baseline_commit': BASELINE,
                'before_label': BASELINE + ' + TP point oracle-only harness patch',
                'harness_patches': [harness_patch],
                'shared_initialization_probe_sha256': sha((ROOT / STUDY / 'cohesion-initialize.cpp').read_bytes()),
                'shared_cmake_sha256': sha((ROOT / STUDY / 'cohesion-native/CMakeLists.txt').read_bytes()),
                'after_source_head': subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                'source_sha256': hashes,
                'source_manifest_sha256': {arm: sha(json.dumps(value,sort_keys=True).encode()) for arm,value in hashes.items()},
                'scope': __doc__}
    extra_replacements = amend(before, after, scratch, manifest) if amend else []
    manifest['source_manifest_sha256'] = {arm: sha(json.dumps(value,sort_keys=True).encode())
                                         for arm,value in manifest['source_sha256'].items()}
    json_file(scratch / 'manifest.json', manifest)
    result = capture.capture(ROOT, args.capture.resolve(), replacements=[
        f'{INPUTS}/before={before}', f'{INPUTS}/after={after}',
        f'{INPUTS}/manifest.json={scratch / "manifest.json"}',
        f'{INPUTS}/tuplepack-point-oracle.patch={patch_path}', *extra_replacements])
    print(json.dumps({'capture':str(args.capture.resolve()), 'capture_source_digest':result['source_digest'],
                      'baseline_commit':BASELINE, 'source_manifest_sha256':manifest['source_manifest_sha256']},indent=2))


# Environment bytes shift the initial stack, and with it every stack-resident
# fixture. Each trial uses a different shift (the same for both arms), so a
# median over trials does not inherit one arbitrary stack alignment.
STACK_PADS = (0, 528, 1104, 2064, 3088, 3600, 272, 1552)


def run_command(cmd, *, cwd, log, receipt, stderr_log=None, pad=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    env = None
    if pad is not None:
        env = dict(os.environ, COHESION_STACK_PAD='x' * pad)
    with log.open('w') as out:
        if stderr_log is None:
            process = subprocess.run(cmd, cwd=cwd, stdout=out, stderr=subprocess.STDOUT, env=env)
        else:
            with stderr_log.open('w') as err:
                process = subprocess.run(cmd, cwd=cwd, stdout=out, stderr=err, env=env)
    receipt.append({'command':list(map(str,cmd)), 'cwd':str(cwd), 'log':str(log),
                    'wall_seconds':time.monotonic()-start, 'exit_code':process.returncode,
                    'stderr_log':str(stderr_log) if stderr_log else None, 'stack_pad':pad})
    if ACTIVE_RECEIPT is not None:
        json_file(RECEIPT_PATH, ACTIVE_RECEIPT)
    if process.returncode:
        raise RuntimeError(f'command failed ({process.returncode}); see {log}')


def run(args):
    global ACTIVE_RECEIPT, RECEIPT_PATH
    inputs = (ROOT / args.inputs).resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((inputs / 'manifest.json').read_text())
    assert manifest['baseline_commit'] == BASELINE
    assert sha((ROOT / STUDY / 'cohesion-initialize.cpp').read_bytes()) == manifest['shared_initialization_probe_sha256']
    assert sha((ROOT / STUDY / 'cohesion-native/CMakeLists.txt').read_bytes()) == manifest['shared_cmake_sha256']
    # Verify the actual files, not only the transported manifest's identity.
    for arm, files in manifest['source_sha256'].items():
        for name, wanted in files.items():
            actual = sha((inputs / arm / name).read_bytes())
            if actual != wanted:
                raise RuntimeError(f'frozen source changed: {arm}/{name}')
    cpu = args.cpu
    assert cpu in os.sched_getaffinity(0), (cpu, sorted(os.sched_getaffinity(0)))
    profiles = args.profile or (['neon'] if os.uname().machine == 'aarch64' else ['avx2','avx512'])
    commands, binaries = [], []
    receipt = {'baseline_commit':BASELINE, 'source_manifest_sha256':manifest['source_manifest_sha256'],
               'runner_sha256':sha(Path(__file__).read_bytes()), 'profiles':profiles,
               'trials':args.trials, 'minimum_time_seconds':args.min_time, 'filter_iterations':args.filter_iterations,
               'before_label':manifest['before_label'], 'harness_patches':manifest['harness_patches'],
               'shared_initialization_probe_sha256':manifest['shared_initialization_probe_sha256'],
               'shared_cmake_sha256':manifest['shared_cmake_sha256'],
               'extra_cxxflags':os.environ.get('COHESION_CXXFLAGS', ''),
               'cpu':cpu, 'allowed_cpus':sorted(os.sched_getaffinity(0)), 'commands':commands,
               'google_filters':FILTERS, 'binaries':binaries,
               'limits':'Warm complete consumers, not cold-memory/publication behavior. Actual before/after callers and contracts differ where recorded; native results include linked code placement. No compiler runs concurrently with a timed trial.'}
    ACTIVE_RECEIPT, RECEIPT_PATH = receipt, out / 'receipt.json'
    json_file(RECEIPT_PATH,receipt)
    shutil.copy2(inputs / 'manifest.json',out / 'source-manifest.json')
    (out / 'cpuinfo.txt').write_text(Path('/proc/cpuinfo').read_text())
    (out / 'compiler.txt').write_text(subprocess.check_output(['clang++-21','--version'],text=True))
    (out / 'lscpu.txt').write_text(subprocess.check_output(['lscpu'],text=True))
    for profile in profiles:
        march,tune,extra = PROFILES[profile]
        paths = {}
        # Finish all construction/validation/codegen work before the first timed arm.
        for arm in ['before','after']:
            source = inputs / arm
            build = ROOT / 'build/ikea-cohesion-v2-native' / profile / arm
            logs = out / profile / arm
            logs.mkdir(parents=True, exist_ok=True)
            # Optional placement control applied identically to both arms.
            cxxflags = ' '.join(filter(None, [extra, os.environ.get('COHESION_CXXFLAGS', '')]))
            flags = ['-DCMAKE_BUILD_TYPE=Release','-DSIXDB_DEBUG_INFO=lines',
                     '-DSIXDB_BENCHMARKS=seriespack;tuplepack','-DSIXDB_SPIKES=filter-composition',
                     '-DIKEA_COMPARISONS=OFF',f'-DSIXDB_MARCH={march}',f'-DSIXDB_TUNE={tune}',
                     f'-DCMAKE_CXX_FLAGS={cxxflags}']
            run_command(['cmake','-S',str(ROOT / STUDY / 'cohesion-native'),'-B',str(build),'-G','Ninja',
                         '-DCOHESION_SOURCE_ROOT='+str(source), '-DCOHESION_BEFORE='+str(int(arm=='before')),*flags],cwd=ROOT,
                        log=logs/'configure.log',receipt=commands)
            run_command(['cmake','--build',str(build),'--target',
                         *[Path(p).name for p in TARGETS.values()],'-j',str(args.jobs)],cwd=ROOT,
                        log=logs/'build.log',receipt=commands)
            shutil.copy2(build/'compile_commands.json',logs/'compile_commands.json')
            shutil.copy2(build/'CMakeCache.txt',logs/'CMakeCache.txt')
            paths[arm] = {}
            for family,relative in TARGETS.items():
                binary = build / relative
                destination = logs / binary.name
                shutil.copy2(binary,destination)
                paths[arm][family] = destination
                binaries.append({'profile':profile,'arm':arm,'family':family,
                                 'sha256':sha(destination.read_bytes()),'bytes':destination.stat().st_size})
                run_command(['llvm-size-21','-A',str(destination)],cwd=ROOT,
                            log=logs/(family+'-size.txt'),receipt=commands)
                run_command(['llvm-objdump-21','-d','-r','--demangle',str(destination)],cwd=ROOT,
                            log=logs/(family+'-disassembly.txt'),receipt=commands)
            run_command(['taskset','-c',str(cpu),str(paths[arm]['filters']),'--check'],cwd=source,
                        log=logs/'filter-check.txt',receipt=commands)
            # A timed consumer runs its own independent expected-result validation
            # before entering its measured loop. Listing proves selected names exist.
            for family in ['seriespack','tuplepack','initialize']:
                run_command([str(paths[arm][family]),'--benchmark_list_tests=true',
                             '--benchmark_filter='+FILTERS[family]],cwd=source,
                            log=logs/(family+'-cases.txt'),receipt=commands)
                if not (logs/(family+'-cases.txt')).read_text().strip():
                    raise RuntimeError(f'no cases selected: {profile}/{arm}/{family}')
        for trial in range(args.trials):
            order = ['before','after'] if trial % 2 == 0 else ['after','before']
            for arm in order:
                logs = out / profile / arm
                for family in ['seriespack','tuplepack','initialize']:
                    result = logs / f'{family}-trial-{trial}.json'
                    run_command(['taskset','-c',str(cpu),str(paths[arm][family]),
                                 '--benchmark_filter='+FILTERS[family],
                                 f'--benchmark_min_time={args.min_time}s','--benchmark_repetitions=1',
                                 '--benchmark_enable_random_interleaving=false','--benchmark_out_format=json',
                                 '--benchmark_out='+str(result)],cwd=inputs/arm,
                                log=logs/f'{family}-trial-{trial}.log',receipt=commands,
                                pad=STACK_PADS[trial % len(STACK_PADS)])
                run_command(['taskset','-c',str(cpu),str(paths[arm]['filters']),
                             str(args.filter_iterations),'1'],cwd=inputs/arm,
                            log=logs/f'filters-trial-{trial}.csv',receipt=commands,
                            stderr_log=logs/f'filters-trial-{trial}.stderr.txt',
                            pad=STACK_PADS[trial % len(STACK_PADS)])
                json_file(out/'receipt.json',receipt)
            json_file(out/'progress.json',{'profile':profile,'completed_trial':trial+1,
                                          'trials':args.trials,'order':order})
    json_file(out/'receipt.json',receipt)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='mode',required=True)
    prepare_p=sub.add_parser('prepare',help='freeze baseline Ikea+oracle-only harness patch and current sources using capture.py')
    prepare_p.add_argument('capture',type=Path)
    prepare_p.add_argument('--scratch',type=Path,required=True)
    run_p=sub.add_parser('run',help='run inside prepared worker capture; never modifies frozen inputs')
    run_p.add_argument('--inputs',type=Path,default=INPUTS)
    run_p.add_argument('--output',type=Path,default=Path(os.environ.get('SIXDB_RESULTS','build/ikea-cohesion-v2-native/results')))
    run_p.add_argument('--cpu',type=int,default=int(os.environ.get('SIXDB_CPU','0')))
    run_p.add_argument('--jobs',type=int,default=int(os.environ.get('SIXDB_BUILD_JOBS','1')))
    run_p.add_argument('--profile',action='append',choices=PROFILES)
    run_p.add_argument('--trials',type=int,default=5)
    run_p.add_argument('--min-time',type=float,default=0.1)
    run_p.add_argument('--filter-iterations',type=int,default=300000)
    a=p.parse_args()
    if a.mode=='prepare':prepare(a)
    else:run(a)

if __name__=='__main__':main()
