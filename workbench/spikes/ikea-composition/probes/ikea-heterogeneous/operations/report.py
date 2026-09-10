#!/usr/bin/env python3
"""Validate timing work and retain a predetermined compact algebra comparison."""
import csv
from collections import defaultdict
from fractions import Fraction
import json
import math
from pathlib import Path
import re
import statistics
import sys
import tarfile
ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from evidence import verify_compact

# Fixed before the first campaign; the complete sweep remains in the bundle.
KEEP_DATASETS = {'structural', 'random_dense', 'terminal_mix', 'census-income', 'census-income_srt', 'msmarco'}
KEEP_MASKS = {'none', 'all', 'single255', 'cluster32', 'dispersed32'}
KEEP_CONFIGS = {'plain', 'local-point', 'local-frame', 'local-inline', 'mixed-frame', 'mixed-point-frame', 'mixed-frame-point', 'bec-plain-frame'}
# The two captured bench revisions have 11 and 13 configurations. Values are
# (BEC operands, plain operands, cached metadata readers), not a kernel model.
CONFIGS = {
    'plain': (0, 2, 0), 'direct': (2, 0, 0),
    'local-point': (2, 0, 0), 'local-frame': (2, 0, 2), 'local-inline': (2, 0, 2),
    'scan-point': (2, 0, 0), 'scan-frame': (2, 0, 2),
    'mixed-point': (2, 0, 0), 'mixed-frame': (2, 0, 2),
    'mixed-point-frame': (2, 0, 1), 'mixed-frame-point': (2, 0, 1),
    'bec-plain-point': (1, 1, 0), 'bec-plain-frame': (1, 1, 1),
}
FIRST_CONFIGS = CONFIGS.keys() - {'mixed-point-frame', 'mixed-frame-point'}
# Fixed masks have exact selected-slice and visited-checkpoint cardinalities.
# Candidate masks depend on the third source and retain only general bounds.
MASKS = {'none': (0, 0), 'all': (256, 16), 'single255': (1, 1),
         'boundary7': (7, 7), 'cluster32': (32, 2), 'dispersed32': (32, 16),
         'alternating128': (128, 16), 'third_candidates': None}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def captured_repetitions(directory):
    """Use captured CLI options when present; a standalone CSV has no receipt."""
    for name in ('provenance.json', 'run.json'):
        path = directory / name
        if path.exists():
            options = json.loads(path.read_text())['config'].get('bench', [])
            repetitions = 5
            for index, option in enumerate(options):
                if option == '--quick': repetitions = 1
                elif option == '--repetitions': repetitions = int(options[index + 1])
            return repetitions
    return None


def algebra_inventory(directory, rows, compact):
    """Recover the exact generator inventory from a raw capture where possible.

    A compact export has no source archive; its hashed rows must match one of
    the two supported selections, then every dataset must have that inventory.
    """
    observed = {r['configuration'] for r in rows}
    source = directory / 'source.tar.gz'
    configs = None
    if source.exists():
        with tarfile.open(source) as archive:
            # Historical captures predate the move under the owning spike.
            suffixes = ('workbench/spikes/ikea-heterogeneous/operations/bench.cpp',
                        'workbench/spikes/ikea-composition/probes/ikea-heterogeneous/operations/bench.cpp')
            names = [n for n in archive.getnames() if n.endswith(suffixes)]
            require(len(names) == 1, 'missing or ambiguous captured algebra bench')
            text = archive.extractfile(names[0]).read().decode()
        definition = text.split('const Configuration configurations[] = {', 1)[1].split('};', 1)[0]
        configs = set(re.findall(r'\{"([a-z-]+)"', definition))
        require(configs in (FIRST_CONFIGS, CONFIGS.keys()), 'unknown captured configuration inventory')
    if configs is None:
        options = [set(FIRST_CONFIGS), set(CONFIGS)]
        if compact: options = [option & KEEP_CONFIGS for option in options]
        require(observed in options, 'missing or unknown configuration inventory')
        configs = observed
    elif compact:
        configs &= KEEP_CONFIGS
    masks = KEEP_MASKS if compact else MASKS.keys()
    expected = {(mask, operation, config, policy)
                for mask in masks for operation in ('union', 'intersection') for config in configs
                for policy in (('complete', 'selected') if config in ('plain', 'local-frame') else ('complete',))}
    inventories = defaultdict(set)
    for row in rows:
        inventories[row['dataset']].add(tuple(row[k] for k in ('mask', 'operation', 'configuration', 'policy')))
    require(all(inventory == expected for inventory in inventories.values()), 'incomplete algebra case inventory')


def read(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def write(path, rows):
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)


def validate(rows, keys, unit, repetitions=None):
    require(rows, 'empty timings')
    groups = defaultdict(list)
    dataset_items = defaultdict(set)
    for r in rows:
        calls, passes, items, elapsed = (int(r[k]) for k in ['calls', 'passes', unit, 'elapsed_ns'])
        require(passes > 0 and items > 0 and calls == passes * items and elapsed > 0, 'invalid call accounting')
        duration = float(r['ns_per_call'])
        require(math.isfinite(duration) and abs(duration - elapsed / calls) <= max(1e-6, elapsed / calls * 1e-5), 'invalid ns/call normalization')
        require(r['residence'] == 'unestablished', 'unknown cache-residence claim')
        status = r['pmu_status']
        counters = [int(r[k]) for k in ('cycles_raw', 'instructions_raw', 'enabled_ns', 'running_ns')]
        cycles, instructions, enabled, running = counters
        require(all(value >= 0 for value in counters), 'negative PMU count')
        if status == 'available':
            require(0 < running <= enabled and cycles > 0 and instructions > 0, 'invalid available PMU counts')
        elif status == 'not_scheduled':
            require(running == 0, 'scheduled PMU marked unscheduled')
        else:
            require(status in {'not_requested', 'enable_failed', 'read_failed'} or re.fullmatch(r'unavailable_[1-9][0-9]*', status), 'unknown PMU status')
            require(not any(counters), 'unavailable PMU has measured counters')
        require(int(r['major_faults']) >= 0 and int(r['minor_faults']) >= 0, 'negative page-fault count')
        dataset_items[r['dataset']].add(items)
        groups[tuple(r[k] for k in keys)].append(r)
    require(all(len(items) == 1 for items in dataset_items.values()), 'inconsistent dataset item count')
    if repetitions is None:
        repetitions = max(int(r['repetition']) for r in rows) + 1
    require(repetitions > 0, 'no repetitions')
    for samples in groups.values():
        require(sorted(int(r['repetition']) for r in samples) == list(range(repetitions)), 'incomplete or duplicate repetitions')
        require(len({r['passes'] for r in samples}) == 1, 'passes changed between repetitions')
    return groups


def summary(groups, keys):
    result = []
    for key, samples in sorted(groups.items()):
        values = [float(r['ns_per_call']) for r in samples]
        # Scale multiplexed counters; this is unchanged when enabled == running.
        cycles = [int(r['cycles_raw']) / int(r['calls']) * (int(r['enabled_ns']) / int(r['running_ns']))
                  for r in samples if r['pmu_status'] == 'available']
        result.append(dict(zip(keys, key), repetitions=len(samples), median_ns=statistics.median(values),
            min_ns=min(values), max_ns=max(values), cycles_per_call=statistics.median(cycles) if cycles else 'NA'))
    return result


def algebra(directory):
    full = directory / 'algebra-timings.csv'
    rows = read(full if full.exists() else directory / 'algebra-selected.csv')
    keys = ['dataset', 'mask', 'operation', 'configuration', 'policy']
    repetitions = captured_repetitions(directory)
    validate(rows, keys, 'pairs', repetitions)
    algebra_inventory(directory, rows, not full.exists())
    equivalence = defaultdict(set)
    accounting, content, extents = defaultdict(set), defaultdict(set), defaultdict(set)
    for r in rows:
        calls, selected = int(r['calls']), int(r['selected_slices'])
        passes, pairs = int(r['passes']), int(r['pairs'])
        bec, plain, frames = (int(r[k]) for k in ('bec_decodes', 'plain_loads', 'metadata_frames'))
        bec_sources, plain_sources, cached_sources = CONFIGS[r['configuration']]
        require(0 <= selected <= calls * 256, 'selected slices exceed source extent')
        require(0 <= bec <= bec_sources * selected and 0 <= plain <= plain_sources * selected, 'impossible operand accounting')
        require(0 <= frames <= calls * 16 * cached_sources, 'impossible metadata frame count')
        require(all(value % passes == 0 for value in (selected, bec, plain, frames)), 'logical work is not a whole number per pass')
        require(int(r['output_bytes']) == pairs * 8192, 'invalid output extent')
        sizes = tuple(int(r[k]) for k in ('left_bytes', 'right_bytes'))
        require(all(size > 0 for size in sizes), 'nonpositive input extent')
        if r['configuration'] == 'plain':
            require(plain == 2 * selected and sizes == (pairs * 8192, pairs * 8192), 'invalid plain work or extent')
        fixed = MASKS[r['mask']]
        if fixed is not None:
            require(selected == calls * fixed[0] and frames == calls * fixed[1] * cached_sources, 'fixed mask cardinality/frame mismatch')
        key = tuple(r[k] for k in ('dataset', 'mask', 'operation', 'configuration'))
        accounting[key].add(tuple(Fraction(value, calls) for value in (selected, bec, plain, frames)))
        content[(r['dataset'], r['mask'], r['operation'], bec_sources)].add(tuple(Fraction(value, calls) for value in (selected, bec, plain)))
        extents[(r['dataset'], r['configuration'])].add(sizes)
        key = (r['dataset'], r['mask'], r['operation'], r['policy'])
        equivalence[key].add((r['checksum'], Fraction(selected, calls), r['pairs']))
    require(all(len(v) == 1 for v in equivalence.values()), 'representation work/output mismatch')
    require(all(len(v) == 1 for v in accounting.values()), 'logical work changed between repetitions/output policies')
    require(all(len(v) == 1 for v in content.values()), 'operand work changed between metadata representations')
    require(all(len(v) == 1 for v in extents.values()), 'input extents changed between cases')
    chosen = [r for r in rows if r['dataset'] in KEEP_DATASETS and r['mask'] in KEEP_MASKS and r['configuration'] in KEEP_CONFIGS]
    require(chosen, 'no compact selection')
    write(directory / 'algebra-selected.csv', chosen)
    summaries = summary(validate(chosen, keys, 'pairs'), keys)
    for r in summaries:
        matching = [p for p in summaries if p['configuration'] == 'plain' and all(p[k] == r[k] for k in ['dataset', 'mask', 'operation', 'policy'])]
        require(len(matching) == 1, 'missing plain reference')
        r['relative_to_plain'] = r['median_ns'] / matching[0]['median_ns']
    write(directory / 'algebra-summary.csv', summaries)
    lines = ['# Masked algebra: selected comparisons', '',
        'Median ns per pair of 65,536-position sources. Same dataset/mask/operation/policy has identical output and selected logical work across representations. Pass counts are calibrated per arm; normalize by calls. Complete establishes inactive zeros; selected leaves inactive bytes unchanged. Cache residence is unestablished. Full sweep is in the retained bundle.', '',
        '| Dataset | Mask | Operation | Configuration | Output policy | Median ns | Min–max ns | Relative to plain |',
        '| --- | --- | --- | --- | --- | ---: | ---: | ---: |']
    for r in summaries:
        lines.append(f"| {r['dataset']} | {r['mask']} | {r['operation']} | {r['configuration']} | {r['policy']} | {r['median_ns']:.3f} | {r['min_ns']:.3f}–{r['max_ns']:.3f} | {r['relative_to_plain']:.3f} |")
    (directory / 'algebra-summary.md').write_text('\n'.join(lines) + '\n')
    print(f'Algebra: validated {len(rows)} rows; retained {len(chosen)} repetitions / {len(summaries)} cases')


def analyser(directory):
    rows = read(directory / 'analyser-timings.csv')
    keys = ['dataset', 'model', 'scan', 'stage']
    groups = validate(rows, keys, 'windows', captured_repetitions(directory))
    expected_cases = {(model, scan, stage) for model in ('cheap', 'quadrants') for scan in ('full', 'sample32')
                      for stage in ('predict', 'predict-then-encode')} | {('cheap', 'full', 'encode')}
    inventories = defaultdict(set)
    for r in rows:
        inventories[r['dataset']].add(tuple(r[k] for k in ('model', 'scan', 'stage')))
    require(all(cases == expected_cases for cases in inventories.values()), 'incomplete analyser case inventory')
    for samples in groups.values():
        expected = set()
        for r in samples:
            calls = int(r['calls']); conversions = int(r['conversions'])
            require(0 <= conversions <= calls and conversions % int(r['passes']) == 0, 'invalid conversion accounting')
            if r['stage'] == 'predict': require(conversions == 0, 'prediction emitted conversions')
            if r['stage'] == 'encode': require(conversions == calls, 'encode omitted conversions')
            expected.add((Fraction(int(r['checksum']), int(r['passes'])), Fraction(conversions, calls)))
        require(len(expected) == 1, 'analyser work/output changed between repetitions')
    summaries = summary(groups, keys)
    write(directory / 'analyser-summary.csv', summaries)
    lines = ['# Whole-bitset analyser timing', '',
        'Median ns per 8,192-byte input. Predict includes native feature extraction and frozen model evaluation. Encode is dense BEC body emission only. Predict-then-encode includes the decision at predicted body +512 metadata +64 suffix <8192 and body emission when chosen; metadata emission/allocation/admission are excluded. Cache residence is unestablished.', '',
        '| Dataset | Model | Scan | Stage | Median ns | Min–max ns |',
        '| --- | --- | --- | --- | ---: | ---: |']
    for r in summaries:
        lines.append(f"| {r['dataset']} | {r['model']} | {r['scan']} | {r['stage']} | {r['median_ns']:.3f} | {r['min_ns']:.3f}–{r['max_ns']:.3f} |")
    (directory / 'analyser-summary.md').write_text('\n'.join(lines) + '\n')
    print(f'Analyser: validated {len(rows)} rows / {len(summaries)} cases')


def main(directory):
    directory = Path(directory)
    if (directory / 'provenance.json').exists(): verify_compact(directory)
    if (directory / 'algebra-timings.csv').exists() or (directory / 'algebra-selected.csv').exists(): algebra(directory)
    if (directory / 'analyser-timings.csv').exists(): analyser(directory)


if __name__ == '__main__':
    main(sys.argv[1])
