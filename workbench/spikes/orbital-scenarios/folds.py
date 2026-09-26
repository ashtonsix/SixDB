"""A concrete L epoch fold: integer deltas with completion-only results.

This is an object algebra probe, separate from the key-protection scheduler.
It has no per-transaction acquire/execute/release transitions. An operation that
returns an intermediate counter value has different semantics; probe() retains
that counterexample as well as the confluent completion-only case.
"""

from itertools import permutations


def combine(left, right):
    changes = dict(left[0])
    for key, delta in right[0].items():
        changes[key] = changes.get(key, 0) + delta
    assert not (left[1] & right[1]), "each contribution appears exactly once"
    return changes, left[1] | right[1]


def trees(items):
    """Every binary grouping for this order; a finite probe, not a scheduler."""
    if len(items) == 1:
        yield items[0]
    for cut in range(1, len(items)):
        for left in trees(items[:cut]):
            for right in trees(items[cut:]):
                yield combine(left, right)


def finish(state, reduction, deferred):
    state = dict(state)
    for key, delta in reduction[0].items():
        state[key] = state.get(key, 0) + delta
    return (tuple(sorted(state.items())), tuple(sorted(reduction[1])), tuple(sorted(deferred)))


def outcomes(state, contributions, protected=()):
    """Choose whole transactions at the boundary, then regroup admitted deltas.

    A retained C1 read or write, or a conflicting provisional reservation,
    excludes deltas on its key. No intermediate values are promised to callers.
    The fixpoint includes completion and deferred IDs, not just object values.
    """
    admitted, deferred = [], []
    for tid, changes in contributions:
        if set(changes) & set(protected):
            deferred.append(tid)
        else:
            admitted.append((changes, frozenset([tid])))
    if not admitted:
        yield finish(state, ({}, frozenset()), deferred)
    for order in permutations(admitted):
        for reduction in trees(order):
            yield finish(state, reduction, deferred)


def probe():
    state = {"x": 10, "y": 20}
    contributions = [("a", {"x": 3}), ("b", {"x": -2, "y": 5}),
                     ("c", {"y": 8}), ("d", {"x": 0, "y": -13}), ("e", {"x": 11})]
    schedules = list(outcomes(state, contributions))
    protected = list(outcomes(state, contributions, {"y"}))
    # Returning the value after each operation makes metadata depend on order,
    # although the final integer counter remains the same.
    observed = set()
    for order in permutations((("a", 1), ("b", 2), ("c", 3))):
        value, results = 0, {}
        for tid, delta in order:
            value += delta
            results[tid] = value
        observed.add((value, tuple(sorted(results.items()))))
    return {"completion_only": {"schedules": len(schedules), "fixpoints": len(set(schedules)),
                                 "result": schedules[0]},
            "protected_y": {"schedules": len(protected), "fixpoints": len(set(protected)),
                            "result": protected[0]},
            "return_intermediate_value": {"schedules": 6, "fixpoints": len(observed),
                                          "examples": sorted(observed)[:2]}}


if __name__ == "__main__":
    import json
    print(json.dumps(probe(), indent=2))
