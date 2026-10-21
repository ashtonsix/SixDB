#!/usr/bin/env python3
"""Audit fixed-multiset hot reads and report unscaled same-thread PMU counts."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import statistics

p = argparse.ArgumentParser()
p.add_argument('root', type=Path)
p.add_argument('--speculation', action='store_true', help='Paired barrier cohort')
a = p.parse_args()
records = json.loads((a.root/'runs.json').read_text())
assert len(records) == (324 if a.speculation else 864)
answers, hashes = {}, {}
counts = collections.Counter()
statuses = collections.Counter()
groups = collections.defaultdict(list)
event_names = ['cpu_cycles', 'retired_instructions', 'branches', 'branch_misses']
for record in records:
    assert record['exit'] == 0
    binary = a.root.parent/'binaries'/Path(record['command'][3]).name
    if binary not in hashes: hashes[binary] = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert hashes[binary] == record['binary_sha256']
    rows = list(csv.DictReader((a.root/record['file']).open()))
    reads = [r for r in rows if r['phase'] == 'read']
    counters = [json.loads(line) for line in (a.root/record['counters']).read_text().splitlines()]
    assert len(reads) == len(counters) == 3
    identity = tuple(record[k] for k in ['w','v','impl','miss','order','sink'])
    counts[identity] += 1
    build, = [r for r in rows if r['phase'] == 'build']
    for row, pmu in zip(reads,counters):
        assert int(row['operations']) == 1_000_000 and float(row['ns']) > 0
        assert int(row['miss']) == record['miss'] and int(row['query_order']) == record['order']
        assert int(row['hits_only']) == record['sink'] and int(row['prefetch']) == 0
        answer = tuple(int(row[k]) for k in ['hits','keys','values'])
        key = (record['w'],record['v'],record['seed'],record['miss'],record['sink'])
        assert answers.setdefault(key,answer) == answer, (record, answer)
        if record['sink']: assert answer[1:] == (0,0)
        if record['miss'] == 0: assert answer[0] == 1_000_000
        if record['miss'] == 100: assert answer == (0,0,0)
        statuses[pmu['status']] += 1
        events = {e['name']:e for e in pmu['events']}
        sample = dict(ns=float(row['ns']),bytes=int(row['bytes']),spill=int(build['spill']),
                      seed=record['seed'],coverage=pmu['coverage'])
        for name in event_names:
            event = events.get(name,{})
            count = event.get('count')
            if count is not None:
                assert event['status'] == 'measured' and not pmu['multiplexed']
                assert pmu['coverage'] == 1
                sample[name] = count / int(row['operations'])
            else: sample[name] = None
        groups[identity].append(sample)
assert len(counts) == (54 if a.speculation else 144) and set(counts.values()) == {6}
with (a.root/'summary.csv').open('w') as out:
    fields = ['w','v','impl','miss','order','sink','ns','mad','mops','samples','processes',
              'seeds','bytes','spill_median',*event_names,'minimum_coverage']
    writer=csv.DictWriter(out,fieldnames=fields,lineterminator='\n');writer.writeheader()
    for key, rr in sorted(groups.items()):
        ns=statistics.median(r['ns'] for r in rr)
        row=dict(zip(fields[:6],key),ns=ns,mad=statistics.median(abs(r['ns']-ns) for r in rr),
                 mops=1000/ns,samples=len(rr),processes=counts[key],
                 seeds=len({r['seed'] for r in rr}),bytes=statistics.median(r['bytes'] for r in rr),
                 spill_median=statistics.median(r['spill'] for r in rr))
        for name in event_names:
            values=[r[name] for r in rr]
            assert all(x is not None for x in values) or all(x is None for x in values)
            row[name]=statistics.median(values) if values[0] is not None else ''
        coverage=[r['coverage'] for r in rr if r['coverage'] is not None]
        row['minimum_coverage']=min(coverage) if coverage else ''
        writer.writerow(row)
audit=dict(processes=len(records),read_intervals=sum(len(r) for r in groups.values()),
           result_identities=len(answers),verified_binaries=len(hashes),
           same_answers_across_order_and_implementation=True,pmu_statuses=dict(statuses),
           counts_unscaled=True,loop_only_thread_user_space=True)
(a.root/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps(audit,indent=2))
