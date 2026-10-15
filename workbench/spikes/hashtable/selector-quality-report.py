#!/usr/bin/env python3
"""Summarize observed selector checks; no timing or adversary guarantee."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
from statistics import mean

FIELDS = ('extra_hit_suffix_checks', 'miss_suffix_checks',
          'primary_extra_hit_checks', 'secondary_extra_hit_checks',
          'primary_miss_checks', 'secondary_miss_checks')
KEY = ('K', 'A', 'slots', 'n', 'pattern')


def report(source, output):
    rows = list(csv.DictReader(source.open()))
    pairs = defaultdict(dict)
    for row in rows:
        row = {k: v if k in ('pattern', 'variant') else int(v) for k, v in row.items()}
        key = tuple(row[k] for k in KEY) + (row['seed'],)
        assert row['variant'] in ('fold', 'multiply') and row['variant'] not in pairs[key]
        assert row['primary'] + row['secondary'] + row['overflow'] == row['n']
        assert row['extra_hit_suffix_checks'] == row['primary_extra_hit_checks'] + row['secondary_extra_hit_checks']
        assert row['miss_suffix_checks'] == row['primary_miss_checks'] + row['secondary_miss_checks']
        pairs[key][row['variant']] = row
    groups = defaultdict(lambda: defaultdict(list))
    for key, variants in pairs.items():
        assert set(variants) == {'fold', 'multiply'}
        fold, multiply = variants['fold'], variants['multiply']
        for field in ('primary', 'secondary', 'overflow', 'primary_selector_word_bits', 'secondary_selector_word_bits'):
            assert fold[field] == multiply[field]
        if fold['K'] - fold['A'] <= 8:
            for field in ('primary_extra_hit_checks', 'primary_miss_checks'):
                assert fold[field] == multiply[field]
        for variant, row in variants.items():
            groups[key[:-1]][variant].append(row)
    assert {key[0] for key in groups} == set(range(8, 65, 8))
    seed_sets = [{r['seed'] for r in variants['fold']} for variants in groups.values()]
    assert seed_sets and all(s == seed_sets[0] for s in seed_sets)
    assert seed_sets[0] == set(range(len(seed_sets[0])))
    result = []
    for key, variants in sorted(groups.items()):
        entry = dict(zip(KEY, key))
        entry['seeds'] = len(seed_sets[0])
        for variant, batch in variants.items():
            for field in FIELDS:
                rates = sorted(r[field] / r['n'] for r in batch)
                entry[f'{variant}_{field}_mean'] = mean(rates)
                entry[f'{variant}_{field}_p95'] = rates[math.ceil(.95 * len(rates)) - 1]
                entry[f'{variant}_{field}_max'] = rates[-1]
            for field in ('worst_hit_suffix_checks', 'worst_miss_suffix_checks'):
                entry[f'{variant}_{field}_max'] = max(r[field] for r in batch)
        result.append(entry)
    with output.open('w') as out:
        writer = csv.DictWriter(out, fieldnames=list(result[0]))
        writer.writeheader()
        writer.writerows(result)
    print(json.dumps(dict(rows=len(rows), paired_histories=len(pairs), cohorts=len(groups),
        seeds=len(seed_sets[0]), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        accounting='False suffix verifications per original hit/miss query, separately by tier; chains excluded. Fresh admission only.')))
    for field in ('extra_hit_suffix_checks', 'miss_suffix_checks'):
        print('Largest mean-rate increases:', field)
        worst = sorted(result, key=lambda r: r[f'multiply_{field}_mean'] - r[f'fold_{field}_mean'], reverse=True)[:12]
        for row in worst:
            print(*(row[k] for k in KEY), *(f'{row[f"{variant}_{field}_{stat}"]:.6f}'
                for variant in ('fold', 'multiply') for stat in ('mean', 'p95', 'max')))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report(args.source, args.output)
