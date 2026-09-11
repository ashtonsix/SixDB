#!/usr/bin/env python3
"""Check the exact ordered inventory; retain every repetition and both pairing edges."""
import csv
import json
import math
from pathlib import Path
import statistics
import sys

ORDER = ['specialized', 'native', 'materialized', 'materialized', 'native', 'specialized']


def require(value, why):
    if not value: raise ValueError(why)


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def table(path, rows):
    with path.open('w') as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)


def main():
    root = Path(sys.argv[1])
    prepared = json.loads((root / 'prepared.json').read_text())
    workloads = ['structural', 'random_half', 'structural_tail129', *sorted(prepared['details']['windows'])]
    require(len(workloads) == 15 and all(n == 8 for n in prepared['details']['windows'].values()), 'prepared windows')
    expected = []
    cases = []
    for block, reader in enumerate(ORDER):
        for workload in workloads:
            n = 129 if workload == 'structural_tail129' else 256
            suffixes = ['refill/g' + str(g) for g in [0, (n // 2) & ~15, (n - 1) & ~15]]
            suffixes += [f'{cut}/{first}/{count}' for first, count in [(0, n), (3, 37), (15, 18), (15, 2), (127, 2), (n-1, 1)]
                         for cut in ['inline', 'split']]
            for suffix in suffixes:
                case = workload + '/' + suffix
                expected.append(f'bec/{block}-{reader}/{case}')
                if block == 0: cases.append(case)
    samples = []
    by_name = {}
    stable = ['windows', 'logical_blocks', 'bound_target', 'metadata_bytes', 'body_bytes_including_suffix',
              'query_bytes', 'source_descriptor_bytes', 'input_hash_lo', 'input_hash_hi',
              'metadata_mod4096', 'lengths_mod4096', 'body_mod4096', 'query_mod4096']
    for filename, repetitions in [('preflight.json', 1), ('timings.json', 3)]:
        raw = json.loads((root / filename).read_text())
        rows = [row for row in raw['benchmarks'] if row.get('run_type') == 'iteration']
        require(len(rows) == len(expected) * repetitions, filename + ': sample count')
        require([r['name'] for r in rows] == [name for name in expected for _ in range(repetitions)], filename + ': execution order')
        anchors = {}
        for index, row in enumerate(rows):
            require(not row.get('error_occurred') and row['threads'] == 1 and row['time_unit'] == 'ns', 'sample status/units')
            require(row['repetition_index'] == index % repetitions and row['iterations'] > 0, 'repetition/iterations')
            require(all(math.isfinite(row[k]) and row[k] > 0 for k in ['cpu_time', 'real_time', 'items_per_second']), 'finite timing')
            block_reader, case = row['name'].removeprefix('bec/').split('/', 1)
            block, reader = block_reader.split('-', 1)
            workload, cut, *args = case.split('/')
            n = 129 if workload == 'structural_tail129' else 256
            require(row['logical_blocks'] == n and row['windows'] == 8, 'logical dimensions')
            require(row['metadata_bytes'] == 4096 and row['query_bytes'] == 8*n*32, 'logical storage counters')
            require(math.isclose(1e9 / row['items_per_second'], row['cpu_time'] / 256, rel_tol=1e-9), 'ns/operation scale')
            fingerprint = [row[k] for k in stable]
            if workload not in anchors: anchors[workload] = fingerprint
            require(anchors[workload] == fingerprint, 'allocation/input changed within paired process')
            if cut == 'refill':
                refills = 1
                count = 0
            else:
                first, count = map(int, args)
                refills = (first + count - 1) // 16 - first // 16 + 1
                require(row['requested_blocks'] == count, 'query count')
            require(row['refills_per_operation'] == refills, 'refill count')
            if filename == 'timings.json':
                record = dict(case=case, block=int(block), reader=reader, repetition=row['repetition_index'],
                    iterations=row['iterations'], ns_per_operation=row['cpu_time']/256,
                    real_ns_per_operation=row['real_time']/256, refills=refills, requested_blocks=count,
                    expected_count_sum=row.get('expected_count_sum', 0), **{k: row[k] for k in stable})
                samples.append(record)
                by_name.setdefault((case, reader), []).append(record)
    table(root / 'samples.csv', samples)
    paired = []
    for case in cases:
        a = by_name[case, 'native']
        for control, edges in [('specialized', [(1, 0), (4, 5)]), ('materialized', [(1, 2), (4, 3)])]:
            b = by_name[case, control]
            av = [r['ns_per_operation'] for r in a]; bv = [r['ns_per_operation'] for r in b]
            am, bm = statistics.median(av), statistics.median(bv)
            edge = [statistics.median(r['ns_per_operation'] for r in a if r['block'] == x) /
                    statistics.median(r['ns_per_operation'] for r in b if r['block'] == y) for x, y in edges]
            paired.append(dict(case=case, control=control, scope='refill' if '/refill/' in case else 'count',
                native_ns=am, control_ns=bm, native_over_control=am/bm,
                separated='win' if max(av) < min(bv) else 'loss' if min(av) > max(bv) else 'overlap',
                first_edge=edge[0], second_edge=edge[1], edges_agree=(edge[0] < 1) == (edge[1] < 1),
                native_samples=json.dumps(av), control_samples=json.dumps(bv)))
    table(root / 'paired.csv', paired)
    summary = {}
    for control in ['specialized', 'materialized']:
        for scope in ['refill', 'count']:
            selected = [r for r in paired if r['control'] == control and r['scope'] == scope]
            summary[control + '/' + scope] = dict(cases=len(selected),
                separated={k: sum(r['separated'] == k for r in selected) for k in ['win', 'loss', 'overlap']},
                median_ratio=statistics.median(r['native_over_control'] for r in selected),
                edges_agree=sum(r['edges_agree'] for r in selected))
    result = dict(samples=len(samples), preflights=len(expected), cases_per_reader=len(cases), order=ORDER, summary=summary)
    save(root / 'summary.json', result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
