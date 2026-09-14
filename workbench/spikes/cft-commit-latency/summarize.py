#!/usr/bin/env python3
"""Regenerate compact statistics from six workers' retained raw samples."""
import argparse
import csv
import gzip
import itertools
import json
import math
from pathlib import Path
import re
import statistics


def stats(values):
    x = sorted(values)
    if not x:
        return {'samples': 0}
    def quantile(p):
        # Linear interpolation, Hyndman–Fan type 7 (numpy/pandas default).
        i = (len(x) - 1) * p
        low = math.floor(i)
        return x[low] + (x[min(low + 1, len(x) - 1)] - x[low]) * (i - low)
    result = {'samples': len(x), 'min_us': x[0], 'mean_us': statistics.fmean(x), 'stddev_us': statistics.pstdev(x), 'max_us': x[-1]}
    for name, p in [('p01', .01), ('p05', .05), ('p25', .25), ('p50', .5), ('p75', .75), ('p90', .9), ('p95', .95), ('p99', .99), ('p999', .999)]:
        result[name + '_us'] = quantile(p)
    for threshold in [1000, 2000, 5000, 10000]:
        result[f'over_{threshold}us_pct'] = 100 * sum(v > threshold for v in x) / len(x)
    return result


def write_csv(path, rows):
    keys = list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w') as target:
        writer = csv.DictWriter(target, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def summarize(inputs, output):
    output.mkdir(parents=True, exist_ok=True)
    groups, case_rows, peers, boundaries = {}, [], {}, []
    for folder in inputs:
        local_peers = json.loads((folder / 'peers.json').read_text())
        for peer in local_peers:
            peers[peer['node']] = {'node': peer['node'], 'az': peer['az'], 'az_id': peer['az_id']}
        cases = json.loads((folder / 'cases.json').read_text())
        assert len(cases) == 330, (folder, len(cases))
        for case in cases:
            protocol, mtu, size, node, dest = (case[k] for k in ('protocol', 'mtu', 'bytes', 'node', 'destination'))
            raw = gzip.open(folder / case['file'], 'rt').read()
            if protocol == 'tcp':
                values = [int(x) / 1000 for x in raw.splitlines()[1:]]
                assert len(values) == 3000 == case['samples']
                assert case['pmtu'] == mtu, case
                transmitted = len(values)
            else:
                values = [float(x) * 1000 for x in re.findall(r'time[=<]([0-9.]+) ms', raw)]
                match = re.search(r'(\d+) packets transmitted, (\d+) received', raw)
                transmitted, received = map(int, match.groups()) if match else (case['requested'], len(values))
                assert received == len(values), case
                if not case['expected_fit']:
                    assert not values and ('message too long' in raw or 'local error' in raw), raw
                    boundaries.append(case | {'verified_local_rejection': True})
                    continue
                assert case['returncode'] in (0, 1), case
            key = protocol, mtu, size, node, dest
            entry = groups.setdefault(key, {'values': [], 'transmitted': 0, 'retrans': 0, 'runs': []})
            entry['values'].extend(values)
            entry['transmitted'] += transmitted
            entry['retrans'] += case.get('measured_retrans', 0)
            summary = stats(values)
            entry['runs'].append(summary)
            case_rows.append(case | summary | {'transmitted': transmitted, 'lost': transmitted - len(values)})
    directed, pair_groups = [], {}
    for (protocol, mtu, size, node, dest), data in sorted(groups.items()):
        assert len(data['runs']) == 3
        row = {'protocol': protocol, 'mtu': mtu, 'bytes': size, 'source': peers[node]['az_id'], 'destination': peers[dest]['az_id'],
               'source_name': peers[node]['az'], 'destination_name': peers[dest]['az'], **stats(data['values']),
               'transmitted': data['transmitted'], 'lost': data['transmitted'] - len(data['values']),
               'loss_pct': 100 * (data['transmitted'] - len(data['values'])) / data['transmitted'], 'measured_retrans': data['retrans'],
               'repeat_p50_min_us': min(r['p50_us'] for r in data['runs']), 'repeat_p50_max_us': max(r['p50_us'] for r in data['runs']),
               'repeat_p99_min_us': min(r['p99_us'] for r in data['runs']), 'repeat_p99_max_us': max(r['p99_us'] for r in data['runs'])}
        directed.append(row)
        pair = tuple(sorted([node, dest]))
        entry = pair_groups.setdefault((protocol, mtu, size, *pair), {'values': [], 'transmitted': 0, 'retrans': 0})
        entry['values'].extend(data['values']); entry['transmitted'] += data['transmitted']; entry['retrans'] += data['retrans']
    pairs = []
    for (protocol, mtu, size, a, b), data in sorted(pair_groups.items()):
        pairs.append({'protocol': protocol, 'mtu': mtu, 'bytes': size, 'a': peers[a]['az_id'], 'b': peers[b]['az_id'],
                      'a_name': peers[a]['az'], 'b_name': peers[b]['az'], **stats(data['values']),
                      'transmitted': data['transmitted'], 'lost': data['transmitted'] - len(data['values']), 'measured_retrans': data['retrans']})
    triples = []
    az_ids = sorted(p['az_id'] for p in peers.values())
    for mtu, size in itertools.product([1500, 9001], [64, 512, 1400, 4096, 8192, 65536]):
        rows = [r for r in directed if r['protocol'] == 'tcp' and r['mtu'] == mtu and r['bytes'] == size]
        local = []
        for triple in itertools.combinations(az_ids, 3):
            edges = [r for r in rows if r['source'] in triple and r['destination'] in triple]
            assert len(edges) == 6
            worst = max(edges, key=lambda r: r['p99_us'])
            local.append({'mtu': mtu, 'bytes': size, 'azs': ','.join(triple), 'worst_direction_p99_us': worst['p99_us'],
                          'worst_direction': worst['source'] + '->' + worst['destination'],
                          'worst_direction_p50_us': max(r['p50_us'] for r in edges),
                          'mean_direction_p50_us': statistics.fmean(r['p50_us'] for r in edges)})
        for rank, row in enumerate(sorted(local, key=lambda r: (r['worst_direction_p99_us'], r['mean_direction_p50_us'])), 1):
            triples.append({'rank': rank, **row})
    write_csv(output / 'directed.csv', directed)
    write_csv(output / 'pairs.csv', pairs)
    write_csv(output / 'repetitions.csv', case_rows)
    write_csv(output / 'triples.csv', triples)
    write_csv(output / 'mtu-boundaries.csv', boundaries)
    payload = {'peers': list(peers.values()), 'directed': directed, 'pairs': pairs, 'triples': triples}
    (output / 'summary.json').write_text(json.dumps(payload, indent=2) + '\n')
    print(json.dumps({'directed_cases': len(directed), 'pairs': len(pairs), 'triples': len(triples), 'mtu_boundary_checks': len(boundaries),
                      'tcp_samples': sum(r['samples'] for r in directed if r['protocol'] == 'tcp'),
                      'icmp_samples': sum(r['samples'] for r in directed if r['protocol'] == 'icmp'),
                      'lost': sum(r['lost'] for r in directed), 'tcp_retrans': sum(r['measured_retrans'] for r in directed)}, indent=2))
    return payload


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    summarize(args.inputs, args.output)
