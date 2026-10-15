#!/usr/bin/env python3
"""Check actual complete set consumers, including the FlatHashSet private search."""
import argparse
import collections
import gzip
import json
import re
from pathlib import Path


def check(path):
    opener = gzip.open if path.suffix == '.gz' else open
    bodies = {};current = None;counts = collections.Counter()
    with opener(path, 'rt') as stream:
        for line in stream:
            m = re.match(r'^[0-9a-f]+ <(.+)>:$', line.strip())
            if m:
                name = m[1];current = None
                for family in ('fill', 'contains_independent', 'contains_serial', 'turnover'):
                    if name.startswith('unsigned long ht::'+family+'<'):
                        counts[family] += 1;current = name;bodies[current] = []
            elif current is not None and re.search(r'\s(?:callq?|bl|blr|jmpq?|b|br)\s', line):
                target = re.search(r'<(.+)>', line)
                if target and not target[1].startswith(current+'+'):bodies[current].append(line.strip())
                elif re.search(r'\s(?:blr|br)\s|\s(?:callq?|jmpq?)\s+\*', line):bodies[current].append(line.strip())
    assert dict(counts) == dict(fill=12, contains_independent=12, contains_serial=12, turnover=12), dict(counts)
    failures = {}
    for name, calls in bodies.items():
        bad = [c for c in calls if any(s in c for s in ('find_or_prepare_insert', '::contains(',
            '::upsert<', '::upsert_miss<', '::find<', '>::find(', '::insert_absent('))
            or re.search(r'\s(?:blr|br)\s|\s(?:callq?|jmpq?)\s+\*', c)]
        if bad:failures[name] = bad
    assert not failures, json.dumps(failures, indent=2)
    return dict(consumers=dict(counts), outlined_hot_searches=0, remaining_transfers=bodies,
        limitation='Named search and indirect-call check. Normal insertion/erase/chain helpers remain; '
                   'manual inspection must qualify Boolean-dependent query selection and actual loaded windows.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__);p.add_argument('assembly', type=Path)
    print(json.dumps(check(p.parse_args().assembly), indent=2))
