#!/usr/bin/env python3
"""Separate construction/growth/release, reciprocal/serial reads and sampled bytes."""
import argparse
import csv
import hashlib
import json
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('root', type=Path)
a = p.parse_args()
groups, hashes = {}, {}
files = sorted(a.root.glob('hashtable_design_cycle_*.csv'))
files = [f for f in files if not f.name.endswith('-trace.csv')]
assert files
identity = ['K', 'kind', 'W', 'slots', 'step', 'load', 'arm', 'seed', 'keys', 'fanout']
if any('allocating_oracle' in next(csv.reader(f.open())) for f in files):
    identity.append('allocating_oracle')
metrics = ['operations', 'build_ns', 'growth_ns', 'retire_ns', 'hit_ns', 'serial_ns',
           'migrations', 'level', 'moved', 'scan_work', 'logical', 'backing', 'metadata',
           'observed_coexist_logical', 'observed_coexist_backing', 'sampled_mean_logical',
           'read_graph_bytes', 'control_objects', 'build_ns_per_update', 'lifetime_ns_per_update']
for file in files:
    rows = list(csv.DictReader(file.open()))
    assert rows
    for row in rows:row.setdefault('allocating_oracle','1')
    hashes[file.name] = hashlib.sha256(file.read_bytes()).hexdigest()
    trace_file = file.with_name(file.stem + '-trace.csv')
    traces = list(csv.DictReader(trace_file.open()))
    hashes[trace_file.name] = hashlib.sha256(trace_file.read_bytes()).hexdigest()
    shape = {(r['K'], r['kind'], r['W'], r['slots'], r['step'], r['load'], r['seed'], r['keys']) for r in rows}
    assert len(shape) == 1
    reps = {int(r['rep']) for r in rows}
    assert reps == set(range(len(reps)))
    fanouts = [1, 4] if rows[0]['kind'] == '2' else [1]
    expected = {(str(arm), str(fanout), str(rep)) for arm in range(3) for fanout in fanouts for rep in reps}
    seen = set()
    for r in rows:
        cell = r['arm'], r['fanout'], r['rep']
        assert cell not in seen
        seen.add(cell)
        assert int(r['operations']) == int(r['keys']) * int(r['fanout'])
        assert 0 <= float(r['growth_ns']) <= float(r['build_ns'])
        assert float(r['hit_ns']) > 0 and float(r['serial_ns']) > 0
        if r['arm'] != '0':
            assert all(int(r[k]) == -1 for k in ['backing', 'observed_coexist_logical', 'observed_coexist_backing'])
            assert int(r['migrations']) == int(r['moved']) == int(r['scan_work']) == 0
        elif r['K'] == '8':
            assert int(r['migrations']) == int(r['level']) == 0
        else:
            assert int(r['level']) == int(r['migrations'])
            assert int(r['scan_work']) >= int(r['moved'])
        tt = [t for t in traces if (t['arm'], t['fanout']) == cell[:2]]
        assert tt and int(tt[-1]['accepted']) == int(r['operations'])
        assert tt[-1]['keys'] == r['keys'] and tt[-1]['logical'] == r['logical']
        area = previous = 0
        for t in tt:
            accepted = int(t['accepted'])
            assert previous <= accepted and 0 <= int(t['keys']) <= int(r['keys'])
            area += (accepted-previous) * int(t['logical'])
            previous = accepted
        assert abs(area/int(r['operations'])-float(r['sampled_mean_logical'])) < .001
        r['build_ns_per_update'] = float(r['build_ns'])/int(r['operations'])
        r['lifetime_ns_per_update'] = (float(r['build_ns'])+float(r['retire_ns']))/int(r['operations'])
        groups.setdefault(tuple(r[k] for k in identity), []).append(r)
    assert seen == expected
summary = []
for key, rows in sorted(groups.items()):
    out = dict(zip(identity, key));out['samples'] = len(rows)
    for metric in metrics:
        values = [float(r[metric]) for r in rows]
        out[metric] = statistics.median(values)
        out[metric+'_min'] = min(values);out[metric+'_max'] = max(values)
    summary.append(out)
with (a.root/'summary.csv').open('w') as f:
    w = csv.DictWriter(f, fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
(a.root/'inputs.json').write_text(json.dumps(hashes, indent=2)+'\n')
print(f'{len(files)} complete processes, {sum(len(v) for v in groups.values())} timing rows, '
      f'{len(groups)} qualified cells with matching independent memory traces')
