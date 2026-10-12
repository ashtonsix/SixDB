#!/usr/bin/env python3
"""Summarize conditioned single-trigger experiments; preserve all raw trials."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

ARMS = ('none', 'demand', 'prefetcht0', 'prefetcht1', 'demand_reflush', 'hot')
TARGETS = ('neighbor', 'far', 'anchor')
FIELDS = ('location', 'anchor_parity', 'delta_lines', 'target_kind', 'arm', 'delay_ns')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def quantiles(values):
    ordered = sorted(values)
    if not ordered or any(not math.isfinite(x) or x < 0 for x in ordered):
        raise ValueError('invalid latency distribution')
    return {name: ordered[min(len(ordered) - 1, int(fraction * (len(ordered) - 1)))]
            for name, fraction in (('p01', .01), ('p10', .1), ('p50', .5), ('p90', .9), ('p99', .99))}


def classify(cold, hot, tested):
    # This is an observation threshold, not a hardware detector or confidence
    # probability. Ambiguous or poorly separated controls remain unknown.
    span = cold['p50'] - hot['p50']
    if span <= 0 or cold['p10'] <= 1.5 * hot['p90']:
        return 'unknown', None
    saving = (cold['p50'] - tested['p50']) / span
    if saving >= .3:
        return 'observed_benefit', saving
    if saving < .15:
        return 'no_observed_benefit', saving
    return 'unknown', saving


def summarize(root):
    groups = defaultdict(lambda: [[], [], []])
    pooled = defaultdict(lambda: [[], [], []])
    configurations = []
    raw_hashes = {}
    total = 0
    for path in sorted(root.glob('seed-*.csv')):
        metadata_path = path.with_suffix('.txt')
        metadata = metadata_path.read_text()
        config_line = next(line for line in metadata.splitlines() if line.startswith('config '))
        config = dict(re.findall(r'(\w+)=([^\s]+)', config_line))
        seed = int(config['seed'])
        factor = float(config['ns_per_tick'])
        line_bytes = int(config['line_bytes'])
        page_bytes = int(config['page_bytes'])
        trials = int(config['trials_per_arm'])
        expected = int(config['reps']) * int(config['trials_per_rep'])
        count = 0
        sequences = defaultdict(set)
        with path.open() as stream:
            for row in csv.DictReader(stream):
                rep = int(row['rep'])
                sequence = int(row['sequence'])
                if sequence in sequences[rep]:
                    raise ValueError(f'{path}: repeated trial sequence')
                sequences[rep].add(sequence)
                if row['arm'] not in ARMS or row['target_kind'] not in TARGETS or row['value_ok'] != '1':
                    raise ValueError(f'{path}: invalid control or value')
                if row['start_aux'] != config['aux'] or row['end_aux'] != config['aux']:
                    raise ValueError(f'{path}: timestamp stamp/sentinel mismatch')
                anchor = int(row['anchor_offset'])
                target = int(row['target_offset'])
                if (anchor // line_bytes) % 2 != int(row['anchor_parity']):
                    raise ValueError(f'{path}: parity mismatch')
                if int(anchor // page_bytes != target // page_bytes) != int(row['page_cross']):
                    raise ValueError(f'{path}: page boundary mismatch')
                if row['target_kind'] == 'neighbor' and target - anchor != int(row['delta_lines']) * line_bytes:
                    raise ValueError(f'{path}: neighbor distance mismatch')
                if row['target_kind'] == 'anchor' and target != anchor:
                    raise ValueError(f'{path}: anchor positive control mismatch')
                if row['target_kind'] == 'far' and abs(target - anchor) <= 8 * line_bytes:
                    raise ValueError(f'{path}: far control aliases the flushed neighborhood')
                key = tuple(row[name] for name in FIELDS)
                values = (int(row['target_ticks']) * factor, int(row['trigger_ticks']) * factor,
                          int(row['trigger_to_target_ticks']) * factor)
                for i, value in enumerate(values):
                    groups[(seed, rep, *key)][i].append(value)
                    pooled[key][i].append(value)
                count += 1
        if count != expected or any(sorted(s) != list(range(int(config['trials_per_rep'])))
                                    for s in sequences.values()):
            raise ValueError(f'{path}: incomplete trial plan')
        for key, vectors in groups.items():
            if key[0] == seed and len(vectors[0]) != trials:
                raise ValueError(f'{path}: missing balanced arm')
        total += count
        if config.get('architecture') == 'aarch64' and 'affinity_single_cpu=true' not in metadata:
            raise ValueError(f'{path}: missing final single-CPU affinity receipt')
        configurations.append(config | {'rows_verified': count})
        raw_hashes[path.name] = sha(path)
        raw_hashes[metadata_path.name] = sha(metadata_path)
    if not configurations:
        raise ValueError('no native raw trials found')
    distributions = {key: [quantiles(v) for v in vectors] for key, vectors in groups.items()}
    with (root / 'distributions.csv').open('w') as stream:
        names = ['seed', 'rep', *FIELDS, 'n', 'target_p01_ns', 'target_p10_ns', 'target_p50_ns',
                 'target_p90_ns', 'target_p99_ns', 'trigger_p50_ns', 'actual_slack_p10_ns',
                 'actual_slack_p50_ns', 'actual_slack_p90_ns']
        writer = csv.writer(stream)
        writer.writerow(names)
        for key, vectors in sorted(groups.items()):
            target, trigger, slack = distributions[key]
            writer.writerow([*key, len(vectors[0]), *target.values(), trigger['p50'],
                             slack['p10'], slack['p50'], slack['p90']])
    with (root / 'pooled.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow([*FIELDS, 'n', 'target_p01_ns', 'target_p10_ns', 'target_p50_ns',
                         'target_p90_ns', 'target_p99_ns', 'actual_slack_p50_ns'])
        for key, vectors in sorted(pooled.items()):
            target, _, slack = [quantiles(v) for v in vectors]
            writer.writerow([*key, len(vectors[0]), *target.values(), slack['p50']])
    observations = []
    conditions = sorted({(key[2], key[3], key[4], key[5], key[7]) for key in groups})
    seed_reps = sorted({key[:2] for key in groups})
    for location, parity, delta, target_kind, delay in conditions:
        for arm in ('demand', 'prefetcht0', 'prefetcht1', 'demand_reflush'):
            rows = []
            for seed, rep in seed_reps:
                prefix = (seed, rep, location, parity, delta, target_kind)
                cold = distributions[(*prefix, 'none', delay)][0]
                hot = distributions[(*prefix, 'hot', delay)][0]
                tested, trigger, slack = distributions[(*prefix, arm, delay)]
                result, saving = classify(cold, hot, tested)
                rows.append({'seed': seed, 'rep': rep, 'observation': result,
                             'normalized_saving': saving, 'cold_p50_ns': cold['p50'],
                             'hot_p50_ns': hot['p50'], 'target_p50_ns': tested['p50'],
                             'trigger_p50_ns': trigger['p50'], 'actual_slack_p50_ns': slack['p50']})
            results = {row['observation'] for row in rows}
            observations.append(dict(zip(('location', 'parity', 'delta_lines', 'target_kind', 'delay_ns'),
                                          (location, int(parity), int(delta), target_kind, int(delay)))) |
                                {'arm': arm, 'observation': next(iter(results)) if len(results) == 1 else 'unknown',
                                 'repetitions': rows})
    architectures = {config.get('architecture', 'x86_64') for config in configurations}
    if len(architectures) != 1:
        raise ValueError('mixed architecture experiment bundle')
    architecture = next(iter(architectures))
    report = {'format': 1, 'verified_rows': total, 'configurations': configurations,
              'binary_sha256': sha(root / 'adjacent'), 'report_sha256': sha(Path(__file__)),
              'raw_sha256': raw_hashes, 'observations': observations,
              'runtime_forward': json.loads((root / 'runtime-forward.json').read_text())
                  if (root / 'runtime-forward.json').exists() else None,
              'runtime_checks_sha256': sha(root / 'runtime-checks.txt')
                  if (root / 'runtime-checks.txt').exists() else None,
              'architecture': architecture,
              'arm_instructions': {'prefetcht0': 'PRFM PLDL1KEEP', 'prefetcht1': 'PRFM PLDL2KEEP'}
                  if architecture == 'aarch64' else {'prefetcht0': 'PREFETCHT0', 'prefetcht1': 'PREFETCHT1'},
              'method_source': 'https://tschlueter.com/research/publications/23-fetchbench/ccs23-fetchbench.pdf',
              'limits': ['Cache-maintenance-conditioned trials, not reset hidden prefetch history',
                         ('CNTVCT converted with reported CNTFRQ and checked against wall time; timer quantum limits resolution'
                          if architecture == 'aarch64' else 'TSC converted to wall nanoseconds') +
                         '; not core-cycle or PMU/stall measurement',
                         ('ARM has no per-load CPU stamp: CSV AUX values are sentinels; single-CPU affinity checked at start/end'
                          if architecture == 'aarch64' else 'RDTSCP CPU stamps checked on every measured load'),
                         'Only one observer target per trial; separate anchor validates software-prefetch response',
                         'Zero requested delay has instrumentation slack; actual intervals are retained',
                         'Page boundary is virtual; physical page adjacency is unknown',
                         'Benefit is an observation under this history; cache level and named prefetch engine remain unknown',
                         'No thermal, SMT-neighbor or system-wide pressure guarantee; machine/kernel context is retained',
                         ('Runtime helper scheduling/training values are explicitly synthetic premises to isolate forward learning'
                          if (root / 'runtime-forward.json').exists() else
                          'Standalone diagnostic only; production runtime calibration was not invoked')]}
    (root / 'adjacent-analysis.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Verified {total} raw rows; {len(observations)} conditioned observations', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    summarize(parser.parse_args().directory.resolve())
