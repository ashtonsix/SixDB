#!/usr/bin/env python3
"""Validate and summarize one width-56 run; never pool different captures."""
import csv
from collections import defaultdict
import math
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from evidence import verify_compact

ARMS = {'local_aos7': 1792, 'prior_shape': 1792, 'fixed_planes': 1792, 'plain_u64': 2048}


def read_and_validate(directory):
    directory = Path(directory)
    if (directory / 'artifact.json').exists():
        verify_compact(directory)
    with (directory / 'timings.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError('Empty timing export')
    groups, comparisons = defaultdict(list), defaultdict(list)
    for r in rows:
        suite, op, arm = r['suite'], r['operation'], r['arm']
        if arm not in ARMS or suite not in {'bulk', 'capacity'}:
            raise ValueError('Unknown arm/suite')
        n, ops, grain, passes = (int(r[x]) for x in ['logical_values', 'operations', 'values_per_operation', 'passes'])
        payload, inp, out, allocation = (int(r[x]) for x in ['payload_bytes', 'input_bytes', 'output_bytes', 'allocation_bytes'])
        if n < 256 or n % 256 or payload != n // 256 * ARMS[arm] or not ops or not passes:
            raise ValueError('Incorrect extent/accounting')
        if int(r['trace_bytes']) != 0 or int(r['payload_alignment']) != 64 or r['residence'] != 'unestablished':
            raise ValueError('Unsupported trace/alignment/residence contract')
        if suite == 'capacity':
            if op not in {'dependent', 'independent', 'get16'} or passes != 1 or grain != (16 if op == 'get16' else 1):
                raise ValueError('Incorrect capacity operation')
            if ops % 8 or inp != payload or out or allocation != payload or n & (n - 1):
                raise ValueError('Incorrect capacity allocation/work count')
            if not 0 < int(r['required_unique_payload_lines']) <= payload // 64:
                raise ValueError('Incorrect trace census')
        else:
            if op not in {'decode', 'encode', 'sum'} or grain != 256 or ops != n // 256 * passes:
                raise ValueError('Incorrect bulk operation')
            want_in = n * 8 if op == 'encode' else payload
            want_out = n * 8 if op == 'decode' else payload if op == 'encode' else 0
            if inp != want_in or out != want_out or allocation != payload + (0 if op == 'sum' else n * 8):
                raise ValueError('Incorrect bulk allocation')
        elapsed = int(r['elapsed_ns'])
        if elapsed <= 0 or not math.isclose(float(r['ns_per_operation']), elapsed / ops, rel_tol=1e-10):
            raise ValueError('Incorrect operation normalization')
        if not math.isclose(float(r['ns_per_value']), elapsed / ops / grain, rel_tol=1e-10):
            raise ValueError('Incorrect value normalization')
        if r['pmu_status'] == 'available' and not 0 < int(r['pmu_time_running_ns']) <= int(r['pmu_time_enabled_ns']):
            raise ValueError('Incorrect PMU scheduling counters')
        key = suite, op, n, arm
        groups[key].append(r)
        comparisons[(suite, op, n, int(r['repetition']))].append(r)
    rep_counts = set()
    for group in groups.values():
        reps = sorted(int(r['repetition']) for r in group)
        if reps != list(range(len(reps))):
            raise ValueError('Missing/duplicate repetitions')
        rep_counts.add(len(reps))
    if len(rep_counts) != 1:
        raise ValueError('Unequal repetition counts')
    for key, group in comparisons.items():
        if {r['arm'] for r in group} != set(ARMS) or len(group) != 4:
            raise ValueError('Missing/duplicate comparator arm')
        fields = ['operations', 'values_per_operation', 'passes', 'seed', 'data_seed', 'checksum0', 'checksum1', 'final_state', 'address_sum', 'trace_hash']
        if any(len({r[field] for r in group}) != 1 for field in fields):
            raise ValueError('Arms disagree on work/results/traces: ' + repr(key))
    return rows, groups


def main(directory):
    directory = Path(directory)
    rows, groups = read_and_validate(directory)
    summary = []
    for (suite, op, n, arm), samples in sorted(groups.items()):
        ns = [float(r['ns_per_operation']) for r in samples]
        cycles = [int(r['cycles_raw']) / int(r['operations']) for r in samples if r['pmu_status'] == 'available']
        local_ns = statistics.median(float(r['ns_per_operation']) for r in groups[(suite, op, n, 'local_aos7')])
        summary.append(dict(suite=suite, operation=op, logical_values=n, arm=arm, repetitions=len(samples),
            median_ns=statistics.median(ns), min_ns=min(ns), max_ns=max(ns),
            median_cycles=statistics.median(cycles) if cycles else 'NA',
            ratio_to_local=statistics.median(ns) / local_ns,
            mean_required_lines_per_request=statistics.mean(int(r['required_line_visits']) / int(r['operations']) for r in samples),
            max_major_faults=max(int(r['major_faults']) for r in samples),
            max_minor_faults=max(int(r['minor_faults']) for r in samples)))
    with (directory / 'summary.csv').open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    lines = ['# Width-56 results', '', 'One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.', '',
        '| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |', '| --- | ---: | --- | ---: | --- | ---: | ---: |']
    for r in summary:
        cycles = 'NA' if r['median_cycles'] == 'NA' else f"{r['median_cycles']:.3f}"
        lines.append(f"| {r['suite']} / {r['operation']} | {r['logical_values']} | {r['arm']} | {r['median_ns']:.3f} | {r['min_ns']:.3f}–{r['max_ns']:.3f} | {cycles} | {r['ratio_to_local']:.3f} |")
    available = [r for r in rows if r['pmu_status'] == 'available']
    if available and all(int(r['cache_misses_raw']) == 0 for r in available):
        lines += ['', 'The generic cache-miss event reported zero throughout this run; treat it as uninformative.']
    lines += ['', 'The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.']
    (directory / 'summary.md').write_text('\n'.join(lines) + '\n')
    print(f'Validated {len(rows)} timing rows / {len(groups)} cases; wrote summary.csv and summary.md')


if __name__ == '__main__':
    main(sys.argv[1])
