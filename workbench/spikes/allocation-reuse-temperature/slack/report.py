#!/usr/bin/env python3
"""Reduce fresh-process trial block means; preserve capacity and VM costs."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
import statistics


def summarize(directory: Path):
    rows = list(csv.DictReader((directory / 'samples.csv').open()))
    groups = defaultdict(list)
    for row in rows:
        groups[(row['trace'], row['touch_bytes'], row['slots'], row['policy'], row['trial'])].append(row)
    trials = []
    for (trace, touch, slots, policy, trial), blocks in sorted(groups.items()):
        row = dict(trace=trace, touch_bytes=touch, slots=slots, policy=policy, trial=trial,
                   median_block_ns=statistics.median(float(b['block_mean_ns_per_op']) for b in blocks))
        for name in ['replay_requested_live_peak', 'replay_owned_capacity_peak', 'replay_owned_slack_peak',
                     'pool_mapped_peak', 'pool_metadata_peak', 'pool_reserved_peak']:
            row[name] = max(int(b[name]) for b in blocks)
        for name in ['mmap_calls', 'mprotect_calls', 'munmap_calls', 'new_allocations']:
            row[name] = sum(int(b[name]) for b in blocks)
        trials.append(row)
    baseline = {(r['trace'], r['touch_bytes'], r['slots'], r['trial']): r for r in trials if r['policy'] == 'baseline'}
    for row in trials:
        reference = baseline[(row['trace'], row['touch_bytes'], row['slots'], row['trial'])]
        row['ratio_to_baseline'] = row['median_block_ns'] / reference['median_block_ns']
    groups = defaultdict(list)
    for row in trials:
        groups[(row['trace'], row['touch_bytes'], row['slots'], row['policy'])].append(row)
    comparisons = []
    for (trace, touch, slots, policy), cases in sorted(groups.items()):
        row = dict(trace=trace, touch_bytes=touch, slots=slots, policy=policy, trials=len(cases))
        for name in list(cases[0])[5:]:
            row[name] = statistics.median(c[name] for c in cases)
        row['ratio_low'] = min(c['ratio_to_baseline'] for c in cases)
        row['ratio_high'] = max(c['ratio_to_baseline'] for c in cases)
        comparisons.append(row)
    for name, values in [('trials.csv', trials), ('comparisons.csv', comparisons)]:
        with (directory / name).open('w') as stream:
            writer = csv.DictWriter(stream, list(values[0])); writer.writeheader(); writer.writerows(values)
    return comparisons


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    args = p.parse_args()
    for row in summarize(args.directory):
        if row['policy'] != 'baseline':
            print(f"{row['trace']:21} touch={row['touch_bytes']:4} {row['policy']:9} "
                  f"{(row['ratio_to_baseline']-1)*100:+6.1f}% "
                  f"owned={row['replay_owned_capacity_peak']:.0f} mapped={row['pool_mapped_peak']:.0f} "
                  f"mmap={row['mmap_calls']:.0f}")
