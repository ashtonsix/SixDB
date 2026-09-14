#!/usr/bin/env python3
"""Network-only three-voter quorum scenarios from separately measured RTTs.

No simultaneous fan-out or consensus latency is claimed. Bounds span every
coupling of the two empirical link marginals; an independence scenario is
reported separately, never treated as an observed joint distribution.
"""
import argparse
import bisect
import csv
import gzip
import itertools
import json
import math
from pathlib import Path

from summarize import write_csv


def quantile_cdf(a, b, q, mode):
    candidates = sorted(set(a) | set(b))
    def cdf(t):
        fa = bisect.bisect_right(a, t) / len(a)
        fb = bisect.bisect_right(b, t) / len(b)
        if mode == 'upper_cdf':
            return min(1., fa + fb)
        if mode == 'lower_cdf':
            return max(fa, fb)
        return 1 - (1 - fa) * (1 - fb)
    low, high = 0, len(candidates) - 1
    while low < high:
        mid = (low + high) // 2
        if cdf(candidates[mid]) >= q:
            high = mid
        else:
            low = mid + 1
    return candidates[low]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = {}
    for folder in args.inputs:
        for row in json.loads((folder / 'cases.json').read_text()):
            if row['protocol'] != 'tcp':
                continue
            values = [int(x) / 1000 for x in gzip.open(folder / row['file'], 'rt').read().splitlines()[1:]]
            key = row['mtu'], row['bytes'], row['node'], row['destination']
            data.setdefault(key, []).extend(values)
    for values in data.values():
        values.sort()
    rows = []
    for mtu, size, triple in itertools.product([1500, 9001], [64, 512, 1400, 4096, 8192, 65536], itertools.combinations(range(6), 3)):
        for leader in triple:
            followers = [x for x in triple if x != leader]
            a, b = [data[mtu, size, leader, follower] for follower in followers]
            row = {'mtu': mtu, 'bytes': size, 'azs': ','.join(f'use1-az{x+1}' for x in triple),
                   'leader': f'use1-az{leader+1}', 'followers': ','.join(f'use1-az{x+1}' for x in followers)}
            for label, q in [('p50', .5), ('p99', .99), ('p999', .999)]:
                row[f'healthy_{label}_lower_us'] = quantile_cdf(a, b, q, 'upper_cdf')
                row[f'healthy_{label}_independent_us'] = quantile_cdf(a, b, q, 'independent')
                row[f'healthy_{label}_upper_us'] = quantile_cdf(a, b, q, 'lower_cdf')
                # Worst follower failure leaves the larger individual percentile.
                row[f'one_follower_down_worst_{label}_us'] = max(x[max(0, math.ceil(q * len(x)) - 1)] for x in [a, b])
            rows.append(row)
    args.output.mkdir(parents=True, exist_ok=True)
    write_csv(args.output / 'consensus-scenarios.csv', rows)
    for size in [64, 65536]:
        subset = [r for r in rows if r['bytes'] == size]
        for metric in ['healthy_p99_upper_us', 'one_follower_down_worst_p99_us']:
            for op in [min, max]:
                best = op(subset, key=lambda r: r[metric])
                print(size, metric, op.__name__, json.dumps(best))

if __name__ == '__main__':
    main()
