#!/usr/bin/env python3
"""Recompute accounting and exact per-trial quantiles from every offered ID."""
import csv
import gzip
import json
import math
from pathlib import Path
import sys


def quantile(values, fraction):
    if not values:
        return None
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def reduce_trial(path, execution):
    name, c = execution['name'], execution['case']
    native = json.loads((path / (name + '.json')).read_text())
    values = {k: [] for k in ['offer_to_done', 'prepare_to_done', 'publication_to_begin_upper',
                              'publication_to_begin_lower', 'consume', 'producer_lateness', 'publication_bracket']}
    totals = dict(offered=0, admitted=0, refused=0, completed=0, unattempted=0)
    steady = dict(totals)
    cutoff = min(20_000_000, c['count'] // c['burst'] * c['interval'] // 10)
    detailed = c.get('timing', 'full') == 'full'
    refusal_max_size = 0
    last_done = 0
    with gzip.open(path / (name + '.csv.gz'), 'rt') as source:
        for row in csv.DictReader(source):
            row = {k: int(v) if v else None for k, v in row.items()}
            i = totals['offered']
            assert row['id'] == i and row['scheduled_ns'] == i // c['burst'] * c['interval']
            totals['offered'] += 1
            measured = row['scheduled_ns'] >= cutoff
            if measured:
                steady['offered'] += 1
            state = row['state']
            assert state in [0, 1, 2]
            totals[{0: 'unattempted', 1: 'admitted', 2: 'refused'}[state]] += 1
            if measured:
                steady[{0: 'unattempted', 1: 'admitted', 2: 'refused'}[state]] += 1
            if state == 2:
                assert row['refused_at_ns'] >= row['scheduled_ns'] and row['refusal_batch_size'] > 0
                refusal_max_size = max(refusal_max_size, row['refusal_batch_size'])
            done = row['consume_end_ns']
            if not row['completed']:
                assert done is None
                assert row['consume_begin_ns'] is None
                continue
            totals['completed'] += 1
            if measured:
                steady['completed'] += 1
            assert state == 1
            if not detailed:
                assert all(row[k] is None for k in ['prepare_ns', 'publish_before_ns', 'publish_after_ns', 'consume_begin_ns', 'consume_end_ns'])
                continue
            due, prepared = row['scheduled_ns'], row['prepare_ns']
            before, after, begin = row['publish_before_ns'], row['publish_after_ns'], row['consume_begin_ns']
            assert state == 1 and due <= prepared <= before <= after and before <= begin <= done and last_done <= begin
            last_done = done
            # All accounting remains whole-run; steady percentiles exclude only
            # the first 20 ms of scheduled offers (2 ms in the quick check).
            if measured:
                for key, value in dict(offer_to_done=done-due, prepare_to_done=done-prepared,
                    publication_to_begin_upper=begin-before, publication_to_begin_lower=max(0, begin-after),
                    consume=done-begin, producer_lateness=prepared-due, publication_bracket=after-before).items():
                    values[key].append(value)
    totals['unfinished'] = totals['offered'] - totals['refused'] - totals['completed']
    steady['unfinished'] = steady['offered'] - steady['refused'] - steady['completed']
    assert totals['offered'] == c['count'] and native['errors'] == 0
    assert all(native[k] == v for k, v in totals.items())
    assert native['offer_window_ns'] == ((c['count'] + c['burst'] - 1) // c['burst']) * c['interval']
    assert native['last_arrival_ns'] == ((c['count'] - 1) // c['burst']) * c['interval']
    result = dict(case=c['name'], rep=execution['rep'], **{k:v for k,v in c.items() if k!='name'}, **native)
    result.update({'steady_' + k: v for k, v in steady.items()})
    result['max_refusal_batch_size'] = refusal_max_size
    result['completion_fraction'] = totals['completed'] / totals['offered']
    result['mean_publication_items'] = totals['admitted'] / native['publications'] if native['publications'] else 0
    result['endpoint_cpu_ns_per_completion'] = ((native['producer_cpu_ns'] + native['consumer_cpu_ns']) / totals['completed']
                                                if totals['completed'] else None)
    result['steady_completed_samples'] = len(values['offer_to_done'])
    for key, samples in values.items():
        for suffix, fraction in [('p50', .5), ('p99', .99), ('p999', .999), ('max', 1)]:
            result[f'{key}_{suffix}_ns'] = (None if suffix == 'p999' and len(samples) < 10000 else quantile(samples, fraction))
    return result


def main(path):
    path = Path(path)
    rows = [reduce_trial(path, e) for e in json.loads((path / 'executions.json').read_text())]
    (path / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
    with (path / 'summary.csv').open('w') as out:
        w = csv.DictWriter(out, list(rows[0]))
        w.writeheader(); w.writerows(rows)
    print(f'{len(rows)} trials: complete accounting and monotonic boundaries; {sum(r["offered"] for r in rows)} offers')


if __name__ == '__main__':
    main(sys.argv[1])
