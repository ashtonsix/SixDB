#!/usr/bin/env python3
"""Read all three-arm predicate trials; duplicate/before is an uncorrected control.

Inputs are neon=RESULTROOT zen=RESULTROOT. Filters report wall ns/window;
blocks report CPU ns/traversal and their checked inverse-rate counters. No
metrics, commands or trials are pooled into a speed score.
Copied-projection receipts additionally require their qualification evidence;
only that declared variant permits the recorded per-arm predicate storage change.
"""
import argparse
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import re
import shlex
import statistics

ARMS = ('before', 'after', 'duplicate')
FILTER_KEY = ('workload', 'width_or_cases', 'op', 'active', 'candidate', 'metric')
ORDERS = [['before', 'after', 'duplicate'], ['after', 'duplicate', 'before'],
          ['duplicate', 'before', 'after'], ['duplicate', 'after', 'before'],
          ['after', 'before', 'duplicate']]
HEADER = 'ikea/include/ikea/tuplepack/filter.h'
PROJECTION = 'copied-projection-final'
COUNTERS = ('blocks', 'queries_per_block', 'width_transitions', 'shared_plan_storage_bytes',
            'fixture_object_bytes', 'retained_predicate_bytes', 'payload_grant_bytes',
            'payload_allocation_bytes')
BLOCK_CASES = {'blocks/' + '/'.join((r, t, p, 'q' + str(q)))
               for r, t, p, q in itertools.product(('retained', 'reprepare', 'oneshot', 'fresh'),
                   ('clustered', 'bursty', 'shuffled'), ('tight', 'strided', 'mixed'), (1, 8, 64))}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(value):
    return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def positive(value):
    value = float(value)
    require(math.isfinite(value) and value > 0, f'nonpositive/nonfinite value: {value}')
    return value


def close(actual, expected):
    require(math.isclose(positive(actual), expected, rel_tol=1e-6, abs_tol=1e-9),
            f'inconsistent work/rate: {actual} != {expected}')


def stats(values):
    middle = statistics.median(values)
    return {'samples': values, 'median': middle, 'min': min(values), 'max': max(values),
            'mad': statistics.median(abs(v - middle) for v in values)}


def worst(values, index):
    case, after, duplicate = max(values, key=lambda value: value[index])
    return {'case': case, 'paired_after_over_before': after, 'paired_duplicate_over_before': duplicate}


def filters(path):
    result = {}
    with path.open(newline='') as handle:
        for row in csv.DictReader(handle):
            key = tuple(row[k] for k in FILTER_KEY)
            require(row['rep'] == '0' and key not in result, f'repeated filter command: {path}/{key}')
            result[key] = {'wall_ns_per_window': positive(row['ns_window'])}
    require(len(result) == 360, f'expected360 filter commands: {path}')
    return result


def blocks(path):
    result, structure = {}, {}
    for row in json.loads(path.read_text())['benchmarks']:
        if row.get('run_type', 'iteration') == 'aggregate':
            continue
        name = row.get('run_name', row['name'])
        require(row.get('run_type', 'iteration') == 'iteration' and not row.get('error_occurred'),
                f'failed/noniteration benchmark: {path}/{name}')
        require(name in BLOCK_CASES and name not in result and row.get('threads', 1) == 1 and
                row['time_unit'] == 'ns', f'unknown/repeated benchmark or unit: {path}/{name}')
        iterations = positive(row['iterations'])
        require(iterations.is_integer(), f'fractional iterations: {name}')
        cpu, real = positive(row['cpu_time']), positive(row['real_time'])
        fields = name.split('/')
        route, trace, placement, queries = fields[1], fields[2], fields[3], int(fields[4][1:])
        counters = {k: float(row[k]) for k in COUNTERS}
        require(all(math.isfinite(v) and v >= 0 and v.is_integer() for v in counters.values()),
                f'invalid structural counters: {path}/{name}')
        require(counters['blocks'] == 64 and counters['queries_per_block'] == queries and
                counters['width_transitions'] == {'clustered': 7, 'bursty': 31, 'shuffled': 58}[trace],
                f'changed logical work: {path}/{name}')
        require(0 < counters['shared_plan_storage_bytes'] <= counters['fixture_object_bytes'] and
                counters['payload_allocation_bytes'] >= counters['payload_grant_bytes'] ==
                {'tight': 1000, 'strided': 1936, 'mixed': 1456}[placement] and
                (counters['retained_predicate_bytes'] > 0 if route == 'retained' else
                 counters['retained_predicate_bytes'] == 0), f'changed storage work: {path}/{name}')
        close(row['ns_per_block'], cpu / 64)
        close(row['ns_per_query'], cpu / (64 * queries))
        close(row['items_per_second'], 64 * queries * 1e9 / cpu)
        result[(name,)] = {'cpu_ns_per_traversal': cpu, 'cpu_ns_per_block': positive(row['ns_per_block']),
                           'cpu_ns_per_query': positive(row['ns_per_query'])}
        structure[name] = counters
    require({k[0] for k in result} == BLOCK_CASES, f'incomplete108 block commands: {path}')
    return result, structure


def projection_qualification(directory, receipt, manifest):
    declared = receipt.get('qualification', {})
    require(declared.get('path') == 'projection-qualification.json' and declared.get('complete') is True,
            'missing projection qualification')
    path = directory / declared['path']
    require(digest(declared['sha256']) and sha(path) == declared['sha256'], 'qualification SHA mismatch')
    q = json.loads(path.read_text())
    require(q.get('complete') is True and q['variant'] == PROJECTION and
            q['profiles'] == receipt['profiles'] and set(q['core']) == set(receipt['profiles']) and
            all(q[k] == receipt[k] for k in ('source_manifest_sha256', 'candidate_sha256', 'runner_sha256')) and
            q['source_manifest_file_sha256'] == sha(directory / 'source-manifest.json') and
            q['fixture_source_sha256'] == manifest['shared_source_sha256'][
                'workbench/spikes/ikea-composition/handle-boundaries/projection-check.cpp'],
            'qualification source/profile mismatch')
    require(len(q['commands']) == 1 + 5 * len(q['profiles']) and
            all(c['exit_code'] == 0 for c in q['commands']) and
            any(c.endswith('/cohesion-v2-check.sh') for c in q['commands'][0]['command']),
            'failed/incomplete qualification commands')
    files = {declared['path']: declared['sha256']}
    public = {p for p in manifest['source_sha256']['after']
              if p.startswith('ikea/include/') and p.endswith('.h') and 'detail' not in Path(p).parts}
    for profile in q['profiles']:
        core = q['core'][profile]
        require(core['checks'] == 96 and core['public_headers'] == 93, 'incomplete core/header qualification')
        for suffix, key in (('selected.json', 'selected_receipt_sha256'), ('check.log', 'check_log_sha256'),
                            ('headers.json', 'header_receipt_sha256')):
            file = directory / 'qualification' / (profile + '-' + suffix)
            require(digest(core[key]) and sha(file) == core[key], 'qualification evidence SHA mismatch')
            files[str(file.relative_to(directory))] = core[key]
        selected = json.loads((directory / 'qualification' / (profile + '-selected.json')).read_text())
        headers = json.loads((directory / 'qualification' / (profile + '-headers.json')).read_text())['headers']
        require(len(selected) == len({s['name'] for s in selected}) == 96 and
                len(headers) == len({h['path'] for h in headers}) == 93 and
                {h['path'] for h in headers} == public and all(h['returncode'] == 0 and
                h['sha256'] == manifest['source_sha256']['after'][h['path']] for h in headers) and
                re.search(r'100% tests passed, 0 tests failed out of 96\b',
                    (directory / 'qualification' / (profile + '-check.log')).read_text()),
                'failed/incomplete core or standalone headers')
    require(len(q['fixtures']) == len(q['profiles']) and
            {f['profile'] for f in q['fixtures']} == set(q['profiles']), 'missing/repeated candidate fixture')
    for i, fixture in enumerate(q['fixtures']):
        commands = [c['command'] for c in q['commands'][1 + 5 * i:6 + 5 * i]]
        require(fixture['profile'] == q['profiles'][i] and [c[0] for c in commands] ==
                ['cmake', 'cmake', 'llvm-size-21', 'llvm-objdump-21', 'taskset'] and
                commands[1][1] == '--build' and 'ikea_predicate_projection_check' in commands[1] and
                commands[-1][1:3] == ['-c', str(receipt['cpu'])], 'candidate fixture command mismatch')
        flags = shlex.split(fixture['compile_command'])
        march, tune = {'neon': ('armv8-a+simd', 'neoverse-v2'), 'avx2': ('x86-64-v3', 'znver5'),
                       'avx512': ('znver5', 'znver5')}[fixture['profile']]
        require({'-DIKEA_PROJECTION_CANDIDATE=1', '-O3', '-g', '-std=c++23',
                 '-march=' + march, '-mtune=' + tune} <= set(flags) and
                not any(f.startswith('-DNDEBUG') for f in flags) and
                any(f.endswith('/after/ikea/include') for f in flags) and
                not any(f.endswith('/before/ikea/include') for f in flags) and
                digest(fixture['binary_sha256']) and isinstance(fixture['bytes'], int) and fixture['bytes'] > 0 and
                digest(fixture['disassembly_sha256']), 'invalid candidate fixture build')
        file = directory / 'qualification' / fixture['profile'] / 'projection/projection-check.txt'
        require(sha(file) == fixture['run_log_sha256'] and 'masks and admission passed' in file.read_text(),
                'candidate fixture run evidence mismatch')
        files[str(file.relative_to(directory))] = fixture['run_log_sha256']
    return {'sha256': declared['sha256'], 'core': q['core'], 'fixtures': q['fixtures'], 'files_sha256': files}


def retain_structure(structures, profile, arm, counters, variant):
    if variant != PROJECTION:
        require(profile not in structures or structures[profile] == counters, 'structural work changed')
        structures[profile] = counters
        return
    arms = structures.setdefault(profile, {})
    require(arm not in arms or arms[arm] == counters, 'structural work changed within arm')
    expected = 1024 if arm == 'after' else 1536
    require(all(c['retained_predicate_bytes'] == (expected if name.split('/')[1] == 'retained' else 0)
                for name, c in counters.items()), 'unexpected copied-predicate storage')
    unchanged = lambda c: {name: {k: v for k, v in row.items() if k != 'retained_predicate_bytes'}
                           for name, row in c.items()}
    require(all(unchanged(counters) == unchanged(peer) for peer in arms.values()),
            'undeclared cross-arm structural change')
    arms[arm] = counters


def source(directory):
    receipt_path, manifest_path = directory / 'receipt.json', directory / 'source-manifest.json'
    receipt, manifest = json.loads(receipt_path.read_text()), json.loads(manifest_path.read_text())
    variant = receipt.get('variant')
    require(variant == manifest.get('variant') and variant in (None, PROJECTION), 'unknown/mismatched variant')
    require(receipt.get('complete') is True and receipt.get('preflight_complete') is True and
            receipt['trials'] == 5 and receipt['timed_arms'] == list(ARMS) and
            receipt['trial_orders'] == ORDERS, f'incomplete three-arm receipt: {directory}')
    require(manifest['study'] == 'prepared-predicate-native' and
            receipt['source_manifest_sha256'] == manifest['source_manifest_sha256'] and
            receipt['candidate_sha256'] == manifest['candidate_sha256'] and
            receipt['shared_source_sha256'] == manifest['shared_source_sha256'], 'source receipt mismatch')
    old, new = manifest['source_sha256']['before'], manifest['source_sha256']['after']
    require(old.keys() == new.keys() and [k for k in old if old[k] != new[k]] == [HEADER] and
            new[HEADER] == receipt['candidate_sha256'], 'candidate is not the recorded one-header delta')
    require(all(digest(h) for h in [*old.values(), *new.values(), *receipt['shared_source_sha256'].values(),
                                    receipt['runner_sha256']]), 'invalid source digest')
    for arm, mapping in (('before', old), ('after', new)):
        mapping_digest = hashlib.sha256(json.dumps(mapping, sort_keys=True).encode()).hexdigest()
        require(mapping_digest == receipt['source_manifest_sha256'][arm], f'bad source map digest: {arm}')
    require(receipt['filter_iterations'] == 300000 and receipt['filter_repeats'] == 1 and
            receipt['blocks_filter'] == '^blocks/' and receipt['blocks_minimum_time_seconds'] >= .1 and
            receipt['commands'] and all(c['exit_code'] == 0 for c in receipt['commands']),
            'changed/incomplete command protocol')
    binaries = {(b['profile'], b['arm'], b['family']): b for b in receipt['binaries']}
    require(len(binaries) == len(receipt['binaries']) and set(binaries) == set(itertools.product(
        receipt['profiles'], ARMS, ('filters', 'blocks', 'oracles'))), 'incomplete/duplicate binary receipt')
    require(all(digest(b['sha256']) and isinstance(b['bytes'], int) and b['bytes'] > 0
                for b in binaries.values()), 'invalid binary hash/size')
    for profile in receipt['profiles']:
        for family in ('filters', 'blocks', 'oracles'):
            before, duplicate = (binaries[(profile, a, family)] for a in ('before', 'duplicate'))
            require(before['sha256'] == duplicate['sha256'] and before['bytes'] == duplicate['bytes'] and
                    duplicate.get('no_compilation') is True, f'duplicate ELF differs: {profile}/{family}')
    meta = {'directory': str(directory), 'receipt_sha256': sha(receipt_path),
        'source_manifest_file_sha256': sha(manifest_path), 'source_manifest_sha256': receipt['source_manifest_sha256'],
        'candidate_sha256': receipt['candidate_sha256'], 'runner_sha256': receipt['runner_sha256'],
        'shared_source_sha256': receipt['shared_source_sha256'], 'binaries': receipt['binaries'],
        'common_argv0': receipt['common_argv0'], 'trial_orders': ORDERS, 'cpu': receipt['cpu'],
        'protocol_limits': receipt.get('limits', ''),
        'fixture': receipt.get('fixture', False), 'raw_files_sha256': {}, 'variant': variant or 'cached-tight'}
    if variant == PROJECTION:
        meta['projection_qualification'] = projection_qualification(directory, receipt, manifest)
    return receipt, meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    inputs = [s.split('=', 1) for s in args.inputs]
    require(len(inputs) == 2 and all(len(s) == 2 for s in inputs) and
            {s[0] for s in inputs} == {'neon', 'zen'}, 'supply neon=ROOT zen=ROOT once')
    args.output.mkdir(parents=True, exist_ok=False)
    samples, sources, structures, case_sets, common = {}, {}, {}, {}, None
    for label, name in inputs:
        directory = Path(name).resolve()
        receipt, meta = source(directory)
        expected_profiles = {'neon'} if label == 'neon' else {'avx2', 'avx512'}
        require(set(receipt['profiles']) == expected_profiles and
                len(receipt['profiles']) == len(expected_profiles), 'profile mismatch')
        identity = tuple(json.dumps(meta[k], sort_keys=True) for k in ('source_manifest_sha256',
            'candidate_sha256', 'runner_sha256', 'shared_source_sha256', 'common_argv0', 'fixture', 'variant'))
        require(common is None or common == identity, 'inputs do not measure the same source/candidate/protocol')
        common = identity; sources[label] = meta
        for profile in receipt['profiles']:
            for arm, trial, family in itertools.product(ARMS, range(5), ('filters', 'blocks')):
                path = directory / profile / arm / f'{family}-trial-{trial}.{"csv" if family == "filters" else "json"}'
                meta['raw_files_sha256'][str(path.relative_to(directory))] = sha(path)
                if family == 'filters':
                    values = filters(path)
                else:
                    values, counters = blocks(path)
                    retain_structure(structures, profile, arm, counters, receipt.get('variant'))
                keys = set(values)
                require(family not in case_sets or case_sets[family] == keys, f'case sets changed: {path}')
                case_sets[family] = keys
                recorded = set(receipt['case_sets'][profile + '/' + family])
                require(recorded == {'/'.join(k) for k in keys}, f'runner case set mismatch: {path}')
                for key, metrics in values.items():
                    for metric, value in metrics.items():
                        samples.setdefault((label, profile, family, key, metric), {}).setdefault(arm, {})[trial] = value
    records, groups = [], {}
    for (label, profile, family, key, metric), arms in sorted(samples.items()):
        require(all(set(arms[a]) == set(range(5)) for a in ARMS), 'missing paired sample')
        summary = {a: stats([arms[a][i] for i in range(5)]) for a in ARMS}
        summary |= {a + '_over_before': stats([positive(arms[a][i] / arms['before'][i]) for i in range(5)])
                    for a in ('after', 'duplicate')}
        row = dict(member=label, profile=profile, family=family, case='/'.join(key), metric=metric, trials=5)
        for arm, numbers in summary.items():
            row.update({arm + '_' + k: json.dumps(v, separators=(',', ':')) if k == 'samples' else v
                        for k, v in numbers.items()})
        row.update({a + '_ratio_of_medians': positive(summary[a]['median'] / summary['before']['median'])
                    for a in ('after', 'duplicate')})
        records.append(row)
        parts = key[0].split('/')
        category = '/'.join((key[0], key[4], key[5])) if family == 'filters' else '/'.join((parts[1], parts[4]))
        group = groups.setdefault((profile, family, metric, category), [])
        group.append((row['case'], summary['after_over_before']['median'], summary['duplicate_over_before']['median']))
    destination = args.output / 'paired-cases.csv'
    with destination.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(records[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(records)
    grouped = [dict(zip(('profile', 'family', 'metric', 'category'), k)) | {'cases': len(v),
        'after_losses_ge_5_percent': sum(a >= 1.05 for _, a, _ in v),
        'duplicate_losses_ge_5_percent': sum(d >= 1.05 for _, _, d in v),
        'worst_after': worst(v, 1), 'worst_duplicate': worst(v, 2)}
        for k, v in sorted(groups.items())]
    (args.output / 'groups.json').write_text(json.dumps(grouped, indent=2) + '\n')
    provenance = {'reader_sha256': sha(Path(__file__)), 'sources': sources, 'cases_and_metrics': len(records),
        'variant': next(iter(sources.values()))['variant'],
        'case_counts': {k: len(v) for k, v in case_sets.items()}, 'structural_counters': structures,
        'filter_tuple_columns': FILTER_KEY, 'duplicate_elf_verification': 'Equality of recorded before/duplicate hashes and byte sizes; copied ELF is not recompiled.',
        'paired_cases_sha256': sha(destination), 'groups_sha256': sha(args.output / 'groups.json'),
        'limits': 'Five raw arm samples and paired A/B,D/B ratios retained. Duplicate is never a correction factor. Filter wall ns/window, block CPU ns/traversal and inverse-rate CPU ns/block or ns/query remain distinct. The three block metrics derive from one traversal, not independent observations. Group counts/extremes are descriptive; no pooled speedup, confidence interval, cold-memory or publication claim.',
        'fixture_inputs': any(v['fixture'] for v in sources.values())}
    (args.output / 'provenance.json').write_text(json.dumps(provenance, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'output': str(args.output), 'case_metrics': len(records), 'fixtures': provenance['fixture_inputs']}))


if __name__ == '__main__':
    main()
