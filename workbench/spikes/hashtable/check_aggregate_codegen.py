#!/usr/bin/env python3
"""Reject outlined hot searches/combiners in the accumulation receipt."""
import argparse
import gzip
import json
import re
from pathlib import Path


def check(path):
    opener = gzip.open if path.suffix == '.gz' else open
    header = re.compile(r'^[0-9a-f]+ <(.+)>:$')
    call = re.compile(r'\s(?:callq?|bl|blr)\s')
    bodies = {}; current = None
    with opener(path, 'rt') as stream:
        for line in stream:
            match = header.match(line.strip())
            if match:
                current = match[1] if ('ht::accumulate<' in match[1] or 'ht::consume_values<' in match[1]) else None
                if current is not None:
                    bodies[current] = []
            elif current is not None and call.search(line):
                bodies[current].append(line.strip())
    accumulation = [k for k in bodies if 'ht::accumulate<' in k]
    reads = [k for k in bodies if 'ht::consume_values<' in k]
    assert len(accumulation) == 9 and len(reads) == 6, (len(accumulation), len(reads))
    assert sum('ht::abseil<' in k for k in accumulation) == 1
    failures = {}
    for name, calls in bodies.items():
        bad = [c for c in calls if any(s in c for s in
            ('find_or_prepare_insert', 'try_emplace', '::upsert<', '::find<', 'ht::add::operator()', '::combined'))]
        if bad:
            failures[name] = bad
    assert not failures, json.dumps(failures, indent=2)
    return dict(accumulation_consumers=len(accumulation), read_consumers=len(reads), outlined_hot_searches=0,
        remaining_calls=bodies,
        limitation='Named call-fusion check only; permits cold allocation, chain resolution and diagnostics. '
                   'Not proof of instruction quality, lifetime, traffic, or performance.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path);args = parser.parse_args()
    print(json.dumps(check(args.assembly), indent=2))
