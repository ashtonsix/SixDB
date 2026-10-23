#!/usr/bin/env python3
"""Summarize complete Bec/Hashtable/StreamPack consumers, retaining phase order.

Each row is one maintained consumer case. Hashtable mutation rounds and the two
StreamPack internal repeats remain separate; independent worker trials supply
dispersion. No average across unrelated cases is a performance claim.
"""
import argparse
import collections
import csv
import importlib.util
import json
import math
from pathlib import Path

SPEC = importlib.util.spec_from_file_location('cohesion_results', Path(__file__).with_name('cohesion-v2-results.py'))
shared = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shared)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', help='distinct member=worker results directory')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    sources, samples = {}, {}
    trials_expected = None

    def add(label, profile, family, case, metric, arm, trial, value):
        value = float(value)
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f'invalid timing: {label}/{profile}/{family}/{case}')
        group = samples.setdefault((label, profile, family, case, metric), {'before': {}, 'after': {}})
        if trial in group[arm]:
            raise ValueError(f'duplicate case: {label}/{profile}/{family}/{case}/{arm}/{trial}')
        group[arm][trial] = value

    for specification in args.inputs:
        label, directory = specification.split('=', 1)
        directory = Path(directory).resolve()
        if not label or label in sources:
            raise ValueError('choose distinct nonempty member labels')
        receipt = json.loads((directory / 'receipt.json').read_text())
        trials = receipt['trials']
        if trials_expected is None:
            trials_expected = trials
        if trials != trials_expected or trials < 3:
            raise ValueError('inputs must have the same three or more independent trials')
        sources[label] = {key: receipt[key] for key in ['baseline_commit', 'before_label', 'source_manifest_sha256', 'cpu', 'trials']}
        sources[label] |= {'directory': str(directory), 'receipt_sha256': shared.sha(directory / 'receipt.json'), 'files_sha256': {}}

        def read(relative):
            path = directory / relative
            sources[label]['files_sha256'][str(relative)] = shared.sha(path)
            return path

        for profile in receipt['profiles']:
            case_sets, fixtures = collections.defaultdict(list), collections.defaultdict(list)
            google_inputs = []
            for arm in ['before', 'after']:
                google_rows, contexts = [], []
                for trial in range(trials):
                    base = Path(profile) / arm
                    document = json.loads(read(base / f'bec-trial-{trial}.json').read_text())
                    rows = [row for row in document['benchmarks'] if row.get('run_type', 'iteration') == 'iteration']
                    keys = [(row.get('run_name', row['name']), row.get('threads', 1)) for row in rows]
                    if not keys or len(keys) != len(set(keys)):
                        raise ValueError('empty or duplicate Bec cases')
                    case_sets['bec256'].append(set(keys))
                    google_rows.extend(rows)
                    contexts.append(document.get('context', {}))
                    for family in ['htable8', 'htable64']:
                        for operation, count in [('read', 2), ('write', 10), ('modify', 4), ('grow', 1)]:
                            path = read(base / f'{family}-{operation}-trial-{trial}.csv')
                            with path.open(newline='') as handle:
                                records = list(csv.DictReader(handle))
                            if len(records) != count:
                                raise ValueError(f'incomplete phase sequence: {path}')
                            rounds, keys, metadata = collections.Counter(), set(), {}
                            for row in records:
                                # Read modes alternate order between process trials.
                                # Mutation phases repeat in their original round order.
                                occurrence = rounds[row['phase']]
                                rounds[row['phase']] += 1
                                case = '/'.join([operation, row['phase'], f'round{occurrence}'])
                                keys.add(case)
                                metadata[case] = {k: v for k, v in row.items() if k not in ['ns', 'rep']}
                                add(label, profile, family, case, 'wall_ns_per_operation', arm, trial, row['ns'])
                            case_sets[(family, operation)].append(keys)
                            fixtures[(family, operation)].append(metadata)
                    for size in [256, 65536]:
                        path = read(base / f'stream-{size}-trial-{trial}.csv')
                        with path.open(newline='') as handle:
                            records = list(csv.DictReader(handle))
                        if len(records) != 108:
                            raise ValueError(f'incomplete StreamPack case set: {path}')
                        keys, metadata = set(), {}
                        for row in records:
                            case = '/'.join(row[field] for field in ['family', 'maximum', 'rows', 'path', 'operation', 'repeat'])
                            if case in keys:
                                raise ValueError(f'duplicate StreamPack case: {path}/{case}')
                            keys.add(case)
                            metadata[case] = row['checksum']
                            add(label, profile, 'streampack', case, 'wall_ns_per_value', arm, trial, row['ns_per_value'])
                        case_sets[('streampack', size)].append(keys)
                        fixtures[('streampack', size)].append(metadata)
                merged = args.output / 'merged' / f'{label}-{profile}-{arm}-bec.json'
                merged.parent.mkdir(exist_ok=True)
                merged.write_text(json.dumps({'context': {'original_trial_contexts': contexts}, 'benchmarks': google_rows}, indent=2) + '\n')
                google_inputs.append((arm, merged))
            for family, sets in case_sets.items():
                if any(cases != sets[0] for cases in sets):
                    raise ValueError(f'changed cases: {label}/{profile}/{family}')
            for family, sets in fixtures.items():
                if any(metadata != sets[0] for metadata in sets):
                    raise ValueError(f'changed fixture, checksum, extent or phase: {label}/{profile}/{family}')
            google = args.output / 'google' / f'{label}-{profile}'
            shared.evidence.summarize(google_inputs, google)
            with (google / 'cases.csv').open(newline='') as handle:
                for row in csv.DictReader(handle):
                    for metric in ['cpu_ns', 'real_ns', 'ns_per_item']:
                        if row[metric]:
                            values = json.loads(row[metric])
                            if len(values) != trials:
                                raise ValueError('incomplete Bec trials')
                            for trial, value in enumerate(values):
                                add(label, profile, 'bec256', row['case'], metric, row['input'], trial, value)

    records = []
    for key, arms in sorted(samples.items()):
        if any(set(arm) != set(range(trials_expected)) for arm in arms.values()):
            raise ValueError(f'incomplete paired trials: {key}')
        left, right = [arms['before'][i] for i in range(trials_expected)], [arms['after'][i] for i in range(trials_expected)]
        before, after = shared.stats(left), shared.stats(right)
        records.append(dict(zip(['member', 'profile', 'family', 'case', 'metric'], key)) |
                       {'trials': trials_expected, 'before': before, 'after': after,
                        'after_over_before': shared.stats([b / a for a, b in zip(left, right)]),
                        'ratio_of_medians': after['median'] / before['median']})
    (args.output / 'paired-cases.json').write_text(json.dumps(records, indent=2) + '\n')
    fields = ['member', 'profile', 'family', 'case', 'metric', 'trials', 'ratio_of_medians']
    fields += [f'{arm}_{stat}' for arm in ['before', 'after', 'after_over_before'] for stat in ['samples', 'median', 'min', 'max', 'mad']]
    with (args.output / 'paired-cases.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        for record in records:
            row = {key: value for key, value in record.items() if key in fields}
            for arm in ['before', 'after', 'after_over_before']:
                row |= {f'{arm}_{key}': json.dumps(value) if isinstance(value, list) else value for key, value in record[arm].items()}
            writer.writerow(row)
    provenance = {'runner_sha256': shared.sha(Path(__file__)), 'shared_reader_sha256': shared.sha(Path(shared.__file__)),
                  'sources': sources, 'cases_and_metrics': len(records), 'paired_cases_sha256': shared.sha(args.output / 'paired-cases.csv'),
                  'ratio_direction': 'after/before; below one is less elapsed time',
                  'dispersion': 'Five independent counterbalanced process trials; unscaled MAD, min/max, paired ratios and ratio of medians retained. Internal rounds/repeats remain distinct. No confidence interval or pooled speedup inferred.',
                  'units': 'Bec uses existing Google evidence helper CPU/real time and item rates. HT ns is wall ns per operation, including allocation in build/grow/destroy. Stream wall ns per value retains dependent/independent and repeat distinctions.'}
    (args.output / 'provenance.json').write_text(json.dumps(provenance, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'output': str(args.output), 'cases_and_metrics': len(records)}, indent=2))


if __name__ == '__main__':
    main()
