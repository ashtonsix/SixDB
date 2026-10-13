#!/usr/bin/env python3
"""Independent qualification/recomputation of frozen cold-factor worker rows."""
import argparse
import csv
import functools
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

MODES = ('reference', 'sync', 'tail', 'vector', 'snapshot', 'slim')
ARMS = ('abseil', 'abseil-prefetch', 'packed', 'direct', 'manual', 'manual-hints', 'ring', 'ring-hints')
NAME = re.compile(r'^(neon|avx2|avx512)-cold_(reference|sync|tail|vector|snapshot|slim)_grain(1|4)-(\d+)-(\d+)-pass([01])\.csv$')
MASK = (1 << 64) - 1

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def check(condition, message):
    if not condition:
        raise ValueError(message)

@functools.lru_cache(None)
def checksum(n):
    # Independent key-set law: shuffled complete cycle visits every mixed key
    # exactly once, so permutation order and chain width cannot change the sum.
    total = 0
    for i in range(1, n + 1):
        value = i ^ (i >> 30)
        value = value * 0xbf58476d1ce4e5b9 & MASK
        value ^= value >> 27
        value = value * 0x94d049bb133111eb & MASK
        total += value ^ (value >> 31)
    return total & MASK

def context(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            check(key not in result, f'duplicate context key {key}: {path}')
            result[key] = value
    return result

def rows(path, ctx, n, chains, grain, reps):
    data = list(csv.DictReader(Path(path).open()))
    check(len(data) == 8 * reps, f'row count {path}')
    keys = [(int(row['rep']), row['execution']) for row in data]
    check(len(set(keys)) == len(keys) and set(keys) == {(r, a) for r in range(reps) for a in ARMS}, f'arm/rep set {path}')
    # Independently check the driver's documented alternating arm sequence.
    check(keys == [(r, a) for r in range(reps) for a in (ARMS if r % 2 == 0 else ARMS[::-1])], f'arm order {path}')
    effective = min(chains, grain)
    groups = (chains + grain - 1) // grain
    rounds = max(1, 262144 // n)
    expected = checksum(n)
    for row in data:
        check((int(row['n']), int(row['chains']), int(row['configured_grain']), int(row['effective_grain']), int(row['groups'])) ==
              (n, chains, grain, effective, groups), f'dimensions {path}')
        check(int(row['checksum']) == expected, f'independent key-set checksum {path}')
        check(int(row['rounds']) == rounds, f'rounds {path}')
        ring = row['execution'] in ('ring', 'ring-hints')
        useful = 2 * n * rounds if ring else 0
        check(int(row['useful_item_stage_visits']) == useful and int(row['ring_visits']) == useful // effective, f'visit accounting {path}')
        check(row['useful_item_stage_visits_origin'] == 'derived_equal_lengths', f'visit origin {path}')
        value = float(row['ns_per_lookup'])
        check(math.isfinite(value) and value > 0, f'invalid timing {path}')
        check(int(row['hint_code']) == int(ctx['selected_hint']), f'hint selection {path}')
        check(int(row['primary_keys']) + int(row['secondary_keys']) + int(row['overflow_keys']) == n, f'Map population {path}')
        check(int(row['key_input_bytes']) == 8 * n, f'key input size {path}')
    check(ctx['context_transfer'] == 'shared_prefix' and ctx['hint_window_contract'] == 'cold_immutable_geometry_positive', f'context contract {path}')
    check(ctx['positive_window_proof'] == 'header_plus_16_and_suffix_bytes_plus_8' and ctx['ragged_check'] == 'passed', f'window/ragged qualification {path}')
    check(ctx['calibration_enabled'] == 'false' and int(ctx['pressure']) == 0, f'background/pressure {path}')
    return data

def qualify_worker(result, expected_commit):
    sources = json.loads((result / 'dispatch-sources.json').read_text())
    provenance = json.loads((result / 'cold-capture-provenance.json').read_text())
    check(sources['fixture_commit'] == expected_commit, f'wrong source commit: {result}')
    files = sources['tracked_source_sha256']
    for path, digest in provenance['source_identities'].items():
        check(files.get(path) == digest, f'provenance/header mismatch {path}: {result}')
    # Reconstruct archive identity including the self-referential manifest file.
    identity = dict(files)
    identity['controls/cold-source-manifest.json'] = sources['manifest_sha256']
    digest = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    check(set(provenance['factors']) == set(MODES), f'factor set {result}')
    check('21.1.8' in (result / 'compiler.txt').read_text(), f'compiler pin {result}')
    order = list(csv.DictReader((result / 'executable-order.csv').open()))
    return order, {'source_commit': expected_commit, 'source_digest': digest,
                   'dispatch_sources_sha256': sha(result / 'dispatch-sources.json'),
                   'provenance_sha256': sha(result / 'cold-capture-provenance.json')}

def qualifications(result, isa, source_hashes):
    normal = (result / f'loom-validation-{isa}.txt').read_text()
    sanitized = (result / f'asan-qualification-{isa}.txt').read_text()
    check(re.search(r'0 tests failed out of 16\b', normal), f'native16 not qualified {result}/{isa}')
    check(re.search(r'0 tests failed out of 6\b', sanitized), f'ASan6 not qualified {result}/{isa}')
    commands = json.loads((result / f'compile-asan-{isa}.json').read_text())
    for mode in MODES[1:]:
        for grain in (1, 4):
            target = f'hashtable_ring_cold_{mode}_grain{grain}'
            command = next(x['command'] for x in commands if target + '.dir/' in x['output'])
            check(all(x in command for x in ('-fsanitize=address,undefined', '-fsanitize-address-use-after-return=always', '-fno-omit-frame-pointer')), f'sanitizer flags {target}')
            for prefix in ('forced', 'asan-forced'):
                stdout = (result / f'{prefix}-{isa}-{target}.txt').read_text()
                stderr = (result / f'{prefix}-{isa}-{target}.context').read_text()
                check('PASS' in stdout, f'forced failure {target}')
                for token in ('primary_hits=16', 'primary_misses=1', 'downstream_hits=112', 'downstream_misses=1', 'real_secondary=32', 'real_overflow=80'):
                    check(token in stdout, f'forced coverage {target}: {token}')
                for case in ('ragged0_cancel0', 'ragged1_cancel0', 'ragged1_cancel1'):
                    check(f'cold_semantics_case={case}' in stderr, f'forced lifecycle {target}')
    codegen = json.loads((result / f'cold-codegen-{isa}.json').read_text())
    check(set(codegen) == {f'cold_{m}_grain{g}' for m in MODES for g in (1, 4)}, f'codegen set {isa}')
    for policy, record in codegen.items():
        check(sha(result / f'assembly-{isa}-{policy}.txt') == record['assembly_sha256'], f'assembly changed {policy}')
        for path, digest in record['selected_headers'].items():
            check(source_hashes[path] == digest, f'codegen included header {policy}/{path}')
        common = [x for x in record['bodies'] if x['phase'] in ('metadata', 'payload')]
        check(common, f'no native common bodies {policy}')
        if record['mode'] in ('tail', 'vector', 'snapshot', 'slim'):
            check(not any(x['ordinary_calls'] for x in common), f'ordinary hot call {policy}')
        if record['mode'] == 'snapshot':
            check(any(x['phase'] == 'finish_cold' for x in record['bodies']), 'snapshot ordinary helper omitted')
    return {'native_checks': 16, 'sanitizer_checks': 6, 'forced_release': 10, 'forced_sanitized': 10,
            'codegen_sha256': sha(result / f'cold-codegen-{isa}.json')}

def analyze(args):
    all_cells, workers = [], []
    total_rows = 0
    for directory in args.results:
        result = Path(directory).resolve()
        order, source = qualify_worker(result, args.commit)
        source_hashes = json.loads((result / 'dispatch-sources.json').read_text())['tracked_source_sha256']
        records = {}
        for row in order:
            isa, n, chains, passed = row['isa'], int(row['n']), int(row['chains']), int(row['pass'])
            policy, reps = row['policy'], int(row['reps'])
            match = re.fullmatch(r'cold_(reference|sync|tail|vector|snapshot|slim)_grain(1|4)', policy)
            check(match and passed in (0, 1) and reps == args.reps and int(row['window_mode']) == 1, 'execution identity')
            mode, grain = match[1], int(match[2])
            key = (isa, n, chains, passed, mode, grain)
            check(key not in records, f'duplicate executable {key}')
            path = result / f'{isa}-{policy}-{n}-{chains}-pass{passed}.csv'
            ctx = context(path.with_suffix('.context'))
            hardware = json.loads((result / f'hardware-{isa}.json').read_text())
            check(hardware['behavior_context'] == ctx['hardware_context'] and not hardware['calibration_enabled'], f'hardware context {path}')
            data = rows(path, ctx, n, chains, grain, reps)
            total_rows += len(data)
            records[key] = {'rows': data, 'context': ctx, 'csv': str(path), 'csv_sha256': sha(path),
                            'context_sha256': sha(path.with_suffix('.context')), 'order': int(row['order'])}
        isas = sorted({key[0] for key in records})
        measured_files = {path.name for path in result.glob('*.csv') if NAME.fullmatch(path.name)}
        listed_files = {Path(value['csv']).name for value in records.values()}
        check(measured_files == listed_files, f'unlisted/duplicate measurement files {result}')
        worker = {'results': str(result), **source, 'isas': {}}
        for isa in isas:
            worker['isas'][isa] = qualifications(result, isa, source_hashes)
            required = {(isa, n, c, p, m, g) for n in args.sizes for c in args.chains for p in (0, 1) for m in MODES for g in (1, 4)}
            check({k for k in records if k[0] == isa} == required, f'missing/extra cells {result}/{isa}')
            for n in args.sizes:
                for chains in args.chains:
                    policy_contexts = {tuple(value['context'][field] for field in ('hardware_context','calibration_enabled','policy_revision','pressure','line_bytes','page_bytes','demand_code','selected_hint'))
                                       for key, value in records.items() if key[:3] == (isa,n,chains)}
                    check(len(policy_contexts) == 1, f'hardware/policy context differs across factors/passes {result}/{isa}/{n}/{chains}')
                    physical = {tuple(int(row[field]) for field in ('packed_requested_bytes','direct_requested_bytes','abseil_requested_bytes','key_input_bytes','primary_keys','secondary_keys','overflow_keys'))
                                for key, value in records.items() if key[:3] == (isa,n,chains) for row in value['rows']}
                    check(len(physical) == 1, f'physical work differs across factors/passes {result}/{isa}/{n}/{chains}')
                    for passed in (0, 1):
                        sequence = [(m, g) for g in (1, 4) for m in MODES]
                        if passed: sequence.reverse()
                        check([records[(isa, n, chains, passed, m, g)]['order'] for m, g in sequence] == list(range(12)), 'factor pass order')
                    for grain in (1, 4):
                        medians = {}
                        inputs = []
                        for mode in MODES:
                            medians[mode] = {}
                            for passed in (0, 1):
                                entry = records[(isa, n, chains, passed, mode, grain)]
                                inputs.append({k: entry[k] for k in ('csv', 'csv_sha256', 'context_sha256', 'order')})
                                medians[mode][str(passed)] = {a: statistics.median(float(x['ns_per_lookup']) for x in entry['rows'] if x['execution'] == a) for a in ARMS}
                            medians[mode]['merged'] = {a: statistics.median(float(x['ns_per_lookup']) for p in (0, 1) for x in records[(isa,n,chains,p,mode,grain)]['rows'] if x['execution'] == a) for a in ARMS}
                        comparisons = {}
                        for newer, older in (('sync','reference'), ('tail','sync'), ('vector','tail'), ('snapshot','vector'), ('slim','tail')):
                            values = {p: {a: medians[newer][p][a] / medians[older][p][a] for a in ('ring','ring-hints')} for p in ('0','1','merged')}
                            values['pass1_over_pass0_ratio'] = {a: values['1'][a] / values['0'][a] for a in ('ring','ring-hints')}
                            values['direction_drift_flag'] = any(abs(values['pass1_over_pass0_ratio'][a] - 1) > .05 for a in ('ring','ring-hints'))
                            comparisons[newer + '_over_' + older] = values
                        ring_over_manual = {m: {p: {'unhinted': medians[m][p]['ring']/medians[m][p]['manual'], 'hinted': medians[m][p]['ring-hints']/medians[m][p]['manual-hints']} for p in ('0','1','merged')} for m in MODES}
                        all_cells.append({'worker_results': str(result), 'isa': isa, 'n': n, 'chains': chains, 'grain': grain,
                                          'independent_checksum': checksum(n), 'medians_ns': medians, 'factor_ratios': comparisons,
                                          'ring_over_actual_manual': ring_over_manual, 'inputs': inputs})
        workers.append(worker)
    if args.expected_rows is not None:
        check(total_rows == args.expected_rows, f'total row count {total_rows}, expected {args.expected_rows}')
    check(len({worker['source_digest'] for worker in workers}) == 1, 'worker source content differs')
    if args.source_digest is not None:
        check(workers[0]['source_digest'] == args.source_digest, 'wrong captured source digest')
    return {'status': 'qualified', 'script_sha256': sha(__file__), 'source_commit': args.commit,
            'rows': total_rows, 'workers': workers, 'cells': all_cells,
            'limits': ['Factor ratios below1 favor numerator; compare per pass before merged medians. Five-percent direction flag is descriptive, not statistical significance.',
                       'sync/reference changes Frame/loop shape; tail/sync isolates cold transfer within matched Frame; vector/tail includes prepared-kernel/fact hoisting; snapshot/vector adds Frame borrow/ordinary helper; slim/tail changes completion-pass fusion.',
                       'Actual manual curves are retained per executable/pass; identical source bodies do not guarantee identical placement or speed.',
                       'Native visits are measured; useful stage work is analytically derived for equal lengths. No per-item counter, PMU, cache-tier or stall inference.',
                       'Snapshot cold accounting includes ordinary finish_cold. ARM helper stack list is SP-only; full disassembly is required for x29-relative spills.']}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', nargs='+', help='Actual worker results directories')
    parser.add_argument('--commit', required=True)
    parser.add_argument('--source-digest', help='Expected reconstructed archive content digest, independently of its recorded commit')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reps', type=int, default=7)
    parser.add_argument('--sizes', type=int, nargs='+', default=[128, 4194304])
    parser.add_argument('--chains', type=int, nargs='+', default=[1, 32])
    parser.add_argument('--expected-rows', type=int)
    arguments = parser.parse_args()
    report = analyze(arguments)
    arguments.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f"qualified {report['rows']} rows in {len(report['cells'])} cells; {arguments.output}")
