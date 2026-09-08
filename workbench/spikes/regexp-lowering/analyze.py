#!/usr/bin/env python3
"""Regenerate efficacy tables from per-query counters, without timing claims."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from evidence import verify_compact

TEXT = {'dataset', 'id', 'group', 'status', 'reason', 'pattern', 'like', 'mandatory'}

def read(path):
    with path.open(newline='') as f:
        rows = list(csv.DictReader(f))
    if any(None in row or any(v is None for v in row.values()) for row in rows):
        raise ValueError(f'Malformed CSV: {path}')
    return rows

def write(path, rows):
    if not rows: raise ValueError(f'Empty result: {path}')
    with path.open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)

def pct(n, d):
    return 100*n/d if d else 0.0

def analyze(root):
    if (root / 'provenance.json').exists():
        verify_compact(root)
    pattern_source = root/'pattern_summary.csv'
    if not pattern_source.exists():
        patterns = []
        for dataset in ('accidents', 'uap'):
            patterns.extend(read(root/f'{dataset}-patterns.csv'))
        # Keep every query's counters; detailed expressions remain in the full bundle.
        compact = [{k: v for k, v in r.items() if k not in ('pattern', 'like', 'mandatory')} for r in patterns]
        write(pattern_source, compact)
        good = [r for r in patterns if r['status'] != 'rejected']
        examples = [r for r in good if r['dataset'] == 'accidents']
        examples += sorted((r for r in good if r['dataset'] == 'uap'), key=lambda r: int(r['like_pass'])-int(r['rich_pass']), reverse=True)[:12]
        examples += [r for r in good if r['dataset'] == 'uap' and r['status'] == 'exact'][:8]
        write(root/'examples.csv', examples)
    raw = read(pattern_source)
    patterns = [{k: v if k in TEXT else int(v) for k, v in row.items()} for row in raw]
    groups = defaultdict(list)
    for r in patterns:
        groups[(r['dataset'], 'all')].append(r)
        if r['dataset'] == 'uap': groups[(r['dataset'], r['group'])].append(r)
    summary = []
    for (dataset, group), rows in sorted(groups.items()):
        accepted = [r for r in rows if r['status'] != 'rejected']
        nonexact = [r for r in accepted if r['status'] != 'exact']
        counts = Counter(r['status'] for r in rows)
        total = sum(r['rows'] for r in accepted)
        residual = sum(r['like_pass'] for r in nonexact)
        richer = sum(r['rich_pass'] for r in nonexact)
        survivorbytes = sum(r['survivor_bytes'] for r in nonexact)
        boundedbytes = sum(r['bounded_bytes'] for r in nonexact)
        row = dict(dataset=dataset, group=group, patterns=len(rows), supported=len(accepted),
                   exact=counts['exact'], signature=counts['signature'], fallback=counts['fallback'],
                   rejected=counts['rejected'], pairs=total, hits=sum(r['hits'] for r in accepted),
                   queries_with_hits=sum(r['hits'] > 0 for r in accepted),
                   nonexact_pairs=sum(r['rows'] for r in nonexact),
                   like_re2_calls=residual, rich_re2_calls=richer,
                   literal_pass=sum(r['literal_pass'] for r in nonexact),
                   survivor_bytes=survivorbytes, bounded_bytes=boundedbytes,
                   bounded_rows=sum(r['bounded_rows'] for r in nonexact),
                   bounds_re2_calls=sum(r['bounds_re2_calls'] for r in nonexact),
                   like_calls_avoided_pct=pct(total-residual, total),
                   rich_calls_avoided_pct=pct(total-richer, total),
                   bounds_bytes_removed_pct=pct(survivorbytes-boundedbytes, survivorbytes),
                   false_negatives=sum(r['false_negatives'] for r in accepted),
                   exact_mismatches=sum(r['exact_mismatches'] for r in accepted),
                   bounds_mismatches=sum(r['bounds_mismatches'] for r in accepted))
        if any(row[x] for x in ('false_negatives', 'exact_mismatches', 'bounds_mismatches')):
            raise ValueError('Correctness failure in recorded counters')
        summary.append(row)
    write(root/'summary.csv', summary)
    policy_path = root/'policy_summary.csv'
    if not policy_path.exists():
        totals = defaultdict(Counter)
        for dataset in ('accidents', 'uap'):
            for row in read(root/f'{dataset}-policies.csv'):
                keys = ('dataset', 'arm', 'order', 'container_rows', 'policy')
                key = tuple(row[k] for k in keys)
                totals[key].update({k: int(v) for k, v in row.items() if k not in (*keys, 'id')})
                totals[key]['queries'] += 1
                totals[key]['queries_switching'] += int(int(row['switches']) > 0)
        policies = [dict(zip(keys, key)) | dict(value) for key, value in sorted(totals.items())]
        write(policy_path, policies)
    policies = read(policy_path)
    lines = ['# Raw-string efficacy', '',
             'RE2 Boolean search oracle. Counts of avoided calls and candidate bytes are not speedups.', '',
             '| Dataset / group | Supported | Exact | Signature | Fallback | LIKE calls avoided | Rich IR calls avoided | Survivor bytes outside bounds |',
             '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    for r in summary:
        lines.append(f'| {r["dataset"]} / {r["group"]} | {r["supported"]} | {r["exact"]} | {r["signature"]} | {r["fallback"]} | {r["like_calls_avoided_pct"]:.3f}% | {r["rich_calls_avoided_pct"]:.3f}% | {r["bounds_bytes_removed_pct"]:.3f}% |')
    lines += ['', '## Switching, 256-row containers', '',
              'Non-exact patterns only; source order. Four-container warmup. Every row contributes a verified result.', '',
              '| Dataset | Arm | Policy | Filtered pairs | RE2 calls | Missed rejections | Queries switching |',
              '| --- | --- | --- | ---: | ---: | ---: | ---: |']
    for r in policies:
        if r['container_rows'] == '256' and r['order'] == 'source':
            lines.append(f'| {r["dataset"]} | {r["arm"]} | {r["policy"]} | {int(r["filters"]):,} | {int(r["re2_calls"]):,} | {int(r["missed_rejections"]):,} | {r["queries_switching"]} |')
    lines += ['', 'Full per-query counters: `pattern_summary.csv`. All order, threshold, and container-size variants: `policy_summary.csv`.',
              'Raw outcome flags and per-container/per-query policy traces are in the full run bundle.', '']
    (root/'summary.md').write_text('\n'.join(lines))
    (root/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print('\n'.join(lines[:12]))

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('directory', type=Path)
    analyze(p.parse_args().directory)
