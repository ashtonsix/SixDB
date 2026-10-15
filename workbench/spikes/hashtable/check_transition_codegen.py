#!/usr/bin/env python3
"""Inspect complete transition consumers and their specialized traversal bodies."""
import argparse
import collections
import gzip
import json
import re
from pathlib import Path


def check(path):
    opener = gzip.open if path.suffix == '.gz' else open
    bodies = {};current = None;counts = collections.Counter()
    roots = {'migrate': 'ht::copy_result ht::migrate<', 'accumulate': 'unsigned long ht::accumulate<',
             'combine_source': 'unsigned long ht::combine_source<', 'read': 'unsigned long ht::consume_values<'}
    with opener(path, 'rt') as stream:
        for line in stream:
            m = re.match(r'^[0-9a-f]+ <(.+)>:$', line.strip())
            if m:
                name = m[1];current = None
                for family, prefix in roots.items():
                    if name.startswith(prefix):counts[family] += 1;current = name
                for family in ('merge_reencoded', 'merge_from'):
                    if ('::merge_result ht::table<' in name and '::'+family+'<' in name):
                        counts[family] += 1;current = name
                # Once-per-traversal calls are intentional; their per-entry
                # callbacks must still fuse the complete search/value update.
                if '::each<' in name and any(s in name for s in ('ht::migrate<', 'ht::combine_source<')):current = name
                if current is not None:bodies[current] = []
            elif current is not None and re.search(r'\s(?:callq?|bl|blr|jmpq?|b|br)\s', line):
                target = re.search(r'<(.+)>', line)
                if target and not target[1].startswith(current+'+'):bodies[current].append(line.strip())
                elif re.search(r'\s(?:blr|br)\s', line):bodies[current].append(line.strip())
    expected = dict(migrate=23, accumulate=3, combine_source=1, read=5, merge_reencoded=21, merge_from=2)
    assert dict(counts) == expected, dict(counts)
    failures = {}
    for name, calls in bodies.items():
        bad = [c for c in calls if any(s in c for s in ('find_or_prepare_insert', 'try_emplace',
            '::upsert<', '::upsert_miss<', '::find<', '>::find(', '::insert_absent(', 'ht::add::operator()'))]
        if bad:failures[name] = bad
    assert not failures, json.dumps(failures, indent=2)
    return dict(consumers=dict(counts), outlined_hot_searches=0, remaining_transfers=bodies,
        limitation='Named search/update fusion only; whole-traversal helpers and allocation/chain helpers allowed. '
                   'Not proof of equal hash cost, code size, admission behavior, memory or speed.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__);p.add_argument('assembly', type=Path)
    print(json.dumps(check(p.parse_args().assembly), indent=2))
