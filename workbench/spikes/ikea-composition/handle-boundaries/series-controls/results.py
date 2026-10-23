#!/usr/bin/env python3
"""Retain every same-ELF SeriesPack control case; never combine unlike work."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

READ = ('columns', 'columns-duplicate', 'shared-fresh', 'shared-retained',
        'copied-fresh', 'copied-retained', 'typed')
INIT = ('columns', 'columns-duplicate', 'dispatch', 'typed')
COUNTERS = ('visited_blocks', 'active_blocks', 'queries_per_active_block',
            'issued_queries', 'logical_rows', 'width_transitions',
            'occupied_payload_bytes', 'resident_input_bytes', 'fixture_object_bytes',
            'retained_binding_bytes', 'operation_table_bytes', 'journal_capacity_entries')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expected_names():
    names = set()
    for operation, routes in [('point', READ), ('range', READ), ('read16', READ), ('initialize', INIT)]:
        prefix = 'series/initialize' if operation == 'initialize' else 'series/read/'+operation
        for route in routes:
            for trace in ('clustered', 'permuted'):
                for geometry in ('local', 'striped'):
                    for q in (1, 8, 64):
                        names.add(f'{prefix}/{route}/{trace}/{geometry}/q{q}')
    assert len(names) == 300
    return names


def case(name):
    fields = name.split('/')
    assert fields.pop(0) == 'series'
    family = fields.pop(0)
    operation = fields.pop(0) if family == 'read' else 'initialize'
    assert family in ('read', 'initialize')
    route, trace, geometry, q = fields
    return operation, route, trace, geometry, int(q.removeprefix('q'))


def baseline(name):
    fields = name.split('/')
    fields[3 if fields[1] == 'read' else 2] = 'columns'
    return '/'.join(fields)


def check_work(row, facts):
    operation, route, trace, geometry, q = case(row['name'])
    counters = {key: row[key] for key in COUNTERS}
    assert all(math.isfinite(value) and value >= 0 for value in counters.values())
    active = 40 if operation == 'read16' else 56
    rows = {'point': 56, 'range': 1028, 'read16': 640, 'initialize': 4928}[operation]
    expected = {'visited_blocks': 64, 'active_blocks': active,
                'queries_per_active_block': q, 'issued_queries': active*q,
                'logical_rows': rows*q, 'resident_input_bytes': 78848,
                'fixture_object_bytes': facts['fixture_object_bytes'],
                'operation_table_bytes': sum(facts[key] for key in
                    ('read_table_bytes', 'initialize_table_bytes', 'typed_table_bytes',
                     'typed_initialize_table_bytes', 'extent_table_bytes')),
                'journal_capacity_entries': 68 if operation == 'initialize' else 0}
    expected['retained_binding_bytes'] = 64 * (facts['shared_reader_bytes'] if route == 'shared-retained'
                                              else facts['copied_reader_bytes'] if route == 'copied-retained' else 0)
    assert all(counters[key] == value for key, value in expected.items()), (row['name'], counters, expected)
    assert counters['width_transitions'] == 7 if trace == 'clustered' else 7 < counters['width_transitions'] <= 63
    assert counters['occupied_payload_bytes'] > 0
    return counters


def load(member, root):
    receipt = json.loads((root/'receipt.json').read_text())
    assert receipt['complete'] and receipt['source_unchanged']
    assert all(command['returncode'] == 0 for command in receipt['commands'])
    names, groups, raw, binaries, facts = expected_names(), {}, {}, {}, {}
    source = receipt['source_sha256']
    cxx = {path: digest for path, digest in source.items()
           if '/series-controls/' in path and Path(path).suffix in ('.h', '.cpp')}
    assert len(cxx) == 7
    for profile in receipt['profiles']:
        facts[profile] = json.loads((root/profile/'series-facts.json').read_text())
        assert facts[profile]['cases'] == 300
        binary = receipt['binaries'][profile]['series']
        local = root/profile/Path(binary['path']).name
        if local.exists():
            assert sha(local) == binary['sha256'] and local.stat().st_size == binary['bytes']
        binaries[profile] = {**binary, 'local_bytes_verified': local.exists()}
        checked = [command for command in receipt['commands']
                   if Path(command['log']).name == 'series-check.txt' and profile in Path(command['log']).parts]
        assert len(checked) == 1 and '--check-only' in checked[0]['argv']
        log = root/profile/'series-check.txt'
        assert sha(log) == checked[0]['log_sha256']
    trials = [trial for trial in receipt['trials'] if trial.get('family') == 'series']
    assert len(trials) == 40*len(receipt['profiles'])
    protocol = set()
    for trial in trials:
        profile, route, number = trial['profile'], trial['route'], trial['trial']
        assert profile in facts and route in set(READ+INIT) and number in range(5)
        key = (profile, route, number); assert key not in protocol; protocol.add(key)
        path = root/profile/Path(trial['path']).name
        assert path.name == f'series-{number}-{route}.json' and str(path) not in raw
        treatments = ['columns', 'shared-fresh', 'copied-retained', 'typed',
                      'columns-duplicate', 'copied-fresh', 'shared-retained', 'dispatch']
        order = treatments[number:] + treatments[:number]
        assert trial['order'] == (list(reversed(order)) if number % 2 else order)
        assert sha(path) == trial['sha256']; raw[str(path)] = trial['sha256']
        rows = [row for row in json.loads(path.read_text())['benchmarks'] if row.get('run_type') == 'iteration']
        wanted = {name for name in names if case(name)[1] == route}
        assert len(rows) == len(wanted) == trial['cases'] and {row['name'] for row in rows} == wanted
        for row in rows:
            assert not row.get('error_occurred') and row['time_unit'] == 'ns'
            assert math.isfinite(row['cpu_time']) and row['cpu_time'] > 0 and row['iterations'] > 0
            work = check_work(row, facts[profile])
            entry = groups.setdefault((profile, row['name']), {'samples': {}, 'counters': work})
            assert entry['counters'] == work and number not in entry['samples']
            entry['samples'][number] = row['cpu_time']
    assert len(groups) == 300*len(facts)
    result = []
    for (profile, name), entry in sorted(groups.items()):
        assert set(entry['samples']) == set(range(5))
        values = [entry['samples'][i] for i in range(5)]
        base = groups[profile, baseline(name)]
        assert all(value == base['counters'][key] for key, value in entry['counters'].items()
                   if key != 'retained_binding_bytes')
        ratios = [values[i]/base['samples'][i] for i in range(5)]
        operation, route, trace, geometry, q = case(name)
        result.append({'member': member, 'profile': profile, 'case': name,
                       'operation': operation, 'route': route, 'trace': trace, 'geometry': geometry, 'q': q,
                       'metric': 'cpu_ns_per_64_descriptor_traversal', 'samples': values,
                       'median': statistics.median(values), 'route_over_columns': ratios,
                       'route_over_columns_median': statistics.median(ratios), 'counters': entry['counters']})
    return result, {'receipt_sha256': sha(root/'receipt.json'), 'source_sha256': source,
                    'cxx_source_sha256': cxx, 'binaries': binaries, 'facts': facts,
                    'trials_sha256': raw, 'compiler': receipt['compiler'], 'profiles': receipt['profiles']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', help='member=collected results directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rows, provenance, common = [], {}, None
    for named in args.inputs:
        member, directory = named.split('=', 1)
        assert member not in provenance
        result, origin = load(member, Path(directory))
        assert common is None or common == origin['source_sha256'], 'different frozen sources'
        assert not ({profile for value in provenance.values() for profile in value['profiles']} & set(origin['profiles']))
        common = origin['source_sha256']; rows.extend(result); provenance[member] = origin
    data = {'scope': __doc__, 'timing_units': 'CPU ns per complete64-descriptor traversal',
            'comparisons': 'Five trial-index paired route/columns ratios; duplicate is uncorrected noise control.',
            'reader_sha256': sha(Path(__file__)), 'provenance': provenance, 'cases': rows,
            'limits': 'Warm resident same-ELF controls. Different operations/q are distinct work. No aggregate score, cold-cache or owner-publication claim.'}
    (args.output/'cases.json').write_text(json.dumps(data, indent=2, sort_keys=True)+'\n')
    with (args.output/'cases.csv').open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]), lineterminator='\n'); writer.writeheader()
        writer.writerows({key: json.dumps(value, sort_keys=True) if isinstance(value, (list, dict)) else value
                         for key, value in row.items()} for row in rows)
    print(json.dumps({'case_routes': len(rows), 'profiles': sorted({row['profile'] for row in rows}),
                      'cases_json_sha256': sha(args.output/'cases.json'),
                      'cases_csv_sha256': sha(args.output/'cases.csv')}, indent=2))


if __name__ == '__main__':
    main()
