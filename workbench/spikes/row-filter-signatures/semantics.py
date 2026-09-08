"""Small-domain evidence and mutation models, independently of native kernels."""
from bisect import bisect_right
from itertools import combinations, product
import json
import sys


def sql_and(a, b):
    return False if a is False or b is False else None if a is None or b is None else True


def sql_or(a, b):
    return True if a is True or b is True else None if a is None or b is None else False


def expr(a):
    return sql_or(sql_and(a[0], a[1]), a[2])


def classify(code, boundaries, threshold):
    low = boundaries[code-1] if code else float('-inf')
    high = boundaries[code]-1 if code < len(boundaries) else float('inf')
    return low > threshold, high > threshold


def run():
    counts = {'boolean': 0, 'rollup': 0, 'range': 0, 'history': 0}
    # Any unrepresented or unresolved atom has lower=False and upper=True.
    for a in product((False, True, None), repeat=3):
        for known in product((False, True), repeat=3):
            lower = [bool(k and x is True) for k, x in zip(known, a)]
            upper = [x is True if k else True for k, x in zip(known, a)]
            truth = expr(a) is True
            assert not expr(lower) or truth
            assert not truth or expr(upper)
            counts['boolean'] += 1
    assert expr((False, False, True)) is True
    assert (None is True) is False and sql_or(None, False) is None
    # Enumerate block patterns: OR, fixed-bit bounds, presence and maximal masks.
    for size in range(1, 4):
        for block in combinations(range(8), size):
            union, common = 0, 7
            for v in block:
                union |= v
                common &= v
            maxima = [v for v in block if not any(v != u and v & u == v for u in block)]
            for q in range(8):
                truth = any(v & q == q for v in block)
                assert not truth or union & q == q
                assert truth == any(v & q == q for v in maxima)
                if q in block:
                    assert q & common == common and q | union == union
                allowed = {v for v in range(8) if v & q == q}
                assert bool(set(block) & allowed) == truth
                counts['rollup'] += 1
    assert (1 | 2) & 3 == 3 and not any(v & 3 == 3 for v in (1, 2))
    # Exact threshold semantics, including empty bins and duplicate cut points.
    for boundaries in ((4, 8, 12), (4, 4, 12), (0, 8, 16)):
        for value in range(-2, 19):
            code = bisect_right(boundaries, value)
            for threshold in range(-3, 20):
                certain, possible = classify(code, boundaries, threshold)
                assert not certain or value > threshold
                assert not value > threshold or possible
                counts['range'] += 1
    # Immutable editions and a conservative dirty bypass across publication.
    rows = {0: 2, 1: 7}
    old = dict(rows)
    signatures = {k: v % 4 for k, v in rows.items()}
    retained = dict(signatures)
    dirty = set()
    for key, value in ((0, 9), (2, 6), (1, None), (0, 4)):
        dirty.add(key)  # Before the new visible value can be missed by metadata.
        if value is None:
            rows.pop(key)
        else:
            rows[key] = value
        for query in range(12):
            for k, v in rows.items():
                possible = k in dirty or signatures.get(k) == query % 4
                assert v != query or possible
                counts['history'] += 1
            for k, v in old.items():
                assert v != query or retained[k] == query % 4
                counts['history'] += 1
        signatures = {k: v % 4 for k, v in rows.items()}
        dirty.clear()
    # Drift is a loss of evidence under frozen boundaries; relabeling is a new edition.
    old_bins, new_bins = (25, 50, 75), (125, 150, 175)
    drift = []
    for name, values, boundaries in (
        ('initial_frozen', range(100), old_bins),
        ('shifted_frozen', range(100, 200), old_bins),
        ('shifted_rebuilt', range(100, 200), new_bins),
    ):
        certain = possible = actual = 0
        for value in values:
            c, p = classify(bisect_right(boundaries, value), boundaries, 150)
            certain += c; possible += p; actual += value > 150
        drift.append({'case': name, 'rows': 100, 'true': actual, 'certain': certain,
                      'possible': possible, 'unresolved': possible-certain})
    # Changing boundaries without recoding turns value 130 into the wrong interval.
    assert classify(bisect_right(old_bins, 130), new_bins, 150)[0] and not 130 > 150
    return {'checks': counts, 'drift': drift,
            'scope': 'enumerated semantic models; no concurrent memory-ordering or recovery proof'}


if __name__ == '__main__':
    data = run()
    if len(sys.argv) > 1:
        with open(sys.argv[1], 'w') as out:
            json.dump(data, out, indent=2)
            out.write('\n')
    print(json.dumps(data))
