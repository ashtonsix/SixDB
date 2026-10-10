#!/usr/bin/env python3
"""Bounded large-body Message-service panel; throughput and delay clocks stay separate."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import sys
import shutil

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run

ARMS = [('unloaded', 'ring', 128), ('empty', 'ring', 128),
        ('loaded', 'ring', 128), ('deferred', 'ring', 128),
        ('loaded', 'ring', 32), ('loaded', 'manual', 128)]


def cases():
    for repetition in range(2):
        modes = ['throughput', 'delay'] if repetition == 0 else ['delay', 'throughput']
        for mode in modes:
            arms = [arm for arm in ARMS if mode == 'throughput' or arm[0] not in ('unloaded', 'empty')]
            if repetition:
                arms = list(reversed(arms))
                arms = arms[1:] + arms[:1]
            for treatment, execution, quantum in arms:
                yield dict(repetition=repetition, mode=mode, treatment=treatment,
                           execution=execution, quantum=quantum)


def distribution(values):
    values = sorted(values)
    return {'count': len(values), 'sum': sum(values),
            **{label: values[(len(values) * rank + 999) // 1000 - 1] if values else None
               for label, rank in [('p50', 500), ('p99', 990), ('max', 1000)]}}


def audit_ordered(path, data):
    rows = list(csv.DictReader(path.open()))
    assert len(rows) == data['groups'] == 513
    assert [int(row['group']) for row in rows] == list(range(len(rows)))
    assert sum(int(row['requests']) for row in rows) == data['operations'] == 524291
    assert all(int(row['requests']) == 1024 for row in rows[:-1]) and int(rows[-1]['requests']) == 3
    read = [int(row['read_ns']) for row in rows]
    complete = [int(row['complete_ns']) for row in rows]
    assert all(0 < r <= c for r, c in zip(read, complete))
    assert sum(read) == data['read_complete_ns'] and sum(complete) == data['all_work_complete_ns']
    assert sum(int(row['quanta']) for row in rows) == data['returned_quanta']
    assert sum(int(row['polls']) for row in rows) == data['poll_calls']
    loaded = data['treatment'] in ('loaded', 'deferred')
    assert data['messages'] == (8 * len(rows) if loaded else 0)
    for row in rows:
        q, polls, at_read = (int(row[key]) for key in ['quanta', 'polls', 'messages_at_read'])
        expected = min(q, 8) if data['treatment'] == 'loaded' else 0
        assert at_read == expected
        expected_polls = 0 if data['treatment'] == 'unloaded' else 8 if data['treatment'] == 'deferred' else q + (8 - expected if loaded else 0)
        assert polls == expected_polls
    delays = [int(row[f'completion_delay_{i}_ns']) for row in rows for i in range(8)]
    diagnostic = data['mode'] == 'delay'
    if diagnostic:
        assert loaded and all(delays)
        observed = distribution(delays)
        for label in ['p50', 'p99', 'max']:
            assert observed[label] == data[f'completion_delay_{label}_ns']
        assert observed['count'] == data['completion_delay_samples'] == 4104
    else:
        assert not any(delays) and data['completion_delay_samples'] == 0
        observed = distribution([])
    return {'group_read_ns': distribution(read), 'group_complete_ns': distribution(complete),
            'completion_delay_ns': observed, 'messages_at_read': distribution([int(r['messages_at_read']) for r in rows]),
            'final_group': {k: int(v) for k, v in rows[-1].items()},
            'ordered_csv_sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def report(samples, output):
    keys = ['repetition', 'mode', 'treatment', 'execution', 'quantum']
    costs = ['read_ns_per_request', 'all_work_ns_per_request', 'caller_cpu_ns_per_request', 'process_cpu_ns_per_request']
    measures = ['returned_quanta', 'poll_calls', 'messages', 'digest', 'logical_rows',
                'driver_record_bytes', 'message_record_bytes', 'completion_control_bytes',
                'message_ops_static_bytes', 'group_sample_bytes', 'delay_storage_bytes', 'ring_arena_charge']
    quantiles = [f'{family}_{q}' for family in ['group_read_ns', 'group_complete_ns', 'completion_delay_ns'] for q in ['count', 'p50', 'p99', 'max']]
    table = []
    for case, data, observed in samples:
        row = dict(case)
        for field, source in zip(costs, ['read_complete_ns', 'all_work_complete_ns', 'caller_cpu_ns', 'process_cpu_ns']):
            row[field] = data[source] / data['operations']
        row.update({k: data[k] for k in measures})
        row.update({f'{family}_{q}': observed[family][q] for family in ['group_read_ns', 'group_complete_ns', 'completion_delay_ns'] for q in ['count', 'p50', 'p99', 'max']})
        table.append(row)
    with (output / 'measurements.csv').open('w') as out:
        writer = csv.DictWriter(out, keys + costs + measures + quantiles)
        writer.writeheader(); writer.writerows(table)
    groups = {}
    for row in table:
        key = tuple(row[k] for k in keys[1:])
        groups.setdefault(key, []).append(row)
    with (output / 'summary.csv').open('w') as out:
        metrics = costs + [f'{family}_{q}' for family in ['group_read_ns', 'group_complete_ns', 'completion_delay_ns'] for q in ['p50', 'p99', 'max']]
        writer = csv.DictWriter(out, keys[1:] + ['observations'] + [f'{m}_median' for m in metrics] + [f'{m}_observations' for m in metrics])
        writer.writeheader()
        for key, rows in groups.items():
            row = dict(zip(keys[1:], key)) | {'observations': len(rows)}
            for m in metrics:
                values = [r[m] for r in rows]
                row[f'{m}_median'] = statistics.median(values) if all(v is not None for v in values) else ''
                row[f'{m}_observations'] = json.dumps(values)
            writer.writerow(row)


def qualify(run, binary, check):
    results = []
    for mode in ['throughput', 'delay']:
        for treatment, execution, quantum in ARMS:
            if mode == 'delay' and treatment in ('unloaded', 'empty'):
                continue
            name = f'qualification-{len(results):02d}'
            command = [binary, '--rows', '128', '--objects', '4', '--ops', '35', '--group', '32', '--verify-only', '1',
                       '--mode', mode, '--treatment', treatment, '--execution', execution, '--quantum', str(quantum)]
            if treatment == 'loaded' and quantum == 128:
                command += ['--refusal-check', '1']
            run.step(name, command, stdout_name=name + '.json')
            data = json.loads((run.output / (name + '.json')).read_text())
            assert data['passed'] and data['final_group_requests'] == 3 and data['groups'] == 2
            assert data['read_complete_ns'] == data['all_work_complete_ns'] == data['caller_cpu_ns'] == 0
            assert data['messages'] == (16 if treatment in ('loaded', 'deferred') else 0)
            assert data['completion_delay_samples'] == (16 if mode == 'delay' else 0)
            results.append(data)
    assert len({r['digest'] for r in results}) == 1
    run.step('original-ring-check', [check], stdout_name='original-ring-check.json')
    original = json.loads((run.output / 'original-ring-check.json').read_text())
    (run.output / 'native-qualification.json').write_text(json.dumps({'passed': True, 'control_cases': results, 'original_ring_check': original, 'sanitizer_claim': False}, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report-only', type=Path)
    parser.add_argument('--cpu', type=int, default=int(os.environ.get('SIXDB_CPU', '0')))
    parser.add_argument('--jobs', type=int, default=1)
    args = parser.parse_args()
    if args.report_only:
        report(json.loads((args.report_only / 'samples.json').read_text()), args.output)
        return
    config = dict(cpu=args.cpu, jobs=args.jobs, rows=8388608, objects=256, ops=524291, group=1024, width=32, hints=True, steps=16,
                  repetitions=2, march='x86-64-v3', tune='zen5', pmu='off', arrival_model='eight owner-posted ready Messages per group',
                  clock_modes_separate=True, ordering='first forward; second reversed then rotated; reverse mode blocks')
    workspace = Path(os.environ.get('SIXDB_DATA_CACHE', ROOT / 'build')) / 'orbital-cooperative-feedback'
    run = Run(ROOT, args.output, config, workspace=workspace)
    error = None
    try:
        source, build = run.source_root, run.build_dir
        run.step('configure', ['cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                              '-DCMAKE_CXX_COMPILER=clang++-21', '-DSIXDB_SPIKES=orbital-mvcc', '-DSIXDB_MARCH=x86-64-v3', '-DSIXDB_TUNE=zen5'])
        run.step('build', ['cmake', '--build', build, '--target', 'loom_object_polling_control', 'loom_object_ring_check', '-j', str(args.jobs)])
        binary = build / 'workbench/spikes/orbital-mvcc/performance/loom_object_polling_control'
        check = build / 'workbench/spikes/orbital-mvcc/metadata/loom_object_ring_check'
        (args.output / 'binary-identity.json').write_text(json.dumps({name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in [('control', binary), ('ring_check', check)]}, indent=2) + '\n')
        qualify(run, binary, check)
        samples = []
        for i, case in enumerate(cases()):
            name = f'trial-{i:02d}'
            ordered = args.output / (name + '-ordered.csv')
            opts = ['--rows', '8388608', '--objects', '256', '--ops', '524291', '--group', '1024', '--samples', str(ordered)]
            for key in ['mode', 'treatment', 'execution', 'quantum']:
                opts += ['--' + key, str(case[key])]
            run.step(name, ['taskset', '-c', str(args.cpu), str(binary), *opts], stdout_name=name + '.json')
            data = json.loads((args.output / (name + '.json')).read_text())
            assert data['passed'] and data['logical_rows'] == 16 * data['operations']
            samples.append((case, data, audit_ordered(ordered, data)))
            (args.output / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n')
        assert len(samples) == 20 and len({d['digest'] for c, d, a in samples}) == 1
        for c, d, a in samples:
            if c['quantum'] != 128:
                continue
            assert len({other['returned_quanta'] for _, other, _ in samples if other['quantum'] == 128}) == 1
        report(samples, args.output)
        (args.output / 'qualification.json').write_text(json.dumps({'passed': True, 'timing_trials': len(samples), 'same_read_digest': True,
            'ordered_samples_audited': True, 'matching_q128_quantum_counts': True, 'sanitizer_claim': False, 'pmu': 'off',
            'limits': 'Ready owner-posted bursts only; group read/all-work makespans and post-to-handler Message delay are separate distributions; no per-request latency or external arrival/fairness claim.'}, indent=2) + '\n')
        (args.output / 'build-cost.json').write_text(json.dumps([c for c in run.receipt['commands'] if c['name'] in ('configure', 'build')], indent=2) + '\n')
        # Preserve executable bytes in the full archive, outside all work clocks.
        binaries = args.output / 'binaries'
        binaries.mkdir()
        for path in [binary, check]:
            shutil.copy2(path, binaries / path.name)
        run.compact(['samples.json', 'measurements.csv', 'summary.csv', 'qualification.json', 'native-qualification.json', 'binary-identity.json', 'build-cost.json'], [])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    if error:
        raise error


if __name__ == '__main__':
    main()
