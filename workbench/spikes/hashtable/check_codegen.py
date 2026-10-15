#!/usr/bin/env python3
"""Reject outlined lookup/packet drivers before timing the captured binary."""
import argparse
import gzip
import json
import re
from pathlib import Path


def check(path):
    opener = gzip.open if path.suffix == '.gz' else open
    header = re.compile(r'^[0-9a-f]+ <(.+)>:$')
    call = re.compile(r'\s(?:callq?|bl|blr)\s')
    consumers = ('ht::dependent<', 'ht::cohort_dependent<', 'ht::independent<', 'ht::packets<')
    bodies = {}
    current = None
    with opener(path, 'rt') as stream:
        for line in stream:
            match = header.match(line.strip())
            if match:
                name = match[1]
                current = name if any(c in name for c in consumers) else None
                if current is not None:
                    bodies[current] = []
            elif current is not None and call.search(line):
                bodies[current].append(line.strip())
    baseline = {k: v for k, v in bodies.items() if 'ht::abseil<' in k}
    assert len(baseline) == 10, ('expected K16/K40 point/cohort/independent/packet4/packet8', len(baseline))
    failures = {name: calls for name, calls in baseline.items() if calls}
    # Cold chain resolution remains permitted. The packet expansion and its
    # callbacks must fuse; merely marking the outer wrapper inline is not enough.
    for name, calls in bodies.items():
        if name in baseline:
            continue
        outlined = [line for line in calls if 'ht::lanes<' in line or
                    ('operator()' in line and ('getN<' in line or 'ht::packets<' in line))]
        if outlined:
            failures[name] = outlined
    assert not failures, json.dumps(failures, indent=2)
    return dict(baseline_consumers=len(baseline), candidate_consumers=len(bodies)-len(baseline),
                baseline_calls=0, outlined_packet_stages=0,
                limitation='Checks call fusion, not instruction quality, spills, PMU traffic or performance.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.assembly), indent=2))
