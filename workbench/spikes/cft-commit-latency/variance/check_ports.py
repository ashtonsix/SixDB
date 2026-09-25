#!/usr/bin/env python3
"""Check the sampling design, nested budgets, schedule and overlap semantics."""
from collections import Counter
from port_plan import make_plan
from port_sampling import reduce, select


def check_selection():
    plan = make_plan(nodes=2)
    pairblocks, directed = {}, {}
    for choice in plan['pairs'][0]['candidates']:
        f = choice['canonical_flow']
        for t in range(5):
            # RTT training favors port index 15, then 20 when budget permits;
            # held-out measurements deliberately favor a different candidate.
            training = {15: 90, 20: 80, 40: 85}.get(f, 1000 + f)
            pairblocks[0, 1, t * 64 + f] = dict(az_a=1, az_b=2,
                rtt_p50_us=training if t < 3 else (2000 if f == 15 else 10 + f))
            for a, b in ((0, 1), (1, 0)):
                v = (1 if f == (3 if a == 0 else 35) else 300 + f) if t < 3 else 500 + 100 * t + a
                directed[a, b, t * 64 + f] = dict(p50_us=v, p50_lo_us=v - 2, p50_hi_us=v + 2,
                    initiator=0 if t % 2 == 0 else 1)
    candidates, comparisons, requests = reduce(plan, {0: 'az1a', 1: 'az2a'}, pairblocks, directed)
    primary = next(r for r in comparisons if r['budget'] == 16)
    assert primary['sequential_flow'] == 15 and primary['scattered_flow'] == 40
    assert primary['round_3_gain_us'] == 1950 and primary['round_4_gain_us'] == 1950
    large = next(r for r in comparisons if r['budget'] == 32)
    assert large['sequential_flow'] == 20
    ab = next(r for r in requests if r['budget'] == 16 and r['src'] == 0)
    ba = next(r for r in requests if r['budget'] == 16 and r['src'] == 1)
    assert ab['sequential_flow'] == 3 and ab['holdout_round'] == 4 and ab['sequential_holdout_us'] == 900
    assert ba['scattered_flow'] == 35 and ba['holdout_round'] == 3 and ba['scattered_holdout_us'] == 801
    assert ab['gain_lo_us'] == -4 and ab['gain_hi_us'] == 4
    rows = [dict(index=i, rtt_train_worst_us=10, rtt_train_median_us=9 if i == 2 else 10,
                 port_a=50010-i) for i in range(4)]
    assert select(rows, 4)['index'] == 2
    rows[2]['rtt_train_median_us'] = 10
    assert select(rows, 4)['index'] == 3


def main():
    plan = make_plan()
    assert plan == make_plan()
    assert plan != make_plan(seed=250927)
    pairs = {(p['node_a'], p['node_b']) for p in plan['pairs']}
    assert len(pairs) == 66
    assert Counter(tuple(p) for m in plan['matchings'] for p in m) == Counter({p: 1 for p in pairs})
    overlap = 0
    for pair in plan['pairs']:
        candidates = pair['candidates']
        seq, scattered = candidates[:32], candidates[32:]
        assert [r['port_a'] for r in seq] == list(range(seq[0]['port_a'], seq[0]['port_a'] + 32))
        assert len({r['port_a'] for r in scattered}) == 32
        assert all(49152 <= r['port_a'] <= 65535 and r['port_b'] == 48100 for r in candidates)
        for budget in (4, 16, 32):
            assert all(sum(budget in r['budgets'] for r in arm) == budget for arm in (seq, scattered))
        for r in candidates:
            canonical = candidates[r['canonical_flow']]
            assert canonical['port_a'] == r['port_a'] and canonical['canonical_flow'] == canonical['flow']
            overlap += r['canonical_flow'] != r['flow']
    assert overlap > 0, 'fixture should exercise duplicate physical tuples'
    for repeat in range(5):
        order = [s for s in plan['schedule'] if s['round'] == repeat]
        assert Counter((s['matching'], s['flow']) for s in order) == Counter({(m, f): 1 for m in range(11) for f in range(64)})
        for first, second in zip(order[::2], order[1::2]):
            assert first['matching'] == second['matching']
            assert abs(first['flow'] - second['flow']) == 32
    check_selection()
    print(f'port design and holdout-selection checks passed; {overlap} shared physical tuples')


if __name__ == '__main__':
    main()
