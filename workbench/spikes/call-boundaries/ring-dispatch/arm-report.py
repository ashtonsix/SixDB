#!/usr/bin/env python3
"""Recompute ARM-gap comparisons from every hashed selected input, without timing.

Analysis code is owned by the current checkout. Captured source/native receipts
are verified as evidence, not misrepresented as an offline full-archive audit.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

import arm_analysis


def check(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_name(name):
    check(isinstance(name, str), 'non-string selected input path')
    path = Path(name)
    check(not path.is_absolute() and '..' not in path.parts
          and bool(path.parts), 'unsafe selected input path')
    return path


class Inputs:
    def __init__(self, root):
        self.root = root
        self.index = json.loads((root / 'recovery.json').read_text())
        check(self.index['format'] == 1 and not self.index.get('pending'), 'incomplete selection')
        self.members = {}
        for member in self.index['payloads']:
            name = member['path']
            path = root / safe_name(name)
            check(name not in self.members, 'duplicate indexed input')
            check(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(root),
                  'missing or redirected input: ' + name)
            check(path.stat().st_size == member['bytes'] and sha(path) == member['sha256'],
                  'changed selected input: ' + name)
            self.members[name] = member
        self.reads = set()

    def path(self, name, origin=None):
        check(name in self.members, 'unindexed report input: ' + str(name))
        if origin is not None:
            check(any(all(item.get(k) == v for k, v in origin.items())
                      for item in self.members[name]['origins']), 'wrong input origin: ' + name)
        self.reads.add(name)
        return self.root / safe_name(name)

    def json(self, name, origin=None):
        return json.loads(self.path(name, origin).read_text())


def divide(numerator, denominator):
    check(math.isfinite(numerator) and math.isfinite(denominator)
          and numerator > 0 and denominator > 0, 'invalid ratio operand')
    value = numerator / denominator
    check(math.isfinite(value) and value > 0, 'nonfinite ratio')
    return value


def finite_difference(first, second):
    value = first - second
    check(math.isfinite(value), 'nonfinite timing difference')
    return value


def comparison(values, reference):
    return {
        'ring_over_own_manual': divide(values['ring'], values['manual']),
        'ring_hints_over_own_manual_hints': divide(values['ring_hints'], values['manual_hints']),
        'own_manual_hints_gap_ns': finite_difference(values['ring_hints'], values['manual_hints']),
        'ring_over_reference_ring': divide(values['ring'], reference['ring']),
        'ring_hints_over_reference_ring_hints': divide(values['ring_hints'], reference['ring_hints']),
        'ring_hints_over_reference_manual_hints': divide(values['ring_hints'], reference['manual_hints']),
        'reference_manual_hints_gap_ns': finite_difference(values['ring_hints'], reference['manual_hints']),
    }


def summarize(samples):
    check(all(values and all(math.isfinite(v) and v > 0 for v in values)
              for values in samples.values()), 'invalid sample set')
    return {arm: statistics.median(values) for arm, values in samples.items()}


def qualify_job(inputs, selected):
    qualification = inputs.json(selected['qualification'])
    check(qualification['job'] == selected['job'] and qualification['label'] == selected['label'],
          'source qualification identity')
    source = qualification['source']
    archive = qualification['worker_archive']
    origin = {'kind': 'worker_archive', 'job': selected['job'], 'archive_sha256': archive['sha256']}
    receipts = selected['retained_receipts']

    def member(name):
        return inputs.path(receipts[name], origin | {'member': name})

    provenance = json.loads(member('control-provenance.json').read_text())
    check(provenance == source['provenance'], 'frozen provenance differs from qualification')
    runtime = json.loads(member('job.json').read_text())
    check(runtime['id'] == selected['job'] and runtime['source_commit'] == source['source_commit']
          and runtime['source']['digest'] == source['content_digest']
          and runtime['source']['sha256'] == source['source_archive_sha256'], 'runtime source identity')
    result = json.loads(member('worker-result.json').read_text())
    check(result['state'] == 'complete' and result['script_returncode'] == 0, 'incomplete runtime')
    before = json.loads(member('source-before.json').read_text())
    after = json.loads(member('source-after.json').read_text())
    check(before == after and before == source['source_stability'], 'captured source stability')
    env = runtime['config']['env']
    reps = int(env.get('REPS', env.get('ARM_SERIAL_REPS', provenance['default_repetitions'])))
    passes = int(env.get('PASSES', env.get('ARM_SERIAL_PASSES', provenance['default_passes'])))
    check(reps > 0 and passes in (1, 2), 'invalid repetitions/passes')
    isa = selected['isa']
    hardware = json.loads(member('hardware-' + isa + '.json').read_text())
    order = list(csv.DictReader(member('executable-order.csv').open()))
    factors = [factor['name'] for factor in provenance['factors']]
    targets = {factor['name']: factor['target'] for factor in provenance['factors']}
    check(len(set(factors)) == len(factors) and factors, 'duplicate/empty factor inventory')
    definitions = {case['name']: case for case in provenance['case_order']}
    check(len(definitions) == len(provenance['case_order']), 'duplicate case definition')
    expected = {(name, passed, factor) for name in definitions for passed in range(passes) for factor in factors}
    descriptors = {}
    for cell in selected['cells']:
        key = (cell['case'], cell['pass'], cell['factor'])
        check(key not in descriptors, 'duplicate selected cell')
        descriptors[key] = cell
    check(set(descriptors) == expected and len(order) == len(expected), 'missing/extra cell coverage')
    order_keys = []
    for row in order:
        key = (row['case'], int(row['pass']), row['factor'])
        check(key in expected and key not in order_keys, 'unexpected/duplicate execution order cell')
        case = definitions[row['case']]
        check(row['isa'] == isa and row['target'] == targets[row['factor']]
              and int(row['reps']) == reps and int(row['seed']) == provenance['seed'], 'execution qualifiers')
        check(tuple(int(row[field]) for field in ('a', 'entries', 'turnovers', 'mixed', 'concurrency'))
              == tuple(case[field] for field in ('a', 'entries', 'turnovers', 'mixed', 'concurrency')),
              'wrong case/age/query mode in execution order')
        order_keys.append(key)
    ordered_expected = []
    for case in provenance['case_order']:
        for passed in range(passes):
            sequence = case['pass_orders'][passed] if 'pass_orders' in case else (
                case['factor_order'] if passed == 0 else case['factor_order'][::-1])
            check(set(sequence) == set(factors) and len(sequence) == len(factors), 'factor order definition')
            for number, factor in enumerate(sequence):
                key = (case['name'], passed, factor)
                check(descriptors[key]['order'] == number, 'wrong within-case factor order')
                ordered_expected.append(key)
    check(order_keys == ordered_expected, 'execution order does not follow frozen opposite passes')
    if selected.get('role') == 'final' and isa == 'neon':
        check(reps == 7 and passes == 2, 'final V2 requires7 repetitions and opposite passes')
    raw_cells, total = {}, 0
    for name, case in definitions.items():
        reference = None
        for passed in range(passes):
            for factor in factors:
                descriptor = descriptors[(name, passed, factor)]
                params = {'a': case['a'], 'entries': case['entries'], 'turnovers': case['turnovers'],
                          'mixed': case['mixed'], 'concurrency': case['concurrency'], 'reps': reps,
                          'seed': provenance['seed']}
                check(descriptor['parameters'] == params and descriptor['target'] == targets[factor]
                      and descriptor['isa'] == isa, 'wrong selected case/reps/target')
                stem = isa + '-' + name + '-' + factor + '-pass' + str(passed)
                csv_path = inputs.path(descriptor['csv'], origin | {'member': stem + '.csv'})
                context_path = inputs.path(descriptor['context'], origin | {'member': stem + '.context'})
                policy = inputs.json(descriptor['policy'], origin | {'member': stem + '.policy.json'})
                check(policy['csv_sha256'] == sha(csv_path) and policy['context_sha256'] == sha(context_path),
                      'policy/raw hash mismatch')
                check(policy['parameters'] == {'entries': case['entries'], 'concurrency': case['concurrency'],
                      'repetitions': reps, 'turnovers': case['turnovers'], 'mixed': case['mixed'],
                      'seed': provenance['seed']}, 'policy parameter mismatch')
                verified = arm_analysis.check_cell(csv_path, context_path,
                    (name, case['a'], case['entries'], case['concurrency']), reps,
                    reference=reference, generation=case['turnovers'], mixed=bool(case['mixed']),
                    frame_reference=None if reference is None else reference['frame_dimensions'])
                check(policy['rows'] == verified['rows'], 'policy row count')
                check(hardware['behavior_context'] == verified['context']['hardware_context']
                      and not hardware['calibration_enabled'], 'hardware context mismatch')
                check(policy['policy'] == verified['context'], 'policy/context content mismatch')
                if reference is None:
                    reference = verified
                else:
                    check(reference['context'] == verified['context'], 'factor/pass hardware-policy context drift')
                raw_cells[(name, passed, factor)] = verified
                total += verified['rows']
    forced = []
    check({f['factor'] for f in selected['forced']} == set(factors), 'missing forced factor')
    forced_reference = None
    for descriptor in selected['forced']:
        stem = 'check-' + isa + '-' + descriptor['factor']
        verified = arm_analysis.check_cell(inputs.path(descriptor['csv'], origin | {'member': stem + '.csv'}),
            inputs.path(descriptor['context'], origin | {'member': stem + '.context'}),
            ('forced', 5, 701, 8), 1, generation=2, mixed=True, reference=forced_reference,
            frame_reference=None if forced_reference is None else forced_reference['frame_dimensions'])
        if forced_reference is None:
            forced_reference = verified
        forced.append({'factor': descriptor['factor'], 'rows': verified['rows'],
                       'independent_work': verified['independent_work'], 'physical': verified['physical']})
    check(total == selected['timing_rows'], 'selected row count changed')
    reported = []
    for name, case in definitions.items():
        by_factor = {}
        for factor in factors:
            sets = {str(p): raw_cells[(name, p, factor)]['samples_ns'] for p in range(passes)}
            sets['merged'] = {arm: [value for p in range(passes) for value in sets[str(p)][arm]]
                              for arm in arm_analysis.ARMS}
            by_factor[factor] = {'samples_ns': sets, 'medians_ns': {p: summarize(s) for p, s in sets.items()}}
        reference_factor = factors[0]
        for factor in factors:
            item = by_factor[factor]
            item['comparisons'] = {p: comparison(values, by_factor[reference_factor]['medians_ns'][p])
                                   for p, values in item['medians_ns'].items()}
            if passes == 2:
                ratios = {arm: divide(item['comparisons']['1'][arm], item['comparisons']['0'][arm])
                          for arm in ('ring_over_reference_ring', 'ring_hints_over_reference_ring_hints')}
                item['pass_ratio_drift'] = {'pass1_over_pass0_factor_ratio': ratios,
                    'over_5_percent': any(abs(r - 1) > .05 for r in ratios.values()),
                    'scope': 'Descriptive order drift, not a significance test.'}
        reference = raw_cells[(name, 0, reference_factor)]
        reported.append({'case': name, 'parameters': case, 'reference_factor': reference_factor,
                         'independent_work': reference['independent_work'], 'physical': reference['physical'],
                         'frame_dimensions': reference['frame_dimensions'], 'factors': by_factor})
    return {'label': selected['label'], 'job': selected['job'], 'role': selected.get('role', 'diagnostic'),
            'isa': isa, 'repetitions': reps,
            'passes': passes, 'rows': total, 'cells': reported, 'forced': forced,
            'source_identity': {'commit': source['source_commit'], 'content_digest': source['content_digest'],
                                'source_archive_sha256': source['source_archive_sha256']},
            'capture_qualification': {'receipt': selected['qualification'],
                'scope': 'Captured independent full-source, compiler, binary/native and lifecycle qualification. '
                         'This report verified receipt identity; it did not fetch/recheck full archives or run executables.'}}


def owner_reference(inputs, spec):
    """The current owner's tagged CSV has a different input law and no checksum."""
    origin = {'kind': 'worker_archive', 'job': spec['job'],
              'archive_sha256': spec['worker_archive']['sha256']}
    runtime = inputs.json(spec['receipts']['job.json'], origin | {'member': 'job.json'})
    check(runtime['source'] == spec['source'] and runtime['source_commit'] == spec['source_commit']
          and int(runtime['config']['env']['HASHTABLE_REPEATS']) == spec['repetitions'], 'owner source/reps identity')
    outcome = inputs.json(spec['receipts']['worker-result.json'], origin | {'member': 'worker-result.json'})
    check(outcome['state'] == 'complete' and outcome['script_returncode'] == 0, 'owner runtime incomplete')
    native = inputs.json(spec['qualification'])
    check(native['worker'] == spec['job'] and spec['source']['digest'].startswith(native['owning_source_capture_prefix']),
          'owner native/source receipt identity')
    rows = list(csv.reader(inputs.path(spec['csv'], origin | {'member': 'large.csv'}).open()))
    arms = ('abseil', 'abseil_prefetch', 'direct', 'manual', 'manual_hints', 'ring', 'ring_hints')
    cells, contexts, populations, workloads = {}, {}, {}, {}
    for row in rows:
        check(row and row[0] in ('population', 'geometry', 'context', 'workload', 'time', 'close'), 'owner CSV record type')
        if row[0] == 'population':
            check(len(row) == 7 and sum(map(int, row[4:7])) == int(row[2]), 'owner population')
            key = (row[1], int(row[2]), int(row[3]))
            check(key not in populations, 'duplicate owner population')
            populations[key] = list(map(int, row[4:]))
        elif row[0] == 'geometry':
            check(len(row) == 15 and row[4] in ('primary', 'secondary')
                  and sum(map(int, row[5:8])) == 40, 'owner geometry')
        elif row[0] in ('context', 'workload'):
            key = (row[1], int(row[2]), int(row[3]), int(row[4]))
            destination = contexts if row[0] == 'context' else workloads
            check(key not in destination, 'duplicate owner accounting')
            destination[key] = row
        elif row[0] == 'time':
            check(len(row) == 13 and row[6] in arms, 'owner timing schema')
            key = (row[1], int(row[2]), int(row[3]), int(row[4]))
            cells.setdefault(key, []).append(row)
        elif row[0] == 'close':
            check(len(row) == 6 and math.isfinite(float(row[5])) and float(row[5]) > 0, 'owner close duration')
    result = []
    for key, data in cells.items():
        check(key in contexts and key in workloads and (key[0], key[1], key[3]) in populations, 'missing owner accounting')
        workload = workloads[key]
        context = contexts[key]
        check(len(context) == 16, 'owner context schema')
        line, page, hint = map(int, context[6:9])
        check(line > 0 and line & (line - 1) == 0 and page >= line and page & (page - 1) == 0
              and hint in (0, 1, 2, 3) and int(context[10]) >= int(context[9]), 'owner geometry/allocation context')
        check(len(workload) == 9, 'owner workload schema')
        requests, hits, misses, graphs = map(int, workload[5:9])
        check(requests == hits + misses and hits == key[1] * graphs
              and (misses == 0 if key[3] == 0 else 0 < misses <= hits), 'owner workload counts')
        reps = spec['repetitions']
        check(len(data) == 7 * reps, 'owner repetitions')
        stages = set()
        samples = {arm: [] for arm in arms}
        for rep in range(reps):
            group = [row for row in data if int(row[5]) == rep]
            check(len(group) == 7 and {r[6] for r in group} == set(arms), 'owner arm coverage')
        for row in data:
            check(int(row[7]) == requests, 'owner timed requests')
            ns = float(row[8])
            check(math.isfinite(ns) and ns > 0, 'owner timing value')
            primary, secondary, directory, payload = map(int, row[9:13])
            check(primary == requests and secondary <= primary and directory == payload, 'owner logical stages')
            stages.add((primary, secondary, directory, payload))
            samples[row[6]].append(ns)
        check(len(stages) == 1, 'owner stages differ across arms')
        medians = summarize(samples)
        result.append({'layout': key[0], 'entries': key[1], 'concurrency': key[2], 'mixed': bool(key[3]),
                       'repetitions': reps, 'samples_ns': samples, 'medians_ns': medians,
                       'comparisons': comparison(medians, medians), 'logical_stages': list(next(iter(stages))),
                       'workload': {'requests': requests, 'hits': hits, 'misses': misses, 'graphs': graphs}})
    return {'label': spec['label'], 'job': spec['job'], 'source': spec['source'], 'rows': sum(map(len, cells.values())),
            'cells': result, 'qualification_receipt': spec['qualification'],
            'limits': ['Separate owning source/input/layout; no old-vs-current absolute timing comparison.',
                       'Tagged current-owner CSV exposes workload/stage accounting but no checksum or terminal. '
                       'The old40-bit shuffled-cycle law is not applied to make_input<39>(N,638251).',
                       'Captured executable owns full-answer and exact Ring-visit checks; this report recomputed raw samples/accounting only.']}


def selected_proof(inputs, jobs):
    paths = inputs.index.get('selected_proof')
    if not paths:
        return None
    proof = inputs.json(paths['selector_proof'])
    check(proof['source_differences'] == ['workbench/spikes/hashtable/table_ring.cpp']
          and proof['source_files_compared_excluding_git'] == 3160, 'selected source/proof scope')
    check(sha(inputs.path(paths['patch'])) == proof['patch_sha256'], 'selected patch mismatch')
    for platform in ('arm', 'x86'):
        choice = proof['token_comparisons'][platform]
        selected = inputs.path(paths[platform + '_selected'])
        expected = inputs.path(paths[platform + '_expected'])
        check(choice['exact_equal'] and selected.read_bytes() == expected.read_bytes()
              and sha(selected) == choice['selected']['sha256']
              and sha(expected) == choice['expected']['sha256'], 'selected/native token mismatch')
    forms = proof['selection']
    check(forms == {'ARM_hinted': 'plain semantic branch', 'ARM_none': 'original reference expression',
                    'ARM_fused': 'default reference expression',
                    'x86_all': 'original literal template/call syntax and expression'},
          'unexpected architecture/hint selection')
    comparison = inputs.json(paths['function_comparison'])
    literals = inputs.json(paths['literal_relocations'])
    functions = comparison['comparisons']
    check(len(functions) == 95 and len({f['symbol_sha256'] for f in functions}) == 95,
          'selected function coverage')
    counts = {}
    unequal = set()
    for function in functions:
        kind, hint = function['kind'], function['hint']
        check(kind in ('ready', 'manual', 'fused') and hint in (None, 0, 1, 2, 3), 'unknown selected function')
        check((hint is None) == (kind == 'fused'), 'selected function hint scope')
        form = 'reference' if hint in (None, 0) else 'plain'
        check(function['expected'] == form, 'selected function maps to wrong native form')
        check(function['selected_padding_nops'] == function['expected_padding_nops'], 'selected padding differs')
        equal = function['selected_core_sha256'] == function['expected_core_sha256']
        check(function['exact_normalized_equal'] == equal, 'selected instruction identity receipt')
        if not equal:
            check(kind == 'manual' and hint != 0, 'unexpected selected instruction difference')
            unequal.add(function['symbol_sha256'])
        counts[(kind, form)] = counts.get((kind, form), 0) + 1
    check(counts == {('ready', 'reference'): 5, ('ready', 'plain'): 15,
                     ('manual', 'reference'): 15, ('manual', 'plain'): 45,
                     ('fused', 'reference'): 15}, 'selected function population')
    check(len(literals) == 45 and len(unequal) == 45
          and {item['symbol_sha256'] for item in literals} == unequal
          and len(comparison['differences']) == 45, 'cold literal difference coverage')
    check(all(item['equal_bytes'] and item['literal_sha256'] == proof['native_local_function_comparison']['literal_sha256']
              for item in literals), 'selected cold literal differs')
    native = proof['native_local_function_comparison']
    check(native['functions'] == 95 and native['padding_count_differences'] == 0
          and native['ordinary_calls_or_frame_added_to_ready'] == 0, 'selected codegen receipt scope')
    final_jobs = [j for j in jobs if j['role'].startswith('final') or j['role'] == 'negative-final']
    check(final_jobs and all(j['source_identity']['commit'] == proof['native_parent_commit'] for j in final_jobs),
          'selected proof does not match native parent captures')
    return {'reproduction_commit': proof['reproduction_commit'], 'native_parent_commit': proof['native_parent_commit'],
            'superseded_broad_guard_commit': proof['previous_broad_guard_commit'],
            'selected_consumer_sha256': proof['selected_consumer_sha256'], 'selected_forms': forms,
            'captured_local_function_comparisons': len(functions), 'verified_cold_literal_differences': len(literals),
            'current_ikea': 'Already competitive owner implementation remains unchanged.',
            'scope': 'Pinned component token identity plus captured source/function comparison receipts. '
                     'This report checks receipt identity and consistency; it does not regenerate omitted assembly. '
                     'The narrowed reproduction capture was not rerun; no new native measurement or whole-binary equality claim.'}


def report(root):
    inputs = Inputs(root)
    print(f'verified {len(inputs.members)} indexed payloads; recomputing key-law samples', flush=True)
    jobs = [qualify_job(inputs, job) for job in inputs.index['jobs']]
    references = [owner_reference(inputs, ref) for ref in inputs.index.get('owner_references', [])]
    selection = selected_proof(inputs, jobs)
    total = sum(job['rows'] for job in jobs)
    check(total == inputs.index['counts']['timing_rows'], 'indexed total row count')
    output = {'status': 'recomputed_from_verified_selected_inputs', 'rows': total, 'jobs': jobs,
              'owner_references': references, 'indexed_payloads_verified': len(inputs.members),
              'selected_architecture_forms': selection,
              'indexed_inputs_read': len(inputs.reads), 'recovery_sha256': sha(root / 'recovery.json'),
              'report_sha256': sha(Path(__file__)), 'analysis_sha256': sha(Path(arm_analysis.__file__)),
              'limits': inputs.index['limits']}
    destination = root / 'reports'
    destination.mkdir(exist_ok=True)
    (destination / 'cells.json').write_text(json.dumps(output, indent=2, allow_nan=False) + '\n')
    with (destination / 'endpoint-medians.csv').open('w') as stream:
        fields = ['label', 'job', 'isa', 'case', 'factor', 'pass', 'arm', 'ns',
                  'own_manual_hints_gap_ns', 'reference_manual_hints_gap_ns',
                  'ring_over_own_manual', 'ring_hints_over_own_manual_hints',
                  'ring_hints_over_reference_ring_hints']
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for job in jobs:
            for cell in job['cells']:
                for factor, value in cell['factors'].items():
                    for passed, medians in value['medians_ns'].items():
                        for arm, ns in medians.items():
                            ratios = value['comparisons'][passed]
                            writer.writerow({key: job[key] for key in ('label', 'job', 'isa')} |
                                {'case': cell['case'], 'factor': factor, 'pass': passed, 'arm': arm, 'ns': ns} |
                                {key: ratios[key] for key in fields[8:]})
    print(f'recomputed {total} campaign rows; owner reference rows {sum(r["rows"] for r in references)}; {destination}', flush=True)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    report(parser.parse_args().evidence.resolve())
