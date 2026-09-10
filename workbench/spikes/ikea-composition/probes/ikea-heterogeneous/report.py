#!/usr/bin/env python3
"""Validate one captured matched-work comparison and regenerate compact summaries."""
import csv
from collections import defaultdict
from pathlib import Path
import statistics
import sys
ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from evidence import verify_compact


def read_and_validate(directory):
    directory = Path(directory)
    if (directory / 'provenance.json').exists():
        verify_compact(directory)
    with (directory / 'timings.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    assert rows, 'empty timings'
    groups = defaultdict(list)
    matched = defaultdict(list)
    for row in rows:
        ints = {key: int(row[key]) for key in ['first', 'count', 'repetition', 'windows', 'passes', 'calls', 'logical_blocks', 'metadata_bytes', 'body_bytes', 'query_bytes', 'body_suffix_bytes', 'checksum', 'elapsed_ns']}
        assert 0 <= ints['first'] < 256 and 0 < ints['count'] <= 256 - ints['first']
        assert ints['calls'] == ints['windows'] * ints['passes'] > 0
        assert ints['logical_blocks'] == ints['calls'] * ints['count']
        assert ints['query_bytes'] == ints['windows'] * 8192
        assert ints['body_suffix_bytes'] == ints['windows'] * 64
        assert ints['metadata_bytes'] == ints['windows'] * (1024 if row['layout'] == 'direct32' else 512)
        assert row['residence'] == 'unestablished'
        for unit, denominator in [('ns_per_call', ints['calls']), ('ns_per_block', ints['logical_blocks'])]:
            expected = ints['elapsed_ns'] / denominator
            assert abs(float(row[unit]) - expected) <= max(1e-6, abs(expected) * 1e-5)
        key = (row['dataset'], ints['first'], ints['count'])
        groups[key + (row['layout'], row['execution'])].append(row)
        matched[key + (ints['repetition'],)].append(row)
    arms = {(layout, execution) for layout in ['direct32', 'local16', 'scan128'] for execution in ['inline', 'split']}
    for samples in matched.values():
        assert len(samples) == 6 and {(r['layout'], r['execution']) for r in samples} == arms
        for field in ['windows', 'passes', 'calls', 'logical_blocks', 'checksum', 'body_bytes', 'query_bytes', 'body_suffix_bytes']:
            assert len({r[field] for r in samples}) == 1, field
    for samples in groups.values():
        reps = sorted(int(r['repetition']) for r in samples)
        assert reps == list(range(len(samples))), 'duplicate/missing repetitions'
    return rows, groups


def main(directory):
    directory = Path(directory)
    rows, groups = read_and_validate(directory)
    summary = []
    for (dataset, first, count, layout, execution), samples in sorted(groups.items()):
        times = [float(r['ns_per_call']) for r in samples]
        baseline = statistics.median(float(r['ns_per_call']) for r in groups[(dataset, first, count, 'direct32', 'inline')])
        inline = statistics.median(float(r['ns_per_call']) for r in groups[(dataset, first, count, layout, 'inline')])
        cycles = [int(r['cycles_raw']) / int(r['calls']) for r in samples if r['pmu_status'] == 'available']
        summary.append(dict(dataset=dataset, first=first, count=count, layout=layout, execution=execution,
            repetitions=len(samples), median_ns=statistics.median(times), min_ns=min(times), max_ns=max(times),
            cycles_per_call=statistics.median(cycles) if cycles else 'NA',
            ratio_to_direct_inline=baseline/statistics.median(times), split_over_inline=statistics.median(times)/inline))
    with (directory / 'summary.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader(); writer.writerows(summary)
    lines = ['# Heterogeneous range results', '',
        'One captured run; median ns/range call. Baseline/arm >1 favours the arm. Split/inline >1 is the split cost for the same metadata. These are repeated resident controls, with cache residence unestablished; no cold random-access claim follows.', '',
        '| Dataset | Range first/count | Metadata | Region | Median ns | Range | Cycles/call | Baseline/arm | Split/inline |',
        '| --- | --- | --- | --- | ---: | --- | ---: | ---: | ---: |']
    for r in summary:
        cycles = 'NA' if r['cycles_per_call'] == 'NA' else f"{r['cycles_per_call']:.3f}"
        lines.append(f"| {r['dataset']} | {r['first']}/{r['count']} | {r['layout']} | {r['execution']} | {r['median_ns']:.3f} | {r['min_ns']:.3f}–{r['max_ns']:.3f} | {cycles} | {r['ratio_to_direct_inline']:.3f} | {r['split_over_inline']:.3f} |")
    (directory / 'summary.md').write_text('\n'.join(lines) + '\n')
    print(f'Validated {len(rows)} timing rows and {len(groups)} matched cases')


if __name__ == '__main__':
    main(sys.argv[1])
