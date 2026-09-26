#!/usr/bin/env python3
"""Sensitivity contrasts for the shared contention simulator, not benchmarks."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from comparison import Simulation, source_identity
from comparison_inputs import cases


def variants():
    fixtures = {c['name']: c for c in cases(seed=7, width=64)}
    for name in ('bulk_update_reports', 'bulk_update_point_updates', 'blind_bulk_readers',
                 'wan_reports_fresh', 'ordinary_distributed_hot', 'ordinary_distributed_cold',
                 'broad_read_writers_inside'):
        for policy in ('arbitration', 'wound-wait', 'wait-die'):
            case = deepcopy(fixtures[name])
            for tx in case['transactions']:
                tx['early_writes'] = False
            yield 'late-output-protection', case, policy, {'early_writes': False}
    for name in ('wan_reports_fresh', 'bulk_update_point_updates', 'ordinary_distributed_hot'):
        for policy in ('certification', 'snapshot-wait', 'ordered-writes'):
            for limit in (1, 16):
                case = deepcopy(fixtures[name])
                case['retry_limit'] = limit
                yield f'attempts-{limit}', case, policy, {'retry_limit': limit}
            for delay in (1, 15):
                case = deepcopy(fixtures[name])
                case['retry_delay'] = delay
                yield f'retry-delay-{delay}', case, policy, {'retry_delay': delay}
    for budget in (20, 800):
        for name in ('wan_reports_fresh', 'partial_claim_bridge', 'narrow_conflicting_wan'):
            case = deepcopy(fixtures[name])
            case['read_wait_ticks'] = budget
            yield f'capture-wait-{budget}', case, 'snapshot-wait', {'read_wait_ticks': budget}
    for name in ('ordinary_distributed_hot', 'wan_reports_fresh'):
        for delay in (1, 15):
            case = deepcopy(fixtures[name])
            case['retry_delay'] = delay
            yield f'retry-delay-{delay}', case, 'wait-die', {'retry_delay': delay}
    for delay in (0, 50):
        for name in ('ordinary_distributed_hot', 'bulk_update_reports', 'broad_read_writers_inside'):
            case = deepcopy(fixtures[name])
            case['arbitration_delay'] = delay
            yield f'verdict-delay-{delay}', case, 'arbitration', {'arbitration_delay': delay}
    for solver in ('work', 'bounded'):
        for name in ('ordinary_distributed_hot', 'bulk_update_point_updates', 'wan_reports_fresh'):
            case = deepcopy(fixtures[name])
            case['solver'] = solver
            yield f'solver-{solver}', case, 'arbitration', {'solver': solver}
    for name in ('ordinary_distributed_hot', 'narrow_conflicting_wan'):
        for policy in ('arbitration', 'wound-wait', 'wait-die', 'certification', 'snapshot-wait', 'ordered-writes'):
            case = deepcopy(fixtures[name])
            case['horizon'] *= 4
            yield 'long-drain', case, policy, {'horizon': case['horizon']}
    for name in ('bulk_update_reports', 'broad_max_arrivals', 'blind_bulk_readers'):
        yield 'fixed-position-ablation', fixtures[name], 'fixed-position-control', {}
    for name in ('wan_reports_fresh', 'bulk_update_point_updates'):
        case = deepcopy(fixtures[name])
        case['horizon'] *= 4
        yield 'long-drain', case, 'wound-wait', {'horizon': case['horizon']}
    for name in ('wan_reports_fresh', 'narrow_conflicting_wan'):
        for delay in (5, 25, 100):
            for policy in ('arbitration', 'wait-die', 'certification', 'snapshot-wait', 'ordered-writes'):
                case = deepcopy(fixtures[name])
                case.update(link_delay=delay, horizon=4000)
                yield f'link-{delay}', case, policy, {'link_delay': delay, 'horizon': 4000}
        for policy in ('certification', 'snapshot-wait', 'ordered-writes'):
            case = deepcopy(fixtures[name])
            case.update(link_delay=100, horizon=4000, read_wait_ticks=2000)
            yield 'link-100-capture-age-2000', case, policy, {
                'link_delay': 100, 'horizon': 4000, 'read_wait_ticks': 2000}
    for capacity in (12, 24):
        for policy in ('arbitration', 'wound-wait', 'wait-die', 'certification', 'snapshot-wait', 'ordered-writes'):
            case = deepcopy(fixtures['rare_wan_saturated_local'])
            case['capacity'] = capacity
            yield f'regional-capacity-{capacity}', case, policy, {'capacity': capacity}
    for capacity in (12, 16, 24):
        for policy in ('arbitration', 'wound-wait', 'wait-die', 'certification', 'snapshot-wait', 'ordered-writes'):
            case = deepcopy(fixtures['rare_wan_saturated_local'])
            case['capacity'] = capacity
            case['transactions'] = [tx for tx in case['transactions'] if tx['group'] != 'wan']
            yield f'without-wan-capacity-{capacity}', case, policy, {'capacity': capacity, 'remove_cohort': 'wan'}
    wide = {c['name']: c for c in cases(seed=7, width=256)}
    for name in ('wan_reports_fresh', 'bulk_update_reports', 'bulk_update_point_updates'):
        for policy in ('arbitration', 'wound-wait', 'wait-die', 'occ', 'certification', 'snapshot-wait', 'ordered-writes'):
            for field in ('horizon', 'capacity'):
                case = deepcopy(wide[name])
                case[field] *= 4
                yield f'wide-{field}', case, policy, {'width': 256, field: case[field]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    before = source_identity()
    runner_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    rows = []
    for variant, case, policy, changes in variants():
        row = Simulation(case, policy).run()
        row.update(variant=variant, changes=changes, seed=7, width=changes.get('width', 64))
        rows.append(row)
    assert before == source_identity()
    assert runner_hash == hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result = {'source_sha256': before, 'runner_sha256': runner_hash, 'rows': rows,
              'assumptions': 'Synthetic shared simulator; contrasts against corresponding seed7 width64/256 defaults; all changed parameters recorded; not calibrated system performance.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'Wrote {len(rows)} sensitivity runs to {args.output}')


if __name__ == '__main__':
    main()
