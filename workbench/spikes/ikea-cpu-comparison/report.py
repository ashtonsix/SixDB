#!/usr/bin/env python3
"""Rebuild and check the dated six-CPU comparison from retained repetitions."""

import argparse
from collections import Counter, defaultdict
import copy
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from evidence import verify_compact

CPUS = ['v2', 'v3', 'gnr', 'zen4', 'zen5', 'm1']
SUITES = {'seriespack': 150, 'tuplepack': 61, 'words': 40, 'bec256': 78}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def same(actual, expected, where):
    if isinstance(expected, dict):
        require(actual.keys() == expected.keys(), f'{where}: fields differ')
        for key in expected:
            same(actual[key], expected[key], f'{where}/{key}')
    elif isinstance(expected, list):
        require(len(actual) == len(expected), f'{where}: lengths differ')
        for i, (a, e) in enumerate(zip(actual, expected)):
            same(a, e, f'{where}/{i}')
    elif isinstance(expected, float):
        require(math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-14),
                f'{where}: {actual} != {expected}')
    else:
        require(actual == expected, f'{where}: {actual} != {expected}')


def geometric(values):
    return math.exp(statistics.mean(map(math.log, values)))


def quantile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    low = int(position)
    return ordered[low] + (ordered[min(low + 1, len(ordered) - 1)] - ordered[low]) * (position - low)


def wins(matrix):
    counts, close = [0] * len(CPUS), 0
    for row in matrix:
        order = sorted(range(len(CPUS)), key=row.__getitem__, reverse=True)
        if row[order[0]] > 1.03 * row[order[1]]:
            counts[order[0]] += 1
        else:
            close += 1
    return {'clear': counts, 'no_clear': close}


def pairwise(matrix):
    return [[None if i == j else sum(row[i] > 1.03 * row[j] for row in matrix)
             for j in range(len(CPUS))] for i in range(len(CPUS))]


def rebuild(directory):
    manifest = verify_compact(directory)
    load = lambda name: json.loads((directory / name).read_text())
    original = load('plot-data.json')
    context = load('measurement-context.json')
    selected = load('measurement-summary.json')
    catalog_clocks = json.loads(Path(__file__).with_name('catalog-clocks.json').read_text())
    same(catalog_clocks, {cpu: context['pricing'][cpu]['product']['product']['attributes']['clockSpeed']
                          for cpu in CPUS}, 'Captured catalog clocks')
    require([p['id'] for p in original['processors']] == CPUS, 'Processor order changed')
    with (directory / 'cases.csv').open(newline='') as handle:
        cases = list(csv.DictReader(handle))
    keys = {(r['suite'], r['case']) for r in cases}
    require(len(keys) == len(cases) == 329, 'Expected 329 distinct matched cases')
    require(Counter(r['suite'] for r in cases) == SUITES, 'Suite coverage changed')

    samples, counters, count = defaultdict(dict), {}, 0
    with (directory / 'iterations.csv').open(newline='') as handle:
        for r in csv.DictReader(handle):
            case = r['suite'], r['case']
            require(case in keys and r['cpu'] in CPUS, 'Unmatched sample')
            trial, repetition = int(r['trial']), int(r['repetition'])
            require(trial in ([1] if r['cpu'] in ['v2', 'v3'] else [1, 2]), 'Unexpected trial')
            key = r['cpu'], *case, trial
            require(repetition not in samples[key], f'Duplicate repetition: {key}')
            time = float(r['cpu_ns'])
            require(math.isfinite(time) and time > 0, f'Invalid CPU time: {key}')
            samples[key][repetition] = time
            logical = json.loads(r['logical_counters'])
            same(logical, counters.setdefault(case, logical), f'Logical counters/{case}')
            count += 1
    require(count == 16450, 'Expected 16,450 selected repetitions')
    core, variability = [], {}
    for r in cases:
        medians = {}
        for cpu in CPUS:
            trial_medians = []
            for trial in ([1] if cpu in ['v2', 'v3'] else [1, 2]):
                values = samples[cpu, r['suite'], r['case'], trial]
                require(set(values) == set(range(5)), 'Missing/extra repetitions')
                median = statistics.median(values.values())
                trial_medians.append(median)
                if cpu not in ['v2', 'v3']:
                    same(median, float(r[f'{cpu}_trial{trial}_ns']), f'{cpu}/trial{trial}/{r["case"]}')
            medians[cpu] = statistics.median(trial_medians)
            same(medians[cpu], float(r[f'{cpu}_ns']), f'{cpu}/{r["case"]}')
        core.append([medians['v3'] / medians[cpu] for cpu in CPUS])
    for cpu in CPUS[2:]:
        changes = [(float(r[f'{cpu}_trial2_ns']) / float(r[f'{cpu}_trial1_ns']) - 1) * 100 for r in cases]
        variability[cpu] = {'median_trial_change_pct': statistics.median(changes),
                            'p90_abs_trial_change_pct': quantile(list(map(abs, changes)), .9),
                            'cases_above_5pct': sum(abs(x) > 5 for x in changes)}
    same(variability, selected['variability'], 'Trial repeatability')

    source = context['retained_baseline_references']['members']['c9g']['source_sha256']
    require(context['retained_baseline_references']['members']['c8g']['source_sha256'] == source,
            'ARM source identities differ')
    prices = []
    for p in original['processors']:
        cpu = p['id']
        if cpu in context['receipts']:
            receipt = context['receipts'][cpu]
            require(receipt.get('historical_source_sha256', receipt.get('source_sha256')) == source,
                    f'{cpu}: historical source differs')
            require(receipt['status'] == 'complete' and len(receipt['checks']) == 4
                    and all(c['status'] == 'passed' for c in receipt['checks']), f'{cpu}: checks incomplete')
        price = context['pricing'][cpu]
        product = price['product']
        rates = [float(d['pricePerUnit']['USD'])
                 for term in product['terms']['OnDemand'].values()
                 for d in term['priceDimensions'].values()
                 if d['unit'] == 'Hrs' and float(d['pricePerUnit']['USD']) > 0]
        require(len(rates) == 1, f'{cpu}: ambiguous captured price')
        cores = 8 if cpu == 'm1' else price['hardware']['DefaultCores']
        same(rates[0] / cores, p['usd_core_hour'], f'{cpu}/core price')
        same(rates[0] / p['listed_vcpus'], p['usd_vcpu_hour'], f'{cpu}/vCPU price')
        require(p['allocated_cores'] == cores, f'{cpu}: core allocation differs')
        prices.append(rates[0] / cores)
    dollar = [[v * prices[1] / prices[i] for i, v in enumerate(row)] for row in core]
    alternate = [row[:-1] + [row[-1] / 2] for row in dollar]
    rebuilt = copy.deepcopy(original)
    for i, p in enumerate(rebuilt['processors']):
        vals = [row[i] for row in core]
        p.update(core_index=geometric(vals), core_p10=quantile(vals, .1), core_p90=quantile(vals, .9),
                 dollar_index=geometric([row[i] for row in dollar]))
        if p['id'] == 'm1':
            p['dollar_index_4core_proxy'] = p['dollar_index'] / 2
    order, groups = [], []
    for i, suite in enumerate(SUITES):
        members = [j for j, r in enumerate(cases) if r['suite'] == suite]
        members.sort(key=lambda j: (geometric(core[j][2:5]) / geometric([core[j][k] for k in [0, 1, 5]]),
                                    cases[j]['case']))
        order.extend(members)
        groups.append({'label': chr(65 + i), 'cases': len(members),
                       'core_indices': [geometric([core[j][k] for j in members]) for k in range(6)],
                       'dollar_indices': [geometric([dollar[j][k] for j in members]) for k in range(6)]})
    rebuilt.update(groups=groups, case_core_indices=[core[j] for j in order],
                   case_dollar_indices=[dollar[j] for j in order], core_wins=wins(core),
                   dollar_wins=wins(dollar), dollar_wins_4core_proxy=wins(alternate),
                   pairwise_core_wins=pairwise(core), pairwise_dollar_wins=pairwise(dollar))
    same(rebuilt, original, 'Plot data')
    for key in ['processors', 'groups', 'core_wins', 'dollar_wins']:
        same(rebuilt[key], selected[key], f'Measurement summary/{key}')
    return rebuilt, {'cases': len(cases), 'repetitions': count, 'historical_source_sha256': source,
                     'verified_files': manifest['files_sha256'], 'variability': variability,
                     'checks': ['retained hashes', 'matched medians and trial coverage', 'logical counters',
                                'recorded source/check identities', 'captured hourly price allocation and catalog clocks',
                                'all aggregate/group indices, distributions and strict 3% wins']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plot', action='store_true', help='Also render PNG/SVG (requires Matplotlib)')
    args = parser.parse_args()
    require(args.output.resolve() != args.evidence.resolve(), 'Do not overwrite the report inputs')
    data, validation = rebuild(args.evidence.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    for name, value in [('plot-data.json', data), ('validation.json', validation)]:
        (args.output / name).write_text(json.dumps(value, indent=2) + '\n')
    if args.plot:
        subprocess.run([sys.executable, str(Path(__file__).with_name('plot.py')),
                        str(args.output / 'plot-data.json'), '--output', str(args.output / 'comparison')], check=True)
    print(f'Verified {validation["cases"]} matched cases and {validation["repetitions"]:,} repetitions')


if __name__ == '__main__':
    main()
