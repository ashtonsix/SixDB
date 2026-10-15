#!/usr/bin/env python3
"""Check ordinary and bound update consumers, permitting the intentional miss call."""
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
                for family in ('accumulate', 'accumulate_bound'):
                    if name.startswith('unsigned long ht::'+family+'<'):
                        counts[family] += 1;current = name
                if '::continue_upsert<' in name:current = name
                if current is not None:bodies[current] = []
            elif current is not None and re.search(r'\s(?:callq?|bl|blr|jmpq?|b|br)\s', line):
                target = re.search(r'<(.+)>', line)
                if target and not target[1].startswith(current+'+'):bodies[current].append(line.strip())
                elif re.search(r'\s(?:blr|br)\s', line):bodies[current].append(line.strip())
    assert dict(counts) == dict(accumulate=3, accumulate_bound=3), dict(counts)
    failures = {}
    for name, calls in bodies.items():
        bad = [c for c in calls if any(s in c for s in ('find_or_prepare_insert', 'try_emplace',
            '::upsert<', '::upsert_miss<', '::find<', '>::find(', 'ht::add::operator()'))]
        if bad:failures[name] = bad
        if name.startswith('unsigned long ht::accumulate_bound<'):
            assert any('::continue_upsert<' in c for c in calls), name
    assert not failures, json.dumps(failures, indent=2)
    return dict(consumers=dict(counts), outlined_hot_searches=0, remaining_transfers=bodies,
        limitation='Named fusion/continuation check, not proof of hoisting, spill removal or speed. '
                   'A bound primary miss deliberately calls continue_upsert; ordinary cold helpers are allowed.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__);p.add_argument('assembly', type=Path)
    print(json.dumps(check(p.parse_args().assembly), indent=2))
