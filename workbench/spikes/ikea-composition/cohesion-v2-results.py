#!/usr/bin/env python3
"""Summarize paired native trials without replacing per-case interpretation.

Google Benchmark units/iteration filtering use the existing evidence helper.
The custom filter harness reports wall ns per 16-row predicate window. Neither
metric is pooled across cases or presented as an overall speedup.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cohesion_evidence', ROOT / 'workbench/tools/evidence.py')
evidence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evidence)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stats(values):
    middle = statistics.median(values)
    return {'samples': values, 'median': middle, 'min': min(values), 'max': max(values),
            'mad': statistics.median(abs(value - middle) for value in values)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', help='distinct member=worker results directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    sources, samples, google_inputs = {}, {}, []
    expected_trials = None
    for value in args.inputs:
        label, directory = value.split('=', 1)
        if not label or label in sources:
            raise ValueError('choose distinct nonempty member labels')
        directory = Path(directory).resolve()
        receipt = json.loads((directory / 'receipt.json').read_text())
        trials = receipt['trials']
        if expected_trials is None:
            expected_trials = trials
        if trials != expected_trials or trials < 3:
            raise ValueError('all inputs must contain the same three or more independent trials')
        sources[label] = {'directory': str(directory), 'receipt_sha256': sha(directory / 'receipt.json'),
                          'baseline_commit': receipt['baseline_commit'],
                          'before_label': receipt['before_label'],
                          'source_manifest_sha256': receipt['source_manifest_sha256'],
                          'cpu': receipt['cpu'], 'trials': trials, 'files_sha256': {}}
        for profile in receipt['profiles']:
            # Each trial must contain exactly the same case set. The receipt's
            # trial number pairs the counterbalanced arm processes.
            for family in ['seriespack', 'tuplepack', 'initialize']:
                case_sets = []
                for arm in ['before', 'after']:
                    rows, contexts = [], []
                    for trial in range(trials):
                        path = directory / profile / arm / f'{family}-trial-{trial}.json'
                        document = json.loads(path.read_text())
                        sources[label]['files_sha256'][str(path.relative_to(directory))] = sha(path)
                        iteration_rows = [row for row in document['benchmarks']
                                          if row.get('run_type', 'iteration') == 'iteration']
                        keys = [(row.get('run_name', row['name']), row.get('threads', 1))
                                for row in iteration_rows]
                        if len(keys) != len(set(keys)):
                            raise ValueError(f'duplicate case in {path}')
                        case_sets.append(set(keys))
                        rows.extend(iteration_rows)
                        contexts.append(document.get('context', {}))
                    merged = args.output / 'merged' / f'{label}-{profile}-{arm}-{family}.json'
                    merged.parent.mkdir(exist_ok=True)
                    merged.write_text(json.dumps({'context': {'original_trial_contexts': contexts},
                                                  'benchmarks': rows}, indent=2) + '\n')
                    google_inputs.append((f'{label}/{profile}/{arm}/{family}', merged))
                if not case_sets[0] or any(keys != case_sets[0] for keys in case_sets):
                    raise ValueError(f'incomplete or changed cases: {label}/{profile}/{family}')
            filter_keys = []
            for arm in ['before', 'after']:
                for trial in range(trials):
                    path = directory / profile / arm / f'filters-trial-{trial}.csv'
                    sources[label]['files_sha256'][str(path.relative_to(directory))] = sha(path)
                    keys = set()
                    with path.open(newline='') as handle:
                        for row in csv.DictReader(handle):
                            key = tuple(row[field] for field in
                                        ['workload', 'width_or_cases', 'op', 'active', 'candidate', 'metric', 'rep'])
                            if key in keys:
                                raise ValueError(f'duplicate filter case in {path}: {key}')
                            keys.add(key)
                            timing = float(row['ns_window'])
                            if not math.isfinite(timing) or timing <= 0:
                                raise ValueError(f'invalid filter timing in {path}: {key}')
                            case = '/'.join(key)
                            group = samples.setdefault((label, profile, 'filters', case, 1, 'wall_ns_per_window'),
                                                       {'before': {}, 'after': {}})
                            group[arm][trial] = timing
                    filter_keys.append(keys)
            if not filter_keys[0] or any(keys != filter_keys[0] for keys in filter_keys):
                raise ValueError(f'incomplete or changed filter cases: {label}/{profile}')
    # Preserve the standard helper's exact item-rate and time-unit semantics.
    evidence.summarize(google_inputs, args.output / 'google')
    with (args.output / 'google/cases.csv').open(newline='') as handle:
        for row in csv.DictReader(handle):
            label, profile, arm, family = row['input'].split('/')
            for metric in ['cpu_ns', 'real_ns', 'ns_per_item']:
                if not row[metric]:
                    continue
                values = json.loads(row[metric])
                if len(values) != expected_trials or any(value is None or value <= 0 for value in values):
                    raise ValueError(f'incomplete or invalid samples: {row["input"]}/{row["case"]}/{metric}')
                group = samples.setdefault((label, profile, family, row['case'], int(row['threads']), metric),
                                           {'before': {}, 'after': {}})
                group[arm] = dict(enumerate(values))
    records = []
    for key, arms in sorted(samples.items()):
        if any(set(arm) != set(range(expected_trials)) for arm in arms.values()):
            raise ValueError(f'incomplete paired trials: {key}')
        before = [arms['before'][trial] for trial in range(expected_trials)]
        after = [arms['after'][trial] for trial in range(expected_trials)]
        left, right = stats(before), stats(after)
        ratio = stats([b / a for a, b in zip(before, after)])
        records.append(dict(zip(['member', 'profile', 'family', 'case', 'threads', 'metric'], key)) |
                       {'trials': expected_trials, 'before': left, 'after': right,
                        'after_over_before': ratio, 'ratio_of_medians': right['median'] / left['median']})
    with (args.output / 'paired-cases.csv').open('w', newline='') as handle:
        fields = ['member', 'profile', 'family', 'case', 'threads', 'metric', 'trials']
        fields += [f'{arm}_{stat}' for arm in ['before', 'after', 'after_over_before']
                   for stat in ['samples', 'median', 'min', 'max', 'mad']]
        fields += ['ratio_of_medians']
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        for record in records:
            row = {field: record[field] for field in fields if field in record}
            for arm in ['before', 'after', 'after_over_before']:
                for stat, value in record[arm].items():
                    row[f'{arm}_{stat}'] = json.dumps(value) if stat == 'samples' else value
            writer.writerow(row)
    metadata = {'runner_sha256': sha(Path(__file__)), 'evidence_helper_sha256': sha(ROOT / 'workbench/tools/evidence.py'),
                'sources': sources, 'cases_and_metrics': len(records),
                'paired_cases_sha256': sha(args.output / 'paired-cases.csv'),
                'ratio_direction': 'after/before; below one is less elapsed time',
                'dispersion': 'Independent trials in recorded counterbalanced order. MAD is unscaled median absolute deviation. Trial ratios and ratio of arm medians are both retained. No confidence interval or pooled speedup is inferred.',
                'units': 'cpu_ns/real_ns are per benchmark iteration; ns_per_item follows each benchmark item counter. Filter wall_ns_per_window is per 16-row predicate window, with dependent and independent8 traces distinct.'}
    (args.output / 'provenance.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'output': str(args.output), 'cases_and_metrics': len(records),
                      'paired_cases_sha256': metadata['paired_cases_sha256']}, indent=2))


if __name__ == '__main__':
    main()
