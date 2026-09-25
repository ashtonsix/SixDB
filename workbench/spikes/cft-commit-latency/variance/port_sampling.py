#!/usr/bin/env python3
"""Paired, equal-budget port policies with training-only selection."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'oneway'))
from analyze import Clock, read_events, write_csv


def read(path):
    return list(csv.DictReader(path.open()))


def select(rows, budget, metric='rtt'):
    eligible = [r for r in rows if r['index'] < budget]
    assert len(eligible) == budget
    return min(eligible, key=lambda r: (r[metric + '_train_worst_us'],
        r[metric + '_train_median_us'], r['port_a']))


def reduce(plan, hosts, pairblocks, directed):
    candidates, comparisons, requests = [], [], []
    for pair in plan['pairs']:
        a, b = pair['node_a'], pair['node_b']
        local = []
        for choice in pair['candidates']:
            flow = choice['canonical_flow']
            rows = [pairblocks[a, b, t * 64 + flow] for t in range(5)]
            rtts = [float(r['rtt_p50_us']) for r in rows]
            row = dict(node_a=a, node_b=b, host_a=hosts[a], host_b=hosts[b],
                same_az=int(rows[0]['az_a']) == int(rows[0]['az_b']),
                az_a=int(rows[0]['az_a']), az_b=int(rows[0]['az_b']),
                **{k: v for k, v in choice.items() if k != 'budgets'},
                rtt_train_worst_us=max(rtts[:3]), rtt_train_median_us=statistics.median(rtts[:3]),
                **{f'rtt_round_{t}_us': v for t, v in enumerate(rtts)},
                rtt_holdout_worst_us=max(rtts[3:]))
            for label, src, dst, train, holdout in [('ab', a, b, [0, 2], 4), ('ba', b, a, [1], 3)]:
                values = [float(directed[src, dst, t * 64 + flow]['p50_us']) for t in train]
                hold = directed[src, dst, holdout * 64 + flow]
                assert int(hold['initiator']) == src
                row.update({f'{label}_train_worst_us': max(values),
                    f'{label}_train_median_us': statistics.median(values),
                    f'{label}_request_holdout_round': holdout,
                    f'{label}_request_holdout_us': float(hold['p50_us']),
                    f'{label}_request_holdout_lo_us': float(hold['p50_lo_us']),
                    f'{label}_request_holdout_hi_us': float(hold['p50_hi_us'])})
            candidates.append(row)
            local.append(row)
        for budget in plan['budgets']:
            arms = {arm: [r for r in local if r['arm'] == arm] for arm in ('sequential', 'scattered')}
            seq, scat = (select(arms[arm], budget) for arm in arms)
            common = {k: seq[k] for k in ('node_a', 'node_b', 'host_a', 'host_b', 'same_az', 'az_a', 'az_b')}
            result = dict(common, budget=budget, primary=budget == plan['primary_budget'])
            for arm, winner in [('sequential', seq), ('scattered', scat)]:
                result.update({f'{arm}_{k}': winner[k] for k in ('port_a', 'flow', 'rtt_train_worst_us',
                    'rtt_round_3_us', 'rtt_round_4_us', 'rtt_holdout_worst_us')})
            for suffix in ('round_3', 'round_4', 'holdout_worst'):
                result[f'{suffix}_gain_us'] = seq[f'rtt_{suffix}_us'] - scat[f'rtt_{suffix}_us']
            comparisons.append(result)
            for label, src, dst in [('ab', a, b), ('ba', b, a)]:
                seq, scat = (select(arms[arm], budget, label) for arm in arms)
                result = dict(common, budget=budget, src=src, dst=dst,
                    host_src=hosts[src], host_dst=hosts[dst], holdout_round=seq[f'{label}_request_holdout_round'])
                for arm, winner in [('sequential', seq), ('scattered', scat)]:
                    result.update({f'{arm}_{k}': winner[k] for k in ('port_a', 'flow')})
                    result[f'{arm}_train_worst_us'] = winner[f'{label}_train_worst_us']
                    for suffix in ('us', 'lo_us', 'hi_us'):
                        result[f'{arm}_holdout_{suffix}'] = winner[f'{label}_request_holdout_{suffix}']
                result['gain_us'] = result['sequential_holdout_us'] - result['scattered_holdout_us']
                # Conservative separate marginal bounds, not a CI or a paired-clock fit.
                result['gain_lo_us'] = result['sequential_holdout_lo_us'] - result['scattered_holdout_hi_us']
                result['gain_hi_us'] = result['sequential_holdout_hi_us'] - result['scattered_holdout_lo_us']
                requests.append(result)
    return candidates, comparisons, requests


def main():
    p = argparse.ArgumentParser()
    p.add_argument('campaign', type=Path)
    p.add_argument('--inputs', type=Path, help='recovered worker folders named by member')
    args = p.parse_args()
    out = args.campaign / 'summary'
    checks = json.loads((out / 'checks.json').read_text())
    point_model = checks.get('clock_point_model', 'affine')
    refs = json.loads((args.campaign / 'workers.json').read_text())
    names, cases, captured = {}, {}, {}
    plan = None
    clock_audit = []
    for name, member in refs['members'].items():
        folder = args.inputs / name if args.inputs else ROOT / 'build/workers' / member['job'] / 'results'
        ident = json.loads((folder / 'identity.json').read_text())
        node = ident['node']
        assert node not in names
        assert name.startswith(ident['az_id']) and node // 2 + 1 == int(ident['az_id'].removeprefix('use1-az'))
        names[node] = name.removeprefix('use1-')
        local_plan = json.loads((folder / 'port-plan.json').read_text())
        assert plan is None or plan == local_plan, 'hosts used different plans'
        plan = local_plan
        assert plan['primary_budget'] == 16 and plan['budgets'] == [4, 16, 32]
        assert plan['training_rounds'] == [0, 1, 2] and plan['holdout_rounds'] == [3, 4]
        if args.inputs:
            config = json.loads((args.campaign / 'capture-config.json').read_text())[name]
        else:
            config = json.loads((folder.parent / 'job.json').read_text())['config']['env']
        captured[name] = {k: v for k, v in config.items() if k.startswith('ONEWAY_')}
        assert config['ONEWAY_PORT_MODE'] == 'port-sampling'
        assert [int(config[k]) for k in ('ONEWAY_NODES', 'ONEWAY_FLOWS', 'ONEWAY_ROUNDS', 'ONEWAY_COUNT', 'ONEWAY_PACE_US')] == [12, 64, 5, 60, 2000]
        reservation = json.loads((folder / 'port-reservation.json').read_text())
        reserved = set()
        for item in reservation['applied'].split(','):
            ends = list(map(int, item.split('-')))
            reserved.update(range(ends[0], ends[-1] + 1))
        assert {48100, *range(49152, 65536)} <= reserved
        for phase in ('initial', 'final'):
            observed = json.loads((folder / f'{phase}.json').read_text())['reserved_ports']
            assert observed['code'] == 0 and observed['stdout'].strip().split(' = ')[1] == reservation['applied']
        execution = json.loads((folder / 'execution.json').read_text())
        assert len(execution) == len(plan['schedule'])
        pair_choices = {(q['node_a'], q['node_b']): q['candidates'] for q in plan['pairs']}
        for actual, expected in zip(execution, plan['schedule']):
            assert all(actual[k] == v for k, v in expected.items()), 'execution order differs from plan'
            pair = next(q for q in plan['matchings'][actual['matching']] if node in q)
            assert pair == [actual['node_a'], actual['node_b']]
            assert actual['canonical_flow'] == pair_choices[tuple(pair)][actual['flow']]['canonical_flow']
        assert all(a['utc'] <= b['utc'] for a, b in zip(execution, execution[1:]))
        local_cases = json.loads((folder / 'cases.json').read_text())
        clock = Clock(folder / 'calibration.jsonl', point_model=point_model)
        edges = [r['app_raw'] for case in (local_cases[0], local_cases[-1]) for r in read_events(folder / case['file'])]
        first, last = min(edges), max(edges)
        times = [first, last, *[t for t in clock.times if first <= t <= last]]
        adjustment = max(abs(clock.point(t)-(clock.fit_intercept+clock.fit_slope*(t-clock.fit_start)/1e9)) for t in times)/1000
        clock_audit.append(dict(host=name, affine_max_violation_us=clock.affine_violation/1000,
            measurement_max_point_adjustment_us=adjustment))
        for case in local_cases:
            a, b = sorted((case['src'], case['dst']))
            key = node, a, b, case['repeat']
            assert key not in cases
            cases[key] = case
            candidate = pair_choices[a, b][case['flow']]
            assert candidate['canonical_flow'] == case['flow']
            assert case['local_port'] == candidate['port_a' if node == a else 'port_b']
            assert case['repeat'] == case['round'] * 64 + case['flow']
            assert case['src'] == (a if case['round'] % 2 == 0 else b)
            assert case['role'] == ('client' if node == case['src'] else 'server')
            assert case['requested'] == case['received'] == plan['count'], 'loss invalidates planned comparison'
    assert set(names) == set(range(plan['nodes']))
    expected = {(q['node_a'], q['node_b'], t * 64 + c['flow']) for q in plan['pairs']
        for c in q['candidates'] if c['flow'] == c['canonical_flow'] for t in range(5)}
    assert set(cases) == {(n, a, b, w) for a, b, w in expected for n in (a, b)}, 'incomplete physical grid'
    raw_pairs = read(out / 'pair-blocks.csv')
    pairblocks = {(int(r['node_a']), int(r['node_b']), int(r['repeat'])): r for r in raw_pairs}
    assert len(pairblocks) == len(raw_pairs) and set(pairblocks) == expected
    raw_directed = read(out / 'blocks.csv')
    directed = {(int(r['src']), int(r['dst']), int(r['repeat'])): r for r in raw_directed}
    assert len(directed) == len(raw_directed) and set(directed) == {(x, y, w) for a, b, w in expected for x, y in ((a, b), (b, a))}
    assert all(int(r['n']) == 40 for r in raw_pairs + raw_directed)
    assert checks['exchanges'] == checks['requested_exchanges'] == len(expected) * 60
    assert all(checks[k] == 0 for k in ('missing_exchanges', 'duplicate_events', 'negative_point_delays', 'negative_upper_delays'))
    candidates, comparisons, requests = reduce(plan, names, pairblocks, directed)
    write_csv(out / 'port-candidates.csv', candidates)
    write_csv(out / 'port-comparisons.csv', comparisons)
    write_csv(out / 'port-request-comparisons.csv', requests)
    (out / 'capture-config.json').write_text(json.dumps(captured, indent=2) + '\n')
    # Full plan and actual execution stay in the bundle. Candidate rows retain the exact logical plan.
    (out / 'port-plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    design = {k: v for k, v in plan.items() if k not in ('pairs', 'schedule', 'matchings')}
    design.update(full_plan_sha256=hashlib.sha256((out / 'port-plan.json').read_bytes()).hexdigest(),
        physical_tuples=len(expected) // 5, logical_candidates=len(candidates),
        complete_grid=True, port_reservations_verified=True, actual_order_verified=True,
        iid_or_stationarity_assumed=False, clock_bounds_are_not_confidence_intervals=True,
        directional_point_model=checks.get('clock_point_model', 'affine'))
    (out / 'port-design.json').write_text(json.dumps(design, indent=2) + '\n')
    (out / 'clock-model.json').write_text(json.dumps(dict(point_model=point_model,
        chosen_after_affine_validation_failed=point_model == 'feasible' and any(r['affine_max_violation_us'] > .001 for r in clock_audit),
        packet_delays_used_for_fit=False, reference_bounds_and_rate_envelope_unchanged=True,
        hosts=clock_audit), indent=2) + '\n')
    totals = []
    for budget in plan['budgets']:
        rows = [r for r in comparisons if r['budget'] == budget and not r['same_az']]
        gains = [r['holdout_worst_gain_us'] for r in rows]
        totals.append(dict(budget=budget, host_pairs=len(rows), median_gain_us=statistics.median(gains),
            min_gain_us=min(gains), max_gain_us=max(gains),
            scattered_better_by_5us=sum(v > 5 for v in gains), sequential_better_by_5us=sum(v < -5 for v in gains),
            scattered_better_by_5us_both_rounds=sum(r['round_3_gain_us'] > 5 and r['round_4_gain_us'] > 5 for r in rows),
            sequential_better_by_5us_both_rounds=sum(r['round_3_gain_us'] < -5 and r['round_4_gain_us'] < -5 for r in rows)))
    write_csv(out / 'port-totals.csv', totals)
    print(json.dumps(totals, indent=2))


if __name__ == '__main__':
    main()
