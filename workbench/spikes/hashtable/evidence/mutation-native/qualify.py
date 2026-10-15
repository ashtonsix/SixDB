#!/usr/bin/env python3
"""Verify balanced mutation schedules, generated/timed ELFs and retained reports."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys

p = argparse.ArgumentParser(__doc__)
p.add_argument('raw', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--codegen', type=Path)
p.add_argument('--repetitions', type=int, default=5)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
variants = ['control', 'fused', 'partial']
fans = {2: [1, 2, 4, 5, 8, 9, 16, 32], 8: [1, 2, 3, 4, 8, 32], 16: [1, 2, 3, 8, 32]}
schedule = [(w, k, arm, f, large) for w in fans for k in (1, 2) for arm in range(5)
            for f in ([1] if k == 1 else fans[w]) for large in (0, 1)]
random.Random(947).shuffle(schedule)
processes = json.loads((a.raw / 'processes.json').read_text())
probe = json.loads((a.raw / 'probe/receipt.json').read_text())
rows = list(csv.DictReader((a.raw / 'mutation.csv').open()))
assert processes['schedule_seed'] == 947
assert len(schedule) == 220 and len(processes['processes']) == 1320
assert len(rows) == 1320 * a.repetitions * 9
compiled = {(r['variant'], r['width']): r for r in probe['commands'] if r['kind'] == 'bench'}
assert set(compiled) == {(v, w) for v in variants for w in fans}
expected = []
for case, (w, k, arm, f, large) in enumerate(schedule):
    order = variants[case % 3:] + variants[:case % 3]
    for position, variant in enumerate(order + list(reversed(order))):
        expected.append((case, position, variant, w, k, arm, f, large))
for i, (process, entry) in enumerate(zip(processes['processes'], expected)):
    case, position, variant, w, k, arm, f, large = entry
    argv = process['argv']
    assert argv[:2] == ['taskset', '-c']
    assert Path(argv[3]).name == f'values-{w}' and Path(argv[3]).parent.name == variant
    assert argv[4:] == list(map(str, [k, arm, f, large, a.repetitions]))
    assert (process['case'], process['order'], process['variant']) == (case, position, variant)
    assert process['returncode'] == 0 and not process['stderr']
    assert process['ended_unix'] >= process['started_unix']
    if i:
        assert process['started_unix'] >= processes['processes'][i-1]['ended_unix']
    assert processes['binary_sha256'][argv[3]] == compiled[(variant, w)]['binary_sha256']
    chunk = rows[i*a.repetitions*9:(i+1)*a.repetitions*9]
    assert {(int(r['case']), int(r['order']), r['variant'], int(r['value_bytes']),
             int(r['kind']), int(r['arm']), int(r['fanout']), int(r['A'])) for r in chunk} == {
                 (case, position, variant, w, k, arm, f, 10 if large else 6)}

# Use the report copied from this frozen native source. It checks each complete
# operation, repetition and inventory equality across all generated variants.
report = Path(__file__).with_name('report.py')
subprocess.run([sys.executable, str(report), str(a.raw/'mutation.csv'),
                '--output', str(a.output/'summary.json')], check=True, stdout=subprocess.DEVNULL)
reproduced = json.loads((a.output/'summary.json').read_text())
if (a.raw/'summary.json').exists():
    archived = json.loads((a.raw/'summary.json').read_text())
    assert reproduced == archived, 'archived report differs from frozen-source reproduction'
else:
    assert (a.output/'summary.csv').read_bytes() == (a.raw/'summary.csv').read_bytes(), 'archived CSV report differs'
assert reproduced['rows'] == len(rows)
audits = {}
for (variant, w), entry in compiled.items():
    path = (a.codegen/variant/f'W{w}'/'codegen-summary.json' if a.codegen else
            a.raw/f'codegen-{variant}-W{w}.json')
    audit = json.loads(path.read_text())
    assert audit['qualified'] and audit['timed_regions'] == 80 and audit['read_regions'] == 60
    assert audit['update_regions'] == 20 and not audit['update_callgraph_truncated']
    assert audit['variant'] == variant and audit['value_bytes'] == w
    assert audit['binary_sha256'] == entry['binary_sha256']
    assert audit['source_sha256'] == entry['source_sha256']
    for name, field in [('promoted.h', 'promoted_sha256'), ('record_table.h', 'record_table_sha256')]:
        if field in entry:
            assert audit['generated_header_sha256'][name] == entry[field]
    audits[f'{variant}/W{w}'] = {key: audit[key] for key in [
        'binary_sha256', 'source_sha256', 'generated_header_sha256', 'probe_receipt_sha256',
        'read_locations', 'update_locations', 'repeated_update_boundary_categories']}

# Keep process-order medians individually, so unchanged controls and spreads
# remain reviewable instead of disappearing into a median across both runs.
from collections import defaultdict
from statistics import median
groups = defaultdict(list)
for row in rows:
    key = tuple(row[k] for k in ['case', 'order', 'variant', 'value_bytes', 'kind', 'A', 'fanout', 'arm', 'phase'])
    groups[key].append(float(row['ns']) / int(row['operations']))
with (a.output/'order-medians.csv').open('w') as f:
    writer = csv.writer(f)
    writer.writerow(['case', 'order', 'variant', 'value_bytes', 'kind', 'A', 'fanout', 'arm', 'phase', 'median', 'min', 'max'])
    for key, samples in sorted(groups.items()):
        assert len(samples) == a.repetitions
        writer.writerow([*key, median(samples), min(samples), max(samples)])
coverage = dict(rows=len(rows), process_cases=len(schedule), processes=len(expected),
                repetitions_per_process=a.repetitions, forward_reverse_runs_per_variant=2,
                read_regions=540, update_regions=180, report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),
                source_elf_audits=audits, checks=[
                    'exact seed947 case schedule and forward/reverse variant order',
                    'all argv, return codes, stderr, serialized timestamps and raw process chunks',
                    'timed ELF equals generated probe receipt and audited ELF for every variant/width',
                    'archived summary exactly reproduced from captured report source',
                    'full physical inventories and populations equal across variants'],
                limitations=[
                    'CFG recurrence is not measured call frequency or cost.',
                    'Fresh processes share one machine; repetitions are not independent machine trials.',
                    'Unchanged-source Abseil controls still vary in placement and runtime seed.',
                    'Population fields describe build; memory snapshots exclude transient peaks.'])
(a.output/'coverage.json').write_text(json.dumps(coverage, indent=2)+'\n')
print(f'Qualified {len(rows)} rows, {len(expected)} processes, nine generated/timed ELF identities')
