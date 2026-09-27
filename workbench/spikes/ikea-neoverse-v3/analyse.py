"""Summarize raw repetitions without treating a case-matrix average as a workload."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

inputs = {}


def cases(path):
    raw = path.read_bytes()
    data = json.loads(raw)
    inputs[str(path)] = {'sha256': hashlib.sha256(raw).hexdigest(),
                         'context': data.get('context', {})}
    rows = {}
    for item in data['benchmarks']:
        if item.get('error_occurred'):
            raise ValueError(f'{path}: benchmark error: {item}')
        if item.get('run_type', 'iteration') != 'iteration':
            continue
        value = item['cpu_time'] * {'ns': 1, 'us': 1000, 'ms': 1e6, 's': 1e9}[item['time_unit']]
        rows.setdefault(item['name'], []).append(value)
    return {name: {'median_ns': statistics.median(values), 'samples_ns': values,
                   'cv': statistics.stdev(values) / statistics.mean(values) if len(values) > 1 else 0}
            for name, values in rows.items()}


def result_dirs(receipt):
    group = json.loads(receipt.read_text())
    return {member: Path('build/workers') / row['job'] / 'results'
            for member, row in group['members'].items()}


def write_csv(path, rows):
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def baseline(receipt, output):
    dirs = result_dirs(receipt)
    hashes = [json.loads((dirs[m] / 'binary-hashes.json').read_text()) for m in ('c8g', 'c9g')]
    if hashes[0] != hashes[1]:
        raise ValueError('baseline binary hashes differ')
    rows, summary = [], {}
    for suite in ('seriespack', 'tuplepack', 'words', 'bec256'):
        a, b = (cases(dirs[m] / (suite + '.json')) for m in ('c8g', 'c9g'))
        if a.keys() != b.keys(): raise ValueError('case coverage differs')
        ratios = []
        for name in a:
            ratio = a[name]['median_ns'] / b[name]['median_ns']
            ratios.append(ratio)
            rows.append({'suite': suite, 'case': name, 'c8g_ns': a[name]['median_ns'],
                         'c9g_ns': b[name]['median_ns'], 'c8g_over_c9g': ratio,
                         'c8g_cv': a[name]['cv'], 'c9g_cv': b[name]['cv'],
                         'c8g_samples_ns': json.dumps(a[name]['samples_ns']),
                         'c9g_samples_ns': json.dumps(b[name]['samples_ns'])})
        summary[suite] = {'cases': len(ratios), 'median_case_speedup': statistics.median(ratios),
                          'geomean_case_speedup': math.exp(statistics.mean(map(math.log, ratios))),
                          'min_speedup': min(ratios), 'max_speedup': max(ratios)}
    write_csv(output / 'baseline-cases.csv', rows)
    (output / 'baseline-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


def explore(receipt, output):
    rows = []
    for machine, directory in result_dirs(receipt).items():
        for tune in ('neoverse-v2', 'neoverse-v3'):
            for trial in (1, 2):
                data = cases(directory / tune / f'trial-{trial}.json')
                for name, value in data.items():
                    family, method = name.rsplit('/', 1)
                    control = family + ('/tbl4' if family == 'lookup' else '/maintained')
                    rows.append({'machine': machine, 'tune': tune, 'trial': trial,
                                 'case': name, 'family': family, 'method': method,
                                 'median_ns': value['median_ns'], 'cv': value['cv'],
                                 'control_over_candidate': data[control]['median_ns'] / value['median_ns'],
                                 'samples_ns': json.dumps(value['samples_ns'])})
    write_csv(output / 'explore-cases.csv', rows)
    for machine in ('c8g', 'c9g'):
        print(machine)
        for span in ('exact', 'padded'):
            for method in ('factored', 'tbl2', 'tbx4', 'interleaved', 'sve', 'sve_interleaved'):
                selected = [r for r in rows if r['machine'] == machine and r['tune'] == 'neoverse-v2'
                            and r['method'] == method and r['family'].endswith('/' + span)]
                values = [r['control_over_candidate'] for r in selected]
                print(span, method, 'median speedup', round(statistics.median(values), 3),
                      'range', round(min(values), 3), round(max(values), 3))


def paired(receipt, output, study, variants, pattern, prefix=''):
    rows = []
    for machine, directory in result_dirs(receipt).items():
        for trial in (1, 2):
            a, b = (cases(directory / prefix / variant / pattern.format(trial=trial))
                    for variant in variants)
            if a.keys() != b.keys(): raise ValueError('case coverage differs')
            for name in a:
                rows.append({'machine': machine, 'trial': trial, 'case': name,
                             'control': variants[0], 'candidate': variants[1],
                             'control_ns': a[name]['median_ns'], 'candidate_ns': b[name]['median_ns'],
                             'speedup': a[name]['median_ns'] / b[name]['median_ns'],
                             'control_cv': a[name]['cv'], 'candidate_cv': b[name]['cv'],
                             'control_samples_ns': json.dumps(a[name]['samples_ns']),
                             'candidate_samples_ns': json.dumps(b[name]['samples_ns'])})
    write_csv(output / (study + '-cases.csv'), rows)
    summary = {}
    for machine in ('c8g', 'c9g'):
        summary[machine] = {}
        for trial in (1, 2):
            selected = [r['speedup'] for r in rows if r['machine'] == machine and r['trial'] == trial]
            summary[machine][trial] = {'cases': len(selected), 'median_case_speedup': statistics.median(selected),
                                       'geomean_case_speedup': math.exp(statistics.mean(map(math.log, selected))),
                                       'min_speedup': min(selected), 'max_speedup': max(selected)}
    (output / (study + '-summary.json')).write_text(json.dumps(summary, indent=2)+'\n')
    print(study, json.dumps(summary, indent=2))


def tuple(receipt, output):
    paired(receipt, output, 'tuple', ('stock', 'split'), 'tuple-{trial}.json')
    paired(receipt, output, 'tuple-words', ('stock', 'split'), 'words-{trial}.json')


def followup(receipt, output):
    paired(receipt, output, 'tune', ('neoverse-v2', 'neoverse-v3'), 'trial-{trial}.json', 'tune')
    paired(receipt, output, 'series', ('stock', 'vector'), 'trial-{trial}.json', 'series')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['baseline', 'explore', 'tuple', 'followup'])
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    globals()[args.mode](args.receipt, args.output)
    receipt_bytes = args.receipt.read_bytes()
    receipt_name = args.mode + '-receipt.json'
    (args.output / receipt_name).write_bytes(receipt_bytes)
    provenance = {'receipt': str(args.receipt),
                  'retained_receipt': receipt_name,
                  'receipt_sha256': hashlib.sha256(receipt_bytes).hexdigest(),
                  'analysis_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  'selection': 'All iteration rows in the named inputs; per-case median CPU ns. '
                               'Trials remain separate; suite summaries are unweighted descriptions.',
                  'inputs': inputs}
    (args.output / (args.mode + '-inputs.json')).write_text(json.dumps(provenance, indent=2)+'\n')
