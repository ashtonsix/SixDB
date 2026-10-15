#!/usr/bin/env python3
"""Keep whole-intake reciprocal cost, lifecycle phases and storage distinct."""
import argparse
import csv
import hashlib
import json
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('root', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--pattern', default='hashtable_design_worker_epochs_*-order*.csv')
a = p.parse_args()
groups, hashes = {}, {}
files = sorted(a.root.glob(a.pattern))
assert files, 'no worker timing files'
present_columns = set()
for file in files:
    with file.open() as source:
        present_columns.update(next(csv.reader(source)))
for file in files:
    rows = list(csv.DictReader(file.open()))
    assert rows, file
    hashes[file.name] = hashlib.sha256(file.read_bytes()).hexdigest()
    for row in rows:
        row.setdefault('policy', '0')  # Original forced-growth receipts.
        row.setdefault('input_keys', str(int(row['operations'])//8))
        row.setdefault('packet', '0')
        row.setdefault('lookup', '0')
        row.setdefault('growth_policy', '0')
        row.setdefault('slot_family', '0')
        row.setdefault('passes', str(int(row['operations'])//int(row['input_keys'])))
    identity = {(r['K'], r['kind'], r['W'], r['policy'], r['input_keys'], r['packet'], r['passes'], r['lookup'], r['growth_policy'], r['slot_family']) for r in rows}
    assert len(identity) == 1
    reps = {int(r['rep']) for r in rows}
    assert reps == set(range(len(reps)))
    expected = {(str(x), str(y), str(z), arm) for x in range(2) for y in range(2)
                for z in reps for arm in ['hierarchy', 'abseil']}
    seen = set()
    for row in rows:
        cell = tuple(row[f] for f in ['partitioned', 'skew', 'rep', 'arm'])
        assert cell not in seen
        seen.add(cell)
        assert int(row['operations']) == int(row['input_keys'])*int(row['passes'])
        assert int(row['operations']) == int(row['work0']) + int(row['work1'])
        assert 1 <= int(row['max_inflight']) <= 2
        if int(row['slot_family']):
            assert int(row['slot_family']) in [14, 28]
            for bits, capacity, pslots, qslots, pbytes, qbytes in [
                    ('A', 'first_capacity', 'p_slots', 'q_slots', 'P', 'Q'),
                    ('Next', 'second_capacity', 'p2_slots', 'q2_slots', 'P2', 'Q2'),
                    ('Last', 'third_capacity', 'p3_slots', 'q3_slots', 'P3', 'Q3')]:
                assert 0 < int(row[bits]) < int(row['K'])
                assert int(row[capacity]) == (1 << int(row[bits])) * int(row[pslots])
                assert int(row['slot_family']) <= int(row[pslots]) <= 32
                assert (5 if row['slot_family']=='14' else 8) <= int(row[qslots]) <= 32
                assert int(row[pbytes]) % 8 == int(row[qbytes]) % 8 == 0
        if row['policy']=='5':
            assert 0 <= int(row['epochs']) <= (3 if int(row['growth_policy']) else 2)
            if row['arm']=='hierarchy':
                assert (int(row['epochs'])==0) == (max(int(row['keys0']), int(row['keys1']))<=256)
                assert 0 <= int(row['partial_prefixes']) <= int(row['partial_returns']) <= 2
                assert 0 <= int(row['retried']) <= 128
            else:
                assert int(row['epochs'])==int(row['partial_returns'])==int(row['retried'])==0
        elif int(row['policy']) and row['K'] == '8':
            assert int(row['epochs']) == 0
        elif int(row['policy']):
            assert 0 <= int(row['epochs']) <= 2
            if max(int(row['keys0']),int(row['keys1'])) > 256:
                assert int(row['epochs']) >= 1
        else:
            assert 1 <= int(row['epochs']) <= 2
        assert int(row['cancelled']) == 0
        assert float(row['ns_per_update']) > 0
        if row['arm'] == 'abseil':
            assert int(row['backing']) == -1
        total = sum(float(row[x]) for x in ['setup_ns', 'intake_ns', 'growth_ns', 'retire_ns'])
        assert abs(total - float(row['owner_lifetime_ns'])) < .01
        assert abs((float(row['intake_ns']) + float(row['growth_ns'])) /
                   int(row['operations']) - float(row['ns_per_update'])) < .001
        key = tuple(row[f] for f in ['K', 'kind', 'W', 'policy', 'input_keys', 'packet', 'passes', 'lookup', 'growth_policy', 'slot_family', 'partitioned', 'skew', 'arm'])
        groups.setdefault(key, []).append(row)
    assert seen == expected, (file, seen ^ expected)

summary = []
for key, rows in sorted(groups.items()):
    out = dict(zip(['K', 'kind', 'W', 'policy', 'input_keys', 'packet', 'passes', 'lookup', 'growth_policy', 'slot_family', 'partitioned', 'skew', 'arm'], key))
    # Preserve older retained report schemas. A new attribution control must
    # not change every prior summary when it was absent from the measurements.
    for field in ['lookup', 'growth_policy', 'slot_family']:
        if field not in present_columns: out.pop(field)
    out['samples'] = len(rows)
    for field in ['operations', 'setup_ns', 'intake_ns', 'growth_ns', 'retire_ns',
                  'owner_lifetime_ns', 'ns_per_update', 'logical', 'backing',
                  'queue_bytes', 'epochs', 'offers', 'refused_offers',
                  'max_inflight', 'work0', 'work1', 'scan_work', 'A', 'Next',
                  'P', 'Q', 'inline_bytes', 'first_capacity', 'second_capacity',
                  'inactive_bytes', 'context_bytes', 'pool_metadata',
                  'pool_mapped', 'arena_charged', 'keys0', 'keys1',
                  'partial_returns', 'partial_prefixes', 'retried'] + (
                      ['inspection_bytes'] if any('inspection_bytes' in r for r in rows) else []) + (
                      ['Last', 'third_capacity'] if any('Last' in r for r in rows) else []) + (
                      ['P2', 'Q2', 'P3', 'Q3', 'p_slots', 'q_slots', 'p2_slots', 'q2_slots', 'p3_slots', 'q3_slots'] if 'slot_family' in present_columns else []):
        values = [float(r.get(field, -1)) for r in rows]
        out[field] = statistics.median(values)
        out[field + '_min'] = min(values)
        out[field + '_max'] = max(values)
    summary.append(out)
a.output.mkdir(parents=True, exist_ok=True)
with (a.output / 'summary.csv').open('w') as f:
    writer = csv.DictWriter(f, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
(a.output / 'inputs.json').write_text(json.dumps(hashes, indent=2) + '\n')
print(f'{len(files)} complete processes, {sum(len(v) for v in groups.values())} rows, '
      f'{len(summary)} operation/storage cases; no serial-latency or total-RSS claim')
