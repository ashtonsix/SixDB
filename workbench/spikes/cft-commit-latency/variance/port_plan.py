#!/usr/bin/env python3
"""Reproducible sampling policies; logical candidate order is not execution order."""
import random

SEED = 250926
BUDGETS = (4, 16, 32)
FLOWS = 64
ROUNDS = 5
COUNT = 60
PACE_US = 2000


def matchings(nodes):
    roster = list(range(nodes))
    result = []
    for _ in range(nodes - 1):
        result.append([sorted(pair) for pair in zip(roster[:nodes // 2], reversed(roster[nodes // 2:]))])
        roster = [roster[0], roster[-1]] + roster[1:-1]
    return result


def make_plan(nodes=12, seed=SEED):
    pairs = []
    for a in range(nodes):
        for b in range(a + 1, nodes):
            # Separate streams keep both policies independent, including accidental overlap.
            start = random.Random(f'{seed}:{a}:{b}:sequential').randint(49152, 65504)
            scattered = random.Random(f'{seed}:{a}:{b}:scattered').sample(range(49152, 65536), 32)
            candidates = []
            first_flow = {}
            for flow, port in enumerate(list(range(start, start + 32)) + scattered):
                canonical = first_flow.setdefault(port, flow)
                candidates.append(dict(flow=flow, canonical_flow=canonical,
                    arm='sequential' if flow < 32 else 'scattered', index=flow % 32,
                    port_a=port, port_b=48100,
                    budgets=[budget for budget in BUDGETS if flow % 32 < budget]))
            pairs.append(dict(node_a=a, node_b=b, candidates=candidates))
    schedule = []
    for repeat in range(ROUNDS):
        rng = random.Random(f'{seed}:order:{repeat}')
        units = [(m, k) for m in range(nodes - 1) for k in range(32)]
        rng.shuffle(units)
        for m, k in units:
            arms = [k, k + 32]
            rng.shuffle(arms)
            for flow in arms:
                schedule.append(dict(round=repeat, matching=m, flow=flow))
    return dict(version=1, seed=seed, nodes=nodes, rounds=ROUNDS, count=COUNT,
                warmup=20, pace_us=PACE_US, budgets=list(BUDGETS), primary_budget=16,
                training_rounds=[0, 1, 2], holdout_rounds=[3, 4],
                score='minimize worst training RTT p50; tie median training RTT p50, then port_a',
                overlap='one physical measurement per tuple/round, shared between logical candidates',
                matchings=matchings(nodes), pairs=pairs, schedule=schedule)


if __name__ == '__main__':
    import json
    print(json.dumps(make_plan(), indent=2))
