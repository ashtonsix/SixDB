#!/usr/bin/env python3
"""Two private writers, optionally compared with ordinary shared concurrent maps."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

ARMS = ('abseil_partition', 'fuller_partition', 'abseil_merge', 'fuller_merge',
        'fuller_rebuild', 'fuller_roomy')
SHARED_ARMS = ('boost_shared', 'gtl_shared')
UNAVAILABLE_SHARED = ('before_requested', 'coexist_requested', 'retained_requested',
                      'coexist_capacity', 'retained_capacity')
HISTORIES = ('distinct', 'repeated', 'skewed')


def summarize(path, n):
    rows = {}
    for raw in csv.DictReader(path.open()):
        arm, history = raw['arm'], raw['history']
        assert arm in ARMS + SHARED_ARMS and history in HISTORIES
        data = {k: None if v == '' else float(v) if k.endswith('_ns') else int(v)
                for k, v in raw.items() if k not in ('arm', 'history')}
        shared = arm in SHARED_ARMS
        assert {k for k, v in data.items() if v is None} == (set(UNAVAILABLE_SHARED) if shared else set())
        key = arm, history, data['rep']
        assert key not in rows and data['n'] == n
        assert data['rows'] == n * (1 if history == 'distinct' else 16)
        assert data['cpu0'] != data['cpu1'] and data['receiver'] in (0, 1)
        assert all(math.isfinite(v) and v >= 0 for v in data.values() if v is not None)
        assert data['intake_ns'] >= max(data['local0_ns'], data['local1_ns'])
        assert data['finish_ns'] >= max(data['finish0_ns'], data['finish1_ns'])
        if not shared:
            assert data['coexist_requested'] >= data['retained_requested'] > 0
            assert data['coexist_capacity'] >= data['retained_capacity']
        partitioned = arm.endswith('partition')
        assert data['routed'] == (data['misroutes'] if partitioned else 0)
        if shared:
            assert all(data[k] == 0 for k in ('receiver', 'added', 'routing_capacity', 'finish_ns',
                'retire_ns', 'finish0_ns', 'finish1_ns', 'primary', 'secondary', 'chain',
                'primary_bits', 'secondary_bits', 'primary_slots', 'secondary_slots'))
            assert data['coexist_metadata'] == data['retained_metadata'] > 0
        elif partitioned:
            assert data['added'] == 0 and data['coexist_requested'] == data['retained_requested']
        else:
            assert data['added'] == n - data['local' + str(data['receiver'])]
        if arm.startswith('fuller'):
            assert data['primary'] + data['secondary'] + data['chain'] == n
            assert data['primary_slots'] and data['secondary_slots']
        rows[key] = data
    reps = sorted({k[2] for k in rows})
    assert reps and reps == list(range(len(reps)))
    arms = ARMS + SHARED_ARMS if any(k[0] in SHARED_ARMS for k in rows) else ARMS
    assert rows.keys() == {(a, h, r) for a in arms for h in HISTORIES for r in reps}
    for history in HISTORIES:
        for rep in reps:
            samples = [rows[a, history, rep] for a in arms]
            for field in ('local0', 'local1', 'misroutes', 'cpu0', 'cpu1'):
                assert len({x[field] for x in samples}) == 1
            # Same source order and receiver for the reconstruction control.
            for field in ('receiver', 'added', 'primary', 'secondary', 'chain',
                          'primary_bits', 'secondary_bits', 'before_requested', 'retained_requested'):
                assert rows['fuller_merge', history, rep][field] == rows['fuller_rebuild', history, rep][field]
    out = []
    for history in HISTORIES:
        for arm in arms:
            samples = [rows[arm, history, r] for r in reps]
            def med(f):
                values = [f(x) for x in samples]
                assert all(x is None for x in values) or all(x is not None for x in values)
                return None if values[0] is None else statistics.median(values)
            # Pools and worker threads are reusable runtime context. Report their
            # lifetime separately; include all barriers around table phases.
            total = lambda x: sum(x[k] for k in ('setup_ns', 'intake_ns', 'finish_ns', 'retire_ns', 'release_ns'))
            result = dict(arm=arm, history=history, n=n, repetitions=len(reps),
                intake_ns_per_row=med(lambda x: x['intake_ns']/x['rows']),
                local_work_ns_per_row=med(lambda x: max(x['local0_ns'], x['local1_ns'])/x['rows']),
                finish_ns=med(lambda x: x['finish_ns']),
                finish_work_ns=med(lambda x: max(x['finish0_ns'], x['finish1_ns'])),
                ready_ns_per_row=med(lambda x: (x['setup_ns']+x['intake_ns']+x['finish_ns']+x['retire_ns'])/x['rows']),
                lifecycle_ns_per_row=med(lambda x: total(x)/x['rows']),
                min_lifecycle_ns_per_row=min(total(x)/x['rows'] for x in samples),
                max_lifecycle_ns_per_row=max(total(x)/x['rows'] for x in samples),
                read_ns_per_key=med(lambda x: x['read_ns']/n),
                coexist_requested_plus_routing=med(lambda x: None if x['coexist_requested'] is None
                    else x['coexist_requested']+x['routing_capacity']))
            if arm in SHARED_ARMS:
                result['quiescent_read'] = 'protected cvisit' if arm == 'boost_shared' else 'unlocked if_contains_unsafe'
            for k in ('before_requested', 'coexist_requested', 'retained_requested', 'coexist_capacity',
                      'retained_capacity', 'coexist_metadata', 'retained_metadata', 'routing_capacity',
                      'primary', 'secondary', 'chain', 'runtime_ns', 'drain_ns'):
                result[k] = med(lambda x: x[k])
            out.append(result)
    return dict(input=str(path), rows=len(rows), summaries=out,
        scope='Two OS threads; private arms have owner-affine Orbital Pools. No concurrent reader visibility claim. '
              'Partitioned intake includes queue writes; finish includes exception intake or merge and barriers. '
              'Same-layout seeded merge only; distinct locals expose actual chain expansion. '
              'Roomy arm reserves union geometry on both workers. Repeated/skewed merge locals already need '
              'that level and roomy repeats that control. Baseline reserves actual local distinct cardinality. '
              'Coexistence is sampled live storage, not transient relocation peak or RSS. Routing capacity '
              'is separate. Read is warm, after source retirement. Runtime creation and post-release '
              'join wait (not full physical Pool drain duration) are separate. Optional Boost/GTL arms use '
              'one ordinary shared owner, constructed/reserved/first-touched and destroyed by worker 0; '
              'both workers add their unchanged intake directly. They reserve known union cardinality. '
              'Result is complete after intake, with no artificial finish/retire phase. Allocation inventories '
              'are unavailable (null), not zero; only owner metadata is exact. Boost quiescent reads retain '
              'cvisit protection; GTL uses its explicitly quiescent unlocked lookup. This bridge is K40 maps, '
              'not all-key-width/container multi-writer coverage. Lifecycle excludes separately reported reads.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path);parser.add_argument('--n', type=int, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args();result = summarize(args.csv, args.n)
    if args.output: args.output.write_text(json.dumps(result, indent=2)+'\n')
    for h in HISTORIES:
        cells = [x for x in result['summaries'] if x['history'] == h]
        print(h + ': ' + ' '.join(f"{x['arm']} {x['lifecycle_ns_per_row']:.3f}" for x in cells))
    print(f"Validated {result['rows']} rows; complete table lifecycle ns/input row above")
