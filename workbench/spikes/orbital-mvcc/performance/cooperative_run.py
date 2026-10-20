#!/usr/bin/env python3
"""Offline tables from the completed discovery panel; no new campaign dispatch."""
import argparse
import csv
import hashlib
import json
import statistics
from pathlib import Path


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-only', type=Path, required=True, metavar='COLLECTED_RUN')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report(json.loads((args.report_only / 'samples.json').read_text()), args.output)


if __name__ == '__main__':
    main()
