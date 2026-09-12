#!/usr/bin/env python3
"""Summarize retained composition/scan measurements and export analyser costs."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
from statistics import median
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from evidence import verify_compact


def read(path):
    values = defaultdict(list)
    for row in csv.DictReader(path.open()):
        if row.get('run_type', 'iteration') != 'iteration':
            continue
        if row.get('error_occurred', '') not in ('', 'false', 'False', '0'):
            raise ValueError(f"failed case {row['name']}")
        scale = {'ns': 1, 'us': 1e3, 'ms': 1e6, 's': 1e9}[row['time_unit']]
        values[row['name']].append(float(row['cpu_time']) * scale / float(row['items_per_iteration']))
    return {name: median(v) for name, v in values.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('evidence', type=Path, nargs='+')
    ap.add_argument('--cost-output', type=Path)
    args = ap.parse_args()
    for root in args.evidence:
        verify_compact(root)
        samples = sorted(root.glob('*/samples.csv'))
        if not samples:
            raise ValueError(f'No sample CSVs in {root}; use runtime/recover.py '
                             'to restore archived evidence (see evidence/README.md).')
        for path in samples:
            profile = path.parent.name
            times = read(path)
            context = json.loads((path.parent / 'context.json').read_text())
            print(f'\n## {root.name} / {profile}\n\nMedian CPU ns/item; {len(times)} cases.\n')
            print('| Layout/union, contiguous, 1024 rows, 1 binding | Separate | Generic | Constants | Algebraic | Normalized | Normalized + effects |')
            print('| --- | ---: | ---: | ---: | ---: | ---: | ---: |')
            for layout in ('ordered','reordered'):
                for union in ('full','partial'):
                    suffix = f'{layout}/{union}/contiguous/body/1024/1'
                    cells = [times.get(f'fusion/{e}/{suffix}') for e in ('separate','generic','constants','algebraic','normalized')]
                    cells.append(times.get(f'fusion/normalized/{layout}/{union}/contiguous/effects/1024/1'))
                    if all(v is None for v in cells):
                        continue
                    print(f'| {layout}/{union} | ' + ' | '.join('—' if v is None else f'{v:.3f}' for v in cells) + ' |')
            ratios = []
            for name, value in times.items():
                if name.startswith('fusion/normalized/'):
                    other = times.get(name.replace('/normalized/', '/constants/'))
                    if other:
                        ratios.append(value / other)
            if ratios:
                print(f'\nNormalized/constants: median {median(ratios):.3f}×, range {min(ratios):.3f}–{max(ratios):.3f}×; '
                      f'{sum(x > 1.4 for x in ratios)}/{len(ratios)} over 1.4×. Different rows/placements/binding counts are separate cases.')
            if 'fusion/prepare/reordered' in times:
                print(f"Full experimental preparation: {times['fusion/prepare/reordered']:.1f} ns (includes both packet plans and lowering).")
            print('\n| Scan, permuted map | 16B plane, one | 16B plane, four | 64B row, one | 64B row, four |')
            print('| --- | ---: | ---: | ---: | ---: |')
            for rows in (1024,65536,1048576):
                cells = [times.get(f'scan/{packet}/permuted/{stride}/{rows}') for stride in (16,64) for packet in ('packet1','packet4')]
                if any(v is not None for v in cells):
                    print(f'| {rows} rows | ' + ' | '.join('—' if v is None else f'{v:.3f}' for v in cells) + ' |')
            operations = ('R_AC','R_0','R_1','W_A','W_AC','W_AB','W_all')
            point = {}
            for name, value in times.items():
                bits = name.split('/')
                if len(bits) == 4 and bits[0] == 'small':
                    point[tuple(bits[1:])] = value
            # A targeted capture can contain only W_A, for example. Keep all
            # candidates/recipes for represented operations; do not invent reads.
            operations = tuple(op for op in operations
                               if any(observed == op for _, observed, _ in point))
            candidates = sorted({c for c, _, _ in point})
            if candidates:
                print('\n| Scalar8 operation | Best recipe per candidate: min / median / max | Unpacked control |')
                print('| --- | ---: | ---: |')
                for op in operations:
                    winners = [min(point[c,op,r] for r in ('word','bytes')) for c in candidates]
                    control = times.get(f'small/unpacked/{op}')
                    print(f'| {op} | {min(winners):.3f} / {median(winners):.3f} / {max(winners):.3f} | ' + ('—' if control is None else f'{control:.3f}') + ' |')
            if args.cost_output and candidates:
                args.cost_output.mkdir(parents=True, exist_ok=True)
                costs = {'schema_version':1, 'kind':'measured', 'context': {
                    'id': root.name + '-' + profile,
                    'contract_id':'byte8-preserve-v1',
                    'provenance': str(root.resolve().relative_to(Path(__file__).resolve().parents[4])),
                    'statistic':'median CPU ns per complete call after items_per_iteration normalization; three sequential repetitions',
                    'source_digest':context['source_digest'], 'binary_sha256':context['binary_sha256'],
                    'scenario':'1024 tuples stride16, 8192 repeated row IDs, two alternating admitted inputs; reads return scalar8, writes check values/preserve spare bits and return issued-byte coverage; no owner publication',
                }, 'measurements':[]}
                for c in candidates:
                    for op in operations:
                        for recipe in ('word','bytes'):
                            costs['measurements'].append({'candidate_id':c, 'operation_id':op, 'recipe_id':recipe,
                                'run_ns':point[c,op,recipe], 'prepare_ns':point[c,op,'prepare']})
                output = args.cost_output / f'{root.name}-{profile}.json'
                output.write_text(json.dumps(costs, indent=2) + '\n')
                print(f'\nAnalyser cost input: {output}')

if __name__ == '__main__':
    main()
