#!/usr/bin/env python3
"""Extract relevant epoch diagnostics without duplicating the full worker logs."""
import argparse
import json
from pathlib import Path
import re
import sys

from summarize import write_csv


def counters(text):
    return {key: int(value) for key, value in re.findall(r'^\s*([A-Za-z0-9_]+):\s+(\d+)\s*$', text, re.M)}


def cpu(text):
    values = next(line for line in text.splitlines() if line.startswith('cpu ')).split()[1:9]
    return list(map(int, values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows, hosts = [], []
    for folder in args.inputs:
        peers = json.loads((folder / 'peers.json').read_text())
        initial = json.loads((folder / 'initial.json').read_text())
        node = initial['node']; peer = peers[node]
        hosts.append({'node': node, 'az_id': peer['az_id'], 'az': peer['az'], 'identity': peer['identity'],
                      'driver': initial['driver']['stdout'], 'offloads': initial['offloads']['stdout'],
                      'initial_link': initial['link']['stdout'], 'worker': json.loads((folder / 'worker-result.json').read_text())})
        for before_path in sorted(folder.glob('r*-before.json')):
            epoch = before_path.name.removesuffix('-before.json')
            before = json.loads(before_path.read_text()); after = json.loads((folder / f'{epoch}-after.json').read_text())
            b = counters(before['counters']['stdout']); a = counters(after['counters']['stdout'])
            changed = {k: a[k] - b[k] for k in a.keys() & b.keys()}
            relevant = {k: v for k, v in changed.items() if any(term in k for term in ['allowance_exceeded', 'drop', 'error', 'bad', 'fail'])}
            bcpu, acpu = cpu(before['cpu']['stdout']), cpu(after['cpu']['stdout'])
            delta = [x - y for x, y in zip(acpu, bcpu)]
            row = {'node': node, 'az_id': peer['az_id'], 'az': peer['az'], 'epoch': epoch,
                   'seconds': after['utc'] - before['utc'], 'cpu_busy_pct': 100 * (1 - (delta[3] + delta[4]) / sum(delta)),
                   'cpu_steal_pct': 100 * delta[7] / sum(delta), **relevant}
            rows.append(row)
    args.output.mkdir(parents=True, exist_ok=True)
    write_csv(args.output / 'diagnostics.csv', rows)
    (args.output / 'hosts.json').write_text(json.dumps(hosts, indent=2) + '\n')
    print(json.dumps({'epochs': len(rows), 'nonzero_error_or_limit_counters': [
        {'az_id': row['az_id'], 'epoch': row['epoch'], **{k: v for k, v in row.items() if any(term in k for term in ['allowance_exceeded', 'drop', 'error', 'bad', 'fail']) and v}}
        for row in rows if any(v for k, v in row.items() if any(term in k for term in ['allowance_exceeded', 'drop', 'error', 'bad', 'fail']))
    ]}, indent=2))

if __name__ == '__main__':
    main()
