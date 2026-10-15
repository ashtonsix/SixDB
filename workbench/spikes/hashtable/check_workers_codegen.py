#!/usr/bin/env python3
"""Qualify the final two-worker consumers before collecting their timings."""
import argparse
import collections
import gzip
import json
import re
from pathlib import Path

FAMILIES = ('accumulate', 'route_local', 'rebuild_merge', 'read_result')
SHARED_FAMILIES = ('shared_accumulate', 'read_shared_result')


def check(path):
    opener = gzip.open if path.suffix == '.gz' else open
    header = re.compile(r'^[0-9a-f]+ <(.+)>:$')
    transfer = re.compile(r'\s(?:callq?|bl|blr|jmpq?|b|br)\s')
    counts = collections.Counter();bodies = {};current = None
    with opener(path, 'rt') as stream:
        for line in stream:
            match = header.match(line.strip())
            if match:
                name = match[1]
                current = name if any(f'ht::{f}<' in name for f in FAMILIES + SHARED_FAMILIES) or '::merge_from<' in name else None
                if current is not None:
                    bodies[current] = []
                    for f in FAMILIES + SHARED_FAMILIES:
                        if name.startswith(f'unsigned long ht::{f}<'):counts[f] += 1
                    if name.startswith('ht::table<') and '::merge_result ' in name and '::merge_from<' in name:
                        counts['merge_from'] += 1
            elif current is not None and transfer.search(line):
                # Local branches carry the enclosing symbol and +offset. Keep
                # calls and external tail transfers, not ordinary loop branches.
                target = re.search(r'<(.+)>', line)
                if target and not target[1].startswith(current+'+'):bodies[current].append(line.strip())
                elif re.search(r'\s(?:blr|br)\s|\s(?:callq?|jmpq?)\s+\*', line):bodies[current].append(line.strip())
    expected = dict(accumulate=7, route_local=4, rebuild_merge=7, merge_from=8, read_result=11)
    if any(counts[f] for f in SHARED_FAMILIES):
        expected.update(shared_accumulate=2, read_shared_result=2)
    assert dict(counts) == expected, dict(counts)
    failures = {}
    for name, calls in bodies.items():
        if any(f'ht::{f}<' in name for f in SHARED_FAMILIES):
            # Library locking/visitation helpers may remain outlined. Preserve
            # their exact identities in the receipt instead of equating them
            # with an extra table search or silently filtering them away.
            bad = [c for c in calls if '<bool ht::concurrent_baselines::table<' in c and
                '::operator()' not in c and any(s in c for s in ('::add(', '::upsert<', '::consume<'))]
            bad += [c for c in calls if re.search(r'\s(?:blr|br)\s|\s(?:callq?|jmpq?)\s+\*', c)]
            if bad:failures[name] = bad
            continue
        bad = [c for c in calls if any(s in c for s in ('find_or_prepare_insert', 'try_emplace',
            '::upsert<', '::upsert_miss<', '::get_value(', '::find<', '>::find(', 'ht::add::operator()', '::combined'))]
        if bad:failures[name] = bad
    assert not failures, json.dumps(failures, indent=2)
    return dict(consumers=dict(counts), outlined_hot_searches=0, remaining_transfers=bodies,
        outlined_hot_searches_scope='Private consumers; shared consumers only require adapter-wrapper fusion here.',
        indirect_shared_calls=0,
        limitation='Named fusion guard, including specialized iteration bodies. Whole traversal calls, '
                   'allocation, StreamPack resolution and Abseil new-key insertion helpers remain allowed. '
                   'Shared adapter add/upsert/consume wrappers must fuse; library protection/visitation '
                   'helpers remain separately visible in remaining_transfers and need inspection. '
                   'No claim of equal code size, register quality, traffic or performance.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__);parser.add_argument('assembly', type=Path)
    print(json.dumps(check(parser.parse_args().assembly), indent=2))
