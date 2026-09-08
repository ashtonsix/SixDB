"""Regenerate tables from individual timings and matching work accounting."""
import csv
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from evidence import read_measurements


def main(root):
    receipt, measurements = read_measurements(root)
    if receipt['status'] not in ('running', 'complete'):
        raise ValueError('Unsuccessful receipt')
    accounting = list(csv.DictReader((root / 'accounting.csv').open()))
    records = {'/'.join(r[k] for k in ('case', 'method', 'phase')): r for r in accounting}
    if not records or len(records) != len(accounting):
        raise ValueError('Empty or duplicate comparison selection')
    groups = {key: [] for key in records}
    for sample in measurements['benchmarks']:
        if sample.get('run_type') != 'iteration':
            continue
        key = '/'.join(sample['name'].split('/')[:3])
        if key not in groups or sample.get('error_occurred') or sample['time_unit'] != 'ns':
            raise ValueError(f'Unexpected or failed sample: {sample}')
        groups[key].append(sample)
    results = []
    for key, samples in groups.items():
        row = records[key]
        if row['correct'] != '1' or len(samples) != receipt['config']['repetitions']:
            raise ValueError(f'Incomplete comparison: {key}')
        if len({s['repetition_index'] for s in samples}) != len(samples):
            raise ValueError(f'Duplicate repetition: {key}')
        if any(int(s['operations']) != int(row['operations']) for s in samples):
            raise ValueError(f'Denominator mismatch: {key}')
        times = [s['real_time'] / int(row['operations']) for s in samples]
        n = int(row['rows'])
        results.append(row | {'ns_per_op': statistics.median(times),
                              'min_ns': min(times), 'max_ns': max(times),
                              'candidate_fraction': int(row['candidates']) / n,
                              'metadata_B_row': int(row['stored_bytes']) / n})
    with (root / 'summary.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(results[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(results)
    lines = ['# Row signature measurements', '',
             'Pinned sequential manual wall-clock timings. Query ns/input row includes full mask output and exact residual checks.',
             'Build ns/input row includes allocation/construction; replace ns/write includes the primary AoS field and metadata.',
             'Work counters are logical accesses, not hardware traffic. Prefix planes are alternate value storage; other stored bytes are auxiliary.', '',
             '| Case | Method | Phase | ns/op | Min–max | True | Candidates | Groups by plane | Skipped blocks | Stored B/row |',
             '| --- | --- | --- | ---: | --- | ---: | ---: | --- | --- | ---: |']
    for r in results:
        groups = '/'.join(r[f'g{i}'] for i in range(4))
        lines.append(f'| {r["case"]} | {r["method"]} | {r["phase"]} | {r["ns_per_op"]:.3f} | {r["min_ns"]:.3f}–{r["max_ns"]:.3f} | {r["truth"]} | {r["candidates"]} | {groups} | {r["skipped"]}/{r["blocks"]} | {r["metadata_B_row"]:.3f} |')
    (root / 'summary.md').write_text('\n'.join(lines) + '\n')
    print(f'PASS: {len(results)} complete comparisons, matching oracle records and repetitions')


if __name__ == '__main__':
    main(Path(sys.argv[1]))
