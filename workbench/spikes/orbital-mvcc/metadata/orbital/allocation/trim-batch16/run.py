#!/usr/bin/env python3
"""Short native trim comparison over frozen controls; run with the pinned Linux toolchain."""
import argparse
import hashlib
import json
import shutil
import statistics
import subprocess
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command, log=None, cwd=None):
    result = subprocess.run(command, text=True, capture_output=True, timeout=180, cwd=cwd)
    if log:
        log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{command}: {result.stdout}\n{result.stderr}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--owner', type=int, default=2)
    parser.add_argument('--helper', type=int, default=3)
    parser.add_argument('--rounds', type=int, default=12)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = next(p for p in here.parents if (p / 'AGENTS.md').is_file())
    work = repo / 'build/orbital-mvcc-metadata/trim-batch16/root-comparison'
    work.mkdir(parents=True, exist_ok=True)
    inputs = {
        'baseline': here.parent / 'routes/direct-domain/direct-domain4-cut.json',
        'batch16': here / 'direct4-cut.json',
        'heap16': here.parent / 'heap-control/heap-control-cut.json',
    }
    runtime = {}
    hashes = {}
    for name, receipt in inputs.items():
        cut = json.loads(receipt.read_text())
        source = Path(cut['source'])
        hashes[name] = {f['path']: sha(source / f['path']) for f in cut['runtime_inputs']}
        for record in cut['runtime_inputs']:
            assert hashes[name][record['path']] == record['sha256'], record['path']
        runtime[name] = source / 'orbital'
    # Same optional logical counters in the baseline; compiled away in plain builds.
    baseline = work / 'baseline'
    if baseline.exists():
        shutil.rmtree(baseline)
    shutil.copytree(runtime['baseline'], baseline / 'orbital')
    invoke(['git', 'apply', '--directory=' + str(baseline.relative_to(repo)),
            str(here / 'comparison-counters.patch')], cwd=repo)
    runtime['baseline'] = baseline / 'orbital'
    derived_baseline = {p: sha(baseline / p) for p in hashes['baseline']}
    driver_hash = sha(here / 'trim_probe.cpp')
    recipe_hash = sha(Path(__file__))
    cmake = ['cmake_minimum_required(VERSION 3.24)', 'project(trim_probe LANGUAGES CXX)',
             'set(CMAKE_CXX_STANDARD 23)', 'set(CMAKE_CXX_EXTENSIONS OFF)',
             'find_package(Threads REQUIRED)',
             'add_compile_options(-O3 -g -DNDEBUG -fno-fast-math -ffp-contract=off)']
    tus = ['types.cpp', 'worker.cpp', 'allocation/allocation.cpp', 'allocation/arena.cpp',
           'allocation/group.cpp', 'allocation/window.cpp', 'allocation/finite.cpp']
    for name, source in runtime.items():
        for mode in ['plain', 'meter']:
            lib = name + '_' + mode
            cmake.append(f'add_library({lib} STATIC ' + ' '.join(str(source / 'src' / p) for p in tus) + ')')
            cmake.append(f'target_include_directories({lib} PUBLIC {source}/include {source}/src/allocation)')
            cmake.append(f'target_link_libraries({lib} PUBLIC Threads::Threads)')
            if mode == 'meter':
                cmake.append(f'target_compile_definitions({lib} PUBLIC SIXDB_TRIM_COMPARE=1)')
            cmake.append(f'add_executable({lib}_probe {here}/trim_probe.cpp)')
            cmake.append(f'target_link_libraries({lib}_probe PRIVATE {lib})')
            if mode == 'meter':
                cmake.append(f'target_compile_definitions({lib}_probe PRIVATE TRIM_FAULT_PROBE=1)')
                cmake.append(f'target_link_options({lib}_probe PRIVATE -Wl,--wrap=madvise)')
            if mode == 'plain':
                cmake.append(f'add_executable({name}_census {here.parent}/routes/census.cpp)')
                cmake.append(f'target_link_libraries({name}_census PRIVATE {lib})')
                cmake.append(f'target_compile_definitions({name}_census PRIVATE ROUTE_STORAGE=4)')
    (work / 'CMakeLists.txt').write_text('\n'.join(cmake) + '\n')
    build = work / 'native'
    invoke(['cmake', '-G', 'Ninja', '-S', str(work), '-B', str(build),
            '-DCMAKE_CXX_COMPILER=clang++-21'], work / 'configure.log')
    invoke(['cmake', '--build', str(build), '-j', '3'], work / 'build.log')
    print('Matched plain and instrumented controls compiled.', flush=True)
    cases = ['fragmented-a', 'fragmented-b', 'contiguous', 'coalesced-16', 'mixed', 'floor', 'empty']
    rows = []
    census = {}
    for name in runtime:
        census[name] = [json.loads(row) for row in invoke([str(build / (name + '_census'))]).splitlines()]
        for case in cases:
            for fault in ([False, True] if case in ['fragmented-a', 'mixed', 'floor'] else [False]):
                command = [str(build / (name + '_meter_probe')), str(args.owner), str(args.helper),
                           '0' if name == 'baseline' else '16', case]
                row = json.loads(invoke(command + (['fault'] if fault else [])))
                rows.append({'control': name, **row})
    (work / 'consequences.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(f'{len(rows)} instrumented consequence histories passed.', flush=True)
    timings = []
    for round_number in range(args.rounds):
        order = list(runtime) if round_number % 2 == 0 else list(reversed(runtime))
        for name in order:
            output = invoke([str(build / (name + '_plain_probe')), str(args.owner), str(args.helper),
                             '0' if name == 'baseline' else '16'])
            timings.extend({'control': name, 'round': round_number, **json.loads(row)}
                           for row in output.splitlines())
    (work / 'timings.json').write_text(json.dumps(timings, indent=2) + '\n')
    medians = []
    for name in runtime:
        for case in cases:
            sample = [r for r in timings if r['control'] == name and r['case'] == case]
            medians.append({'control': name, 'case': case, 'samples': len(sample),
                            **{key: statistics.median(r[key] for r in sample)
                               for key in ['wall_ns', 'owner_cpu_ns', 'process_cpu_ns', 'driver_calls']}})
    for name, receipt in inputs.items():
        source = Path(json.loads(receipt.read_text())['source'])
        assert hashes[name] == {p: sha(source / p) for p in hashes[name]}, 'Runtime changed during run'
    assert derived_baseline == {p: sha(baseline / p) for p in derived_baseline}, 'Derived baseline changed'
    assert driver_hash == sha(here / 'trim_probe.cpp') and recipe_hash == sha(Path(__file__)), 'Probe recipe changed'
    summary = {'compiler': invoke(['clang++-21', '--version']).splitlines()[0],
               'machine': invoke(['uname', '-m']).strip(), 'owner_cpu': args.owner, 'helper_cpu': args.helper,
               'plain_flags': '-O3 -g -DNDEBUG -fno-fast-math -ffp-contract=off; no counters or fault wrappers',
               'driver_sha256': driver_hash, 'recipe_sha256': recipe_hash,
               'counter_patch_sha256': sha(here / 'comparison-counters.patch'),
               'derived_baseline_inputs': derived_baseline,
               'runtime_inputs': hashes, 'census': census, 'consequence_histories': len(rows),
               'consequence_file_sha256': sha(work / 'consequences.json'),
               'timing_file_sha256': sha(work / 'timings.json'), 'medians': medians,
               'limits': ['AArch64 OrbStack 4KiB native pages; guest scheduling affects times.',
                          'Synthetic mapped inventory, not database traffic or point-operation latency.',
                          'Setup and final fixture close excluded; measured trim includes waits and owner replies.',
                          'Group metadata excludes Arena/thread/libc setup, Pool vector capacity and Object state.',
                          'Instrumented counters are finite Group submissions, not successful physical work.',
                          'Bounded spans do not bound census, span pages, Pool jobs or fairness under mutations.']}
    (work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'census': census, 'medians': medians}, indent=2), flush=True)


if __name__ == '__main__':
    main()
