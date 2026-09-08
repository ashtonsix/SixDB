#!/usr/bin/env python3
"""Factored-filter efficacy and operation counts, without timing claims."""
import argparse
from collections import defaultdict
import csv
from pathlib import Path

TEXT = {'dataset', 'id', 'group', 'pattern', 'like', 'literal_dag', 'chain_dag', 'ordered_dag'}
DETAIL = TEXT - {'dataset', 'id', 'group'}

def read(path):
    with path.open(newline='') as f: rows = list(csv.DictReader(f))
    if any(None in r or any(v is None for v in r.values()) for r in rows):
        raise ValueError(f'Malformed CSV: {path}')
    return rows

def write(path, rows):
    with path.open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)

def analyze(root):
    compact = root/'factor_summary.csv'
    if not compact.exists():
        full = [r for d in ('accidents', 'uap') for r in read(root/f'{d}-factor-patterns.csv')]
        write(compact, [{k: v for k, v in r.items() if k not in DETAIL} for r in full])
        pool = [r for r in full if not int(r['exact'])]
        selected = [r for r in pool if r['dataset'] == 'accidents']
        for a, b in [('like_re2', 'ordered_re2'), ('chain_re2', 'ordered_re2'), ('ordered_re2', 'rich_re2'), ('trained_atoms', 'chain_atoms')]:
            selected += sorted(pool, key=lambda r: int(r[a])-int(r[b]), reverse=True)[:6]
        unique = {(r['dataset'], r['id']): r for r in selected}
        write(root/'examples.csv', list(unique.values()))
    rows = [{k: v if k in TEXT else int(v) for k, v in r.items()} for r in read(compact)]
    grouped = defaultdict(list)
    for r in rows:
        grouped[(r['dataset'], 'all')].append(r)
        if r['dataset'] == 'uap': grouped[(r['dataset'], r['group'])].append(r)
    summaries, structures = [], []
    # arm, residual counter, simple-filter counters, richer evaluations, positions
    arms = [
        ('like64', 'like_re2', 'like', None, False),
        ('rich64', 'rich_re2', 'like', 'like_re2', False),
        ('literal_dag', 'literal_re2', 'literal', None, False),
        ('like_dag', 'chain_re2', 'chain', None, False),
        ('like_dag_trained', 'chain_re2', 'trained', None, False),
        ('like_dag_then_like64', 'joint_like_re2', 'cascade', None, False),
        ('like64_then_like_dag', 'joint_like_re2', 'reverse', None, False),
        ('like_dag_then_rich64', 'joint_rich_re2', 'cascade', 'joint_like_re2', False),
        ('ordered_dag', 'ordered_re2', 'chain', None, True),
        ('ordered_then_like64', 'ordered_joint_like_re2', 'ordered_cascade', None, True),
        ('ordered_then_rich64', 'ordered_joint_rich_re2', 'ordered_cascade', 'ordered_joint_like_re2', True),
    ]
    for (dataset, group), rs in sorted(grouped.items()):
        ns = [r for r in rs if not r['exact']]
        total = sum(r['rows'] for r in rs)
        nonexact_pairs = sum(r['rows'] for r in ns)
        def s(key): return sum(r[key] for r in ns)
        def peak(key): return max((r[key] for r in ns), default=0)
        for arm, residual, work, rich, positions in arms:
            calls = s(residual)
            summaries.append(dict(dataset=dataset, group=group, arm=arm, queries=len(rs), exact=sum(r['exact'] for r in rs),
                pairs=total, nonexact_pairs=nonexact_pairs, true_matches=sum(r['hits'] for r in rs),
                re2_calls=calls, avoided_pct=100*(total-calls)/total,
                simple_evals=s(work+'_atoms'), simple_evals_per_nonexact_pair=s(work+'_atoms')/nonexact_pairs,
                offered_bytes=s(work+'_offered_bytes'), rich_evals=s(rich) if rich else 0,
                position_evals=s('position_atoms') if positions else 0,
                position_visits=s('position_visits') if positions else 0,
                peak_position_buffer_bytes=peak('position_peak_buffer_bytes') if positions else 0,
                peak_position_memo_entries=peak('position_peak_memo_entries') if positions else 0))
        structures.append(dict(dataset=dataset, group=group, nonexact_queries=len(ns),
            baseline_fallbacks=s('baseline_fallback'), literal_fallbacks=s('literal_fallback'), chain_fallbacks=s('chain_fallback'),
            like_branches=s('like_branches'), like_tokens=s('like_tokens'), like_literal_bytes=s('like_literal_bytes'),
            chain_nodes=s('chain_nodes'), chain_leaves=s('chain_leaves'), chain_tokens=s('chain_tokens'), chain_literal_bytes=s('chain_literal_bytes'),
            ordered_nodes=s('ordered_nodes'), ordered_leaves=s('ordered_leaves'),
            peak_chain_nodes=peak('chain_nodes'), peak_ordered_nodes=peak('ordered_nodes'),
            chain_relaxations=s('chain_relaxations'), ordered_relaxations=s('ordered_relaxations'),
            unordered_extra_vs_ordered=s('chain_re2')-s('ordered_re2'),
            source_warm_atoms=s('source_warm_atoms'), eager_warm_atoms=s('warm_atoms'),
            tail_source_atoms=s('tail_source_atoms'), tail_trained_atoms=s('tail_trained_atoms'),
            training_net_saved_atoms=s('chain_atoms')-s('trained_atoms'),
            training_queries_better=sum(r['trained_atoms']<r['chain_atoms'] for r in ns),
            training_queries_worse=sum(r['trained_atoms']>r['chain_atoms'] for r in ns),
            gate_cache_hits=s('chain_cache_hits'), gate_node_visits=s('chain_node_visits')))
    write(root/'summary.csv', summaries); write(root/'structure.csv', structures)
    lines = ['# Factored-filter efficacy', '',
        'All arms use the same 64-branch exact route. Residual metrics concern non-exact queries. Counts are not timings.', '',
        '| Dataset | Arm | Potential RE2 calls | Avoided | Simple LIKE evaluations | Position searches | Rich IR evaluations |',
        '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
    for r in summaries:
        if r['group'] == 'all':
            lines.append(f'| {r["dataset"]} | {r["arm"]} | {r["re2_calls"]:,} | {r["avoided_pct"]:.3f}% | {r["simple_evals"]:,} | {r["position_evals"]:,} | {r["rich_evals"]:,} |')
    lines += ['', 'The ordered arm pays for its unordered LIKE gate and additional position searches. Position searches use a separate untimed DP reference; these operation columns must not be added as equal-cost units.', '',
        '`factor_summary.csv` retains every query. `structure.csv` retains representation, ordering, and paid-training counters. `examples.csv` contains selected full plans.', '']
    (root/'summary.md').write_text('\n'.join(lines))
    print('\n'.join(lines))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('directory', type=Path)
    analyze(parser.parse_args().directory)
