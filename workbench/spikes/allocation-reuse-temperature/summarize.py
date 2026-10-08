#!/usr/bin/env python3
"""Compare complete trials, keeping block timings, VM counts and byte gauges distinct."""
import argparse
from collections import defaultdict
import csv
import math
from pathlib import Path
import statistics

COUNTERS = ('allocations', 'reuses', 'new_allocations', 'mmap_calls', 'munmap_calls',
            'mprotect_calls', 'trim_pages')
GAUGES = ('mapped_bytes', 'idle_bytes')
CONTROLS = ('cpu', 'slots', 'operations')


def read_samples(path, baseline):
    groups = defaultdict(dict)
    identities = defaultdict(set)
    controls = set()
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        required = {'policy', 'trial', 'workload', 'sample', 'block_mean_ns_per_op',
                    'binary_sha256', *COUNTERS, *GAUGES, *CONTROLS}
        if not required <= set(reader.fieldnames or ()):
            raise ValueError('missing sample columns: ' + ', '.join(sorted(required - set(reader.fieldnames or ()))))
        for line, row in enumerate(reader, 2):
            try:
                for name in ('trial', 'sample', *COUNTERS, *GAUGES, *CONTROLS):
                    row[name] = int(row[name])
                row['touch_bytes'] = int(row.get('touch_bytes', '64'))
                row['block_mean_ns_per_op'] = float(row['block_mean_ns_per_op'])
            except (TypeError, ValueError) as error:
                raise ValueError(f'invalid numeric sample at line {line}') from error
            if any(row[name] < 0 for name in ('trial', 'sample', 'touch_bytes', *COUNTERS, *GAUGES, *CONTROLS)):
                raise ValueError(f'negative sample value at line {line}')
            if not row['policy'] or not row['workload'] or not row['slots'] or not row['operations']:
                raise ValueError(f'empty policy/workload or zero slots/operations at line {line}')
            if not math.isfinite(row['block_mean_ns_per_op']) or row['block_mean_ns_per_op'] <= 0:
                raise ValueError(f'nonpositive or nonfinite block timing at line {line}')
            if row['allocations'] != row['operations'] or row['reuses'] + row['new_allocations'] != row['allocations']:
                raise ValueError(f'allocation counts differ from operations at line {line}')
            key = (row['policy'], row['trial'], row['workload'], row['touch_bytes'])
            if row['sample'] in groups[key]:
                raise ValueError(f'duplicate block: {key}, sample {row["sample"]}')
            groups[key][row['sample']] = row
            identities[row['policy']].add(row['binary_sha256'])
            controls.add(tuple(row[name] for name in CONTROLS))
    if not groups or baseline not in identities:
        raise ValueError('samples must include the baseline policy: ' + baseline)
    if len(controls) != 1:
        raise ValueError('CPU, slots and operations must match across all compared samples')
    if any(len(hashes) != 1 or not next(iter(hashes)) for hashes in identities.values()):
        raise ValueError('each policy must use one fixed binary SHA-256 across all samples')
    trials = sorted({key[1] for key in groups})
    cases = sorted({key[2:] for key in groups})
    policies = [baseline, *sorted(identities.keys() - {baseline})]
    if trials != list(range(len(trials))):
        raise ValueError('trial IDs must form a complete zero-based sequence')
    expected = {(policy, trial, *case) for policy in policies for trial in trials for case in cases}
    if set(groups) != expected:
        missing = sorted(expected - set(groups))
        raise ValueError('unequal policy/trial/case coverage; missing pairs: ' + repr(missing[:8]))
    blocks = set(next(iter(groups.values())))
    if blocks != set(range(len(blocks))) or any(set(rows) != blocks for rows in groups.values()):
        raise ValueError('every policy/trial/case must contain the same complete zero-based block sequence')
    return groups, policies, trials, cases


def summaries(groups, policies, trials, cases, baseline):
    trial_rows = []
    for workload, width in cases:
        for trial in trials:
            baseline_ns = statistics.median(row['block_mean_ns_per_op']
                for row in groups[baseline, trial, workload, width].values())
            for policy in policies:
                rows = list(groups[policy, trial, workload, width].values())
                timings = [row['block_mean_ns_per_op'] for row in rows]
                median_ns = statistics.median(timings)
                record = dict(workload=workload, touch_bytes=width, policy=policy, trial=trial,
                              blocks=len(rows), operations_per_block=rows[0]['operations'],
                              median_block_ns_per_op=median_ns, min_block_ns_per_op=min(timings),
                              max_block_ns_per_op=max(timings),
                              baseline_median_block_ns_per_op=baseline_ns,
                              ratio_to_baseline=median_ns / baseline_ns)
                record.update((name + '_total', sum(row[name] for row in rows)) for name in COUNTERS)
                for name in GAUGES:
                    record[name + '_min'] = min(row[name] for row in rows)
                    record[name + '_max'] = max(row[name] for row in rows)
                trial_rows.append(record)
    summary_rows = []
    for workload, width in cases:
        for policy in policies:
            rows = [row for row in trial_rows if (row['workload'], row['touch_bytes'], row['policy']) ==
                    (workload, width, policy)]
            timings = [row['median_block_ns_per_op'] for row in rows]
            ratios = [row['ratio_to_baseline'] for row in rows]
            record = dict(workload=workload, touch_bytes=width, policy=policy,
                          trials=len(rows), blocks_per_trial=rows[0]['blocks'],
                          median_trial_ns_per_op=statistics.median(timings),
                          min_trial_ns_per_op=min(timings), max_trial_ns_per_op=max(timings),
                          median_ratio_to_baseline=statistics.median(ratios),
                          min_ratio_to_baseline=min(ratios), max_ratio_to_baseline=max(ratios))
            for name in COUNTERS:
                record[name + '_trial_total_min'] = min(row[name + '_total'] for row in rows)
                record[name + '_trial_total_max'] = max(row[name + '_total'] for row in rows)
            for name in GAUGES:
                record[name + '_min'] = min(row[name + '_min'] for row in rows)
                record[name + '_max'] = max(row[name + '_max'] for row in rows)
            summary_rows.append(record)
    return trial_rows, summary_rows


def write_csv(path, rows):
    with path.open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--samples', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='new directory for reports; inputs are never modified')
    parser.add_argument('--baseline', default='baseline')
    args = parser.parse_args()
    try:
        groups, policies, trials, cases = read_samples(args.samples.resolve(), args.baseline)
        trial_rows, summary_rows = summaries(groups, policies, trials, cases, args.baseline)
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        write_csv(output / 'trial-comparisons.csv', trial_rows)
        write_csv(output / 'summary.csv', summary_rows)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'{len(policies)} policies, {len(trials)} trials, {len(cases)} workload/width cases; {output}')


if __name__ == '__main__':
    main()
