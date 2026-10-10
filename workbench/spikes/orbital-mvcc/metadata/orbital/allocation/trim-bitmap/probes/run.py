#!/usr/bin/env python3
"""Focused selected full-span versus mapped trim-bitmap comparison, no source edits."""
import argparse
import csv
import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUNTIME_FILES = [
    'orbital/include/orbital/allocation.h', 'orbital/include/orbital/preparation.h',
    'orbital/include/orbital/types.h', 'orbital/include/orbital/worker.h',
    'orbital/src/types.cpp', 'orbital/src/worker.cpp',
    *['orbital/src/allocation/' + name for name in
      ['allocation.cpp', 'arena.cpp', 'group.cpp', 'window.cpp', 'finite.cpp', 'finite.h', 'internal.h']],
]
CASES = ['fragmented-a', 'fragmented-b', 'contiguous', 'coalesced-16', 'mixed', 'floor', 'empty']
EXTRA = ['cross-word', 'cross-word-partial-protect', 'cross-word-paused']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command, log=None):
    result = subprocess.run(command, text=True, capture_output=True, timeout=180)
    if log:
        log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{command}: {result.stdout}\n{result.stderr}')
    return result.stdout


def rows(command):
    return [json.loads(line) for line in invoke(command).splitlines() if line]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--owner', type=int, default=2)
    parser.add_argument('--helper', type=int, default=3)
    parser.add_argument('--rounds', type=int, default=4)
    parser.add_argument('--phase', choices=['baseline', 'comparison', 'qualification'], default='comparison')
    args = parser.parse_args()
    cuts = ['baseline'] if args.phase == 'baseline' else ['baseline', 'projection']
    build = HERE / 'native'
    inputs = {cut: {name: sha(HERE.parent / cut / name) for name in RUNTIME_FILES} for cut in cuts}
    probe_inputs = {name: sha(HERE / name) for name in ['CMakeLists.txt', 'trim_probe.cpp', 'fixture.h', 'census.cpp', 'run.py']}
    compiler = invoke(['clang++-21', '--version']).splitlines()[0]
    assert '21.1.8' in compiler, compiler
    invoke(['cmake', '-G', 'Ninja', '-S', str(HERE), '-B', str(build),
            '-DCMAKE_CXX_COMPILER=clang++-21'], HERE / 'configure.log')
    suffixes = ['_meter_probe', '_census'] if args.phase == 'qualification' else ['_plain_probe', '_meter_probe', '_census']
    targets = [cut + suffix for cut in cuts for suffix in suffixes]
    invoke(['cmake', '--build', str(build), '-j', '3', '--target', *targets], HERE / (args.phase + '-build.log'))
    print(f'{args.phase} matched targets compiled.', flush=True)
    census, consequences = {}, []
    for cut in cuts:
        census[cut] = rows([str(build / (cut + '_census'))])
        for case in CASES:
            for fault in ([False, True] if case in ['fragmented-a', 'mixed', 'floor'] else [False]):
                result = rows([str(build / (cut + '_meter_probe')), str(args.owner), str(args.helper), '0', case,
                               *(['fault'] if fault else [])])
                assert len(result) == 1
                row = result[0]
                if args.phase == 'qualification':
                    row = {key: value for key, value in row.items()
                           if key not in ['wall_ns', 'owner_cpu_ns', 'process_cpu_ns']}
                consequences.append({'control': cut, **row})
        for case in EXTRA:
            result = rows([str(build / (cut + '_meter_probe')), str(args.owner), str(args.helper), '0', case])
            assert len(result) == 1
            consequences.append({'control': cut, **result[0]})
    consequence_file = HERE / (args.phase + '-consequences.json')
    consequence_file.write_text(json.dumps(consequences, indent=2) + '\n')
    print(f'{len(consequences)} wrapped consequence histories passed.', flush=True)
    timings, medians = [], []
    if args.phase == 'comparison':
        for round_number in range(args.rounds):
            order = cuts if round_number % 2 == 0 else list(reversed(cuts))
            for cut in order:
                results = rows([str(build / (cut + '_plain_probe')), str(args.owner), str(args.helper), '0'])
                assert len(results) == len(CASES)
                timings.extend({'control': cut, 'round': round_number, **row} for row in results)
        (HERE / 'timings.json').write_text(json.dumps(timings, indent=2) + '\n')
        for cut in cuts:
            for case in CASES:
                sample = [r for r in timings if r['control'] == cut and r['case'] == case]
                medians.append({'control': cut, 'case': case, 'samples': len(sample),
                                **{key: statistics.median(r[key] for r in sample)
                                   for key in ['wall_ns', 'owner_cpu_ns', 'process_cpu_ns', 'driver_calls']}})
        with (HERE / 'comparison.csv').open('w', newline='') as output:
            writer = csv.DictWriter(output, fieldnames=list(medians[0]))
            writer.writeheader(); writer.writerows(medians)
    if len(cuts) == 2:
        baseline = {r['case']: r for r in consequences if r['control'] == 'baseline' and not r['fault'] and r['case'] in CASES}
        projection = {r['case']: r for r in consequences if r['control'] == 'projection' and not r['fault'] and r['case'] in CASES}
        for case in CASES:
            for key in ['jobs', 'ranges', 'pages', 'retire_jobs', 'retire_pages', 'retained_idle', 'before_mapped', 'after_mapped']:
                assert baseline[case][key] == projection[case][key], (case, key, baseline[case], projection[case])
    for cut in cuts:
        assert inputs[cut] == {name: sha(HERE.parent / cut / name) for name in RUNTIME_FILES}, 'Runtime changed during measurement'
    assert probe_inputs == {name: sha(HERE / name) for name in probe_inputs}, 'Probe changed during measurement'
    summary = {
        'phase': args.phase, 'base_commit': '768e033584bc83e097de34e0f59ee96ba0e491cc',
        'compiler': compiler, 'machine': platform.machine(), 'owner_cpu': args.owner, 'helper_cpu': args.helper,
        'flags': '-O3 -g -UNDEBUG -fno-fast-math -ffp-contract=off; assertions enabled in both cuts',
        'runtime_inputs': inputs, 'probe_inputs': probe_inputs, 'source_hashes_checked_before_and_after': True,
        'census': census, 'consequence_histories': len(consequences),
        'consequences_sha256': sha(consequence_file),
        'counter_definition': 'Wrapped detail::schedule counts actual finite Group jobs and submitted page/range inventory; wrapped madvise/mprotect count attempted trim run syscalls. Retry jobs reuse a previously submitted Group. No visit counters claimed.',
        'limits': [
            'AArch64 OrbStack guest with 4 KiB pages; no target-machine conclusion.',
            'Synthetic real mapped and charged idle inventory, not database traffic.',
            'Consequence binaries include wrapper and independent topology-decoding work; their durations are not performance evidence.',
            'Requested metadata excludes Arena/Pool/thread/libc setup, first owner-vector capacity and Object state; not RSS.',
            'No sanitizer or weak-memory qualification; lifetime review and existing runtime histories remain separate.',
            'Production Group identity width remains 256. Parent geometry census shows 1024-identity palette A control mapping grows from 8192 to 12288 bytes; 64/256 A and all tested B widths are unchanged.',
        ],
    }
    if args.phase == 'qualification':
        summary['timing_rounds_run'] = 0
        summary['qualification_scope'] = 'Final runtime source: 26 wrapped consequence histories plus actual census; no plain timing runs, timing fields or medians.'
        prior_summary = HERE.parent / 'measured-candidate/probes/comparison-summary.json'
        if prior_summary.exists():
            prior = json.loads(prior_summary.read_text())
            summary['prior_candidate_evidence'] = {
                'summary_path': str(prior_summary), 'summary_sha256': sha(prior_summary),
                'runtime_inputs': prior['runtime_inputs'],
                'interpretation': 'Prior-candidate timing belongs to the preserved earlier source and recipe, with concurrent native build load and strong variation. It establishes no performance winner or absence of regression in this final source.',
            }
    if timings:
        summary['medians'] = medians
        summary['limits'].extend([
            'Short duration screen: setup and final close excluded; trim timing includes waits and owner replies.',
            'Plain timing has no wrappers, counter code, faults or topology introspection in the timed loop.',
        ])
        summary['timings_sha256'] = sha(HERE / 'timings.json')
        summary['comparison_csv_sha256'] = sha(HERE / 'comparison.csv')
    (HERE / (args.phase + '-summary.json')).write_text(json.dumps(summary, indent=2) + '\n')
    output = {'census': census}
    if timings:
        output['medians'] = medians
    print(json.dumps(output, indent=2), flush=True)


if __name__ == '__main__':
    main()
