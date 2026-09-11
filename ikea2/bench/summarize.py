#!/usr/bin/env python3
"""Export selected worker evidence, or regenerate its offline comparison report."""
import argparse
import csv
import json
from pathlib import Path
import shutil
import statistics


def export(worker, destination):
    results = worker / 'results'
    raw = json.loads((results / 'samples.json').read_text())
    destination.mkdir(parents=True, exist_ok=False)
    with (destination / 'samples.csv').open('w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['case', 'repetition', 'cpu_ns', 'real_ns', 'logical_values', 'selected_values', 'write_records'])
        scale = {'ns': 1, 'us': 1000, 'ms': 1_000_000, 's': 1_000_000_000}
        for row in raw['benchmarks']:
            if row.get('run_type', 'iteration') != 'iteration':
                continue
            unit = scale[row['time_unit']]
            writer.writerow([row['name'], row.get('repetition_index', 0), row['cpu_time'] * unit,
                             row['real_time'] * unit, row.get('logical_values', ''),
                             row.get('selected_values', ''), row.get('write_records', '')])
    job = json.loads((results / 'job.json').read_text())
    provenance = {'job': worker.name, 'source': job['source'], 'script': job.get('script'),
                  'config': job['config'], 'benchmark_context': raw['context']}
    (destination / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    for name in ['checks.txt', 'integration-checks.txt', 'host.json']:
        shutil.copyfile(results / name, destination / name)
    for name in ['build-summary.json', 'code-size.json']:
        if (results / name).exists():
            shutil.copyfile(results / name, destination / name)
    for folder, output in [('compile-probe', 'compile-cost.json'), ('pipeline-compile', 'pipeline-compile.json')]:
        probe = results / folder / 'summary.json'
        if probe.exists():
            data = json.loads(probe.read_text())
            keys = ['status', 'limits', 'serial_compile_wall_seconds', 'serial_compile_user_seconds',
                    'serial_compile_system_seconds', 'max_process_rss_kib']
            compact = {k: data[k] for k in keys}
            compact['compiles'] = [{k: row[k] for k in ['source', 'wall_seconds', 'user_seconds',
                'system_seconds', 'max_process_rss_kib', 'returncode', 'status', 'object']}
                for row in data['compiles']]
            (destination / output).write_text(json.dumps(compact, indent=2) + '\n')
        sizes = results / folder / 'code-size.json'
        if folder == 'pipeline-compile' and sizes.exists():
            shutil.copyfile(sizes, destination / 'pipeline-size.json')
    if (results / 'incremental/summary.json').exists():
        shutil.copyfile(results / 'incremental/summary.json', destination / 'incremental.json')
    # worker.py already uploaded and verified this content-addressed bundle.
    shutil.copyfile(worker / 'artifact.json', destination / 'artifact.json')


def report(directory):
    groups = {}
    with (directory / 'samples.csv').open() as f:
        for row in csv.DictReader(f):
            groups.setdefault(row['case'], []).append(float(row['cpu_ns']))
    times = {k: statistics.median(v) for k, v in groups.items()}
    rows = []
    for case, value in times.items():
        family, _, endpoint = case.partition('/')
        path, implementation = case.rsplit('/', 1)
        control = None
        if implementation in ('ikea2', 'ikea2-range', 'ikea2-region'):
            choices = [path + '/prior', path + '/ikea']
            control = next((c for c in choices if c in times), None)
        elif implementation == 'ikea2-native':
            choices = [path + '/' + p for p in ['ikea-grouped-deferred', 'ikea-materialized', 'ikea2-materialized']]
            present = [c for c in choices if c in times]
            control = min(present, key=times.get) if present else None
        elif family == 'overwrite' and implementation.startswith('ikea2-'):
            # Raw full overwrite compares final-wire encoders. Maintenance uses
            # an equivalent old materialize/update/encode control when present.
            if implementation == 'ikea2-sum-coverage' and path + '/ikea-materialized-sum-coverage' in times:
                control = path + '/ikea-materialized-sum-coverage'
            elif implementation == 'ikea2-coverage' and path + '/control-coverage' in times:
                control = path + '/control-coverage'
            elif '/all/' in case:
                control = next((c for c in [path + '/prior', path + '/ikea'] if c in times), None)
        elif family in ('pipeline-mutation','pipeline-packet'):
            control = path + '/inline' if implementation != 'inline' else None
        elif family == 'ordinary':
            if implementation == 'bound':
                control = path + '/concrete'
        elif family == 'placed':
            if implementation == 'sum-native':
                control = path + '/sum-materialized'
            elif implementation == 'mutation-bound':
                control = path + '/mutation-materialized'
        else:
            continue
        rows.append((case, value, min(groups[case]), max(groups[case]), control or '',
                     value / times[control] if control else ''))
    with (directory / 'comparisons.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['case', 'median_cpu_ns', 'min_cpu_ns', 'max_cpu_ns', 'control', 'ratio'])
        w.writerows(rows)
    lines = ['# Measured comparisons', '',
             'Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.',
             '`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.',
             'Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred',
             'implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.',
             'Maintenance rows use an equivalent materialized control where present; inspect each named control.',
             'Pipeline ratios compare execution styles sharing stage bodies; scratch calls also materialize lanes.',
             'These are warm microbenchmarks,',
             'not measurements of Engine publication, Loom scheduling, cold memory or whole queries.', '',
             '| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |',
             '| --- | ---: | ---: | ---: | ---: |']
    buckets = {}
    for case, _, _, _, control, ratio in rows:
        if ratio == '':
            continue
        key = case.split('/')[0] + '/' + case.rsplit('/', 1)[1]
        if key.startswith(('decode/', 'point/')):
            key += ' vs ' + control.rsplit('/', 1)[1]
        buckets.setdefault(key, []).append(ratio)
    for key, ratios in sorted(buckets.items()):
        lines.append(f'| {key} | {len(ratios)} | {statistics.median(ratios):.3f} | {max(ratios):.3f} | {sum(r > 1.4 for r in ratios)} |')
    lines += ['', 'See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.',
              '`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.', '']
    recipes = {}
    for case, value in times.items():
        parts = case.split('/')
        if parts[0] == 'pipeline-packet' and parts[-1] in ('inline', 'cps'):
            recipes.setdefault(parts[2], {}).setdefault(parts[-1], []).append((value, case))
    best = []
    for recipe, styles in sorted(recipes.items()):
        if 'inline' not in styles or 'cps' not in styles:
            continue
        inline, cps = min(styles['inline']), min(styles['cps'])
        best.append({'recipe': recipe, 'inline_case': inline[1], 'cps_case': cps[1],
                     'inline_ns': inline[0], 'cps_ns': cps[0], 'ratio': cps[0] / inline[0]})
    if best:
        (directory / 'pipeline-grains.json').write_text(json.dumps(best, indent=2) + '\n')
        lines += ['## Best measured grain per recipe', '',
                  'CPS and inline may choose different grains from the measured set. This is selection',
                  'from these samples, not a held-out tuning result; see `pipeline-grains.json`.', '',
                  '| Recipe | Inline grain | CPS grain | CPS / inline |',
                  '| --- | --- | --- | ---: |']
        for row in best:
            lines.append(f"| {row['recipe']} | {row['inline_case'].split('/')[1]} | {row['cps_case'].split('/')[1]} | {row['ratio']:.3f} |")
        lines += ['']
    (directory / 'summary.md').write_text('\n'.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--worker', type=Path)
    args = parser.parse_args()
    if args.worker:
        export(args.worker, args.evidence)
    report(args.evidence)
