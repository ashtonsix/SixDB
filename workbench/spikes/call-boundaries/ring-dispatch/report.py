#!/usr/bin/env python3
"""Recompute retained Ring endpoints in an evidence working copy.

Inputs are read only and checked against recovery.json. Outputs go to reports/
in that copy. This runs no benchmark; worker recapture uses the source archives.
"""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summary(values):
    return dict(median=statistics.median(values), minimum=min(values), maximum=max(values), samples=len(values))


def write_csv(path, rows):
    if not rows:
        return
    keys = list(dict.fromkeys(key for row in rows for key in row))
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    recovery = json.loads((evidence / 'recovery.json').read_text())
    files = {entry['path']: entry for entry in recovery['files']}
    if len(files) != len(recovery['files']):
        raise ValueError('duplicate indexed evidence paths')
    checked = {}

    def indexed(name):
        entry = files[name]
        path = (evidence / name).resolve()
        if not path.is_relative_to(evidence):
            raise ValueError('indexed input escapes evidence directory: ' + name)
        digest = sha(path)
        if digest != entry['sha256'] or path.stat().st_size != entry['bytes']:
            raise ValueError('indexed input changed: ' + name)
        checked[name] = digest
        return path

    for name in sorted(files):
        indexed(name)

    def read_rows(name):
        return list(csv.DictReader(indexed(name).open()))

    native_rows = read_rows('native.csv')
    native_groups = defaultdict(list)
    for row in native_rows:
        key = (row['job_id'], row['isa'], row['variant'], int(row['n']), int(row['chains']), row['execution'])
        native_groups[key].append(row)
    native = []
    for key, rows in sorted(native_groups.items()):
        assert len(rows) == 7 and {int(r['rep']) for r in rows} == set(range(7))
        values = [float(r['ns_per_lookup']) for r in rows]
        assert all(math.isfinite(v) and v > 0 for v in values)
        native.append(dict(zip(['job_id', 'isa', 'variant', 'n', 'chains', 'execution'], key), **summary(values)))

    shared_rows = read_rows('shared.csv')
    shared_index = json.loads(indexed('shared.json').read_text())
    shared_machines = {job['job_id']: job['machine'] for job in shared_index['jobs']}
    shared_groups = defaultdict(list)
    for row in shared_rows:
        key = (row['job_id'], row['isa'], int(row['n']), int(row['chains']),
               int(row['configured_grain']), int(row['window_mode']), row['transfer'], row['execution'])
        shared_groups[key].append(row)
    shared_medians, shared = {}, []
    for key, rows in sorted(shared_groups.items()):
        assert len(rows) == 7 and {int(r['rep']) for r in rows} == set(range(7))
        assert len({r['checksum'] for r in rows}) == 1
        values = [float(r['ns_per_lookup']) for r in rows]
        assert all(math.isfinite(v) and v > 0 for v in values)
        assert len({(r['ring_storage_bytes'], r['groups']) for r in rows}) == 1
        cell = dict(zip(['job_id', 'isa', 'n', 'chains', 'grain', 'window_mode', 'transfer', 'execution'], key),
                    **summary(values), ring_storage_bytes=int(rows[0]['ring_storage_bytes']), groups=int(rows[0]['groups']))
        shared_medians[key] = cell
        shared.append(cell)
    shared_endpoints = []
    for case in sorted({key[:6] for key in shared_groups}):
        for hinted in (False, True):
            arm, control = ('ring-hints', 'manual-hints') if hinted else ('ring', 'manual')
            local = shared_medians[case + ('local', arm)]
            prefix = shared_medians[case + ('shared', arm)]
            local_manual = shared_medians[case + ('local', control)]
            prefix_manual = shared_medians[case + ('shared', control)]
            assert local['groups'] == prefix['groups']
            assert local['ring_storage_bytes'] - prefix['ring_storage_bytes'] == 8 * local['groups']
            shared_endpoints.append(dict(zip(['job_id', 'isa', 'n', 'chains', 'grain', 'window_mode'], case),
                machine=shared_machines[case[0]], hinted=hinted,
                local_ns=local['median'], shared_ns=prefix['median'],
                local_min=local['minimum'], local_max=local['maximum'], shared_min=prefix['minimum'], shared_max=prefix['maximum'],
                local_manual_ns=local_manual['median'], shared_manual_ns=prefix_manual['median'],
                local_over_shared=local['median'] / prefix['median'],
                local_over_matched_manual=local['median'] / local_manual['median'],
                shared_over_matched_manual=prefix['median'] / prefix_manual['median'],
                shared_time_change_percent=100 * (prefix['median'] / local['median'] - 1),
                local_ring_storage_bytes=local['ring_storage_bytes'], shared_ring_storage_bytes=prefix['ring_storage_bytes'],
                saved_slot_storage_bytes=local['ring_storage_bytes'] - prefix['ring_storage_bytes'],
                groups=local['groups'], samples_per_arm=7))

    factor_rows = read_rows('cohort-factor.csv')
    factor_index = json.loads(indexed('cohort-factor.json').read_text())
    machines = {job['job_id']: job['machine'] for job in factor_index['jobs']}
    groups = defaultdict(list)
    for row in factor_rows:
        key = (row['job_id'], row['isa'], int(row['n']), int(row['chains']), int(row['configured_grain']), row['call_factor'], row['execution'])
        groups[key].append(row)
    medians = {}
    for key, rows in groups.items():
        assert len(rows) == 7 and {int(r['rep']) for r in rows} == set(range(7))
        assert len({r['checksum'] for r in rows}) == 1
        values = [float(r['ns_per_lookup']) for r in rows]
        assert all(math.isfinite(v) and v > 0 for v in values)
        medians[key] = summary(values)
    endpoints = []
    cases = sorted({key[:5] for key in groups})
    for case in cases:
        for hinted in (False, True):
            arm, control = ('ring-hints', 'manual-hints') if hinted else ('ring', 'manual')
            old = medians[case + ('invoke', arm)]
            new = medians[case + ('direct', arm)]
            old_manual = medians[case + ('invoke', control)]
            new_manual = medians[case + ('direct', control)]
            endpoints.append(dict(zip(['job_id', 'isa', 'n', 'chains', 'grain'], case),
                machine=machines[case[0]], hinted=hinted,
                invoke_ns=old['median'], direct_ns=new['median'],
                invoke_min=old['minimum'], invoke_max=old['maximum'], direct_min=new['minimum'], direct_max=new['maximum'],
                invoke_manual_ns=old_manual['median'], direct_manual_ns=new_manual['median'],
                invoke_over_direct=old['median'] / new['median'],
                direct_over_matched_manual=new['median'] / new_manual['median'],
                direct_time_change_percent=100 * (new['median'] / old['median'] - 1),
                samples_per_arm=7))

    pmu_rows = read_rows('pmu.csv')
    pmu_groups = defaultdict(list)
    for row in pmu_rows:
        raw_path = indexed('pmu/raw/' + row['file'])
        assert sha(raw_path) == row['sha256']
        raw = json.loads(raw_path.read_text())
        m = raw['measurement']
        assert raw['validated'] and m['status'] == 'complete'
        assert m['time_running_ns'] == m['time_enabled_ns'] > 0 and not m['scaled'] and not m['multiplexed']
        events = {e['name']: e for e in m['events']}
        cycles = events['cpu_cycles']['count']
        instructions = events['retired_instructions']['count']
        assert cycles > 0 and instructions > 0
        assert math.isclose(instructions / cycles, m['ipc'], rel_tol=1e-14)
        lookups = raw['logical_lookups']
        item = dict(ns=float(row['ns_per_lookup']), cycles=cycles / lookups,
                    instructions=instructions / lookups, ipc=instructions / cycles)
        if raw['mode'] == 'stalls':
            frontend = events['frontend_stall_cycles']
            backend = events['backend_stall_cycles']
            assert frontend['status'] == 'measured' and backend['status'] == 'unavailable' and backend['count'] is None
            item['frontend_stall_fraction'] = frontend['count'] / cycles
        key = (raw['mode'], raw['n'], raw['chains'], raw['implementation'], raw['execution'])
        pmu_groups[key].append(item)
    pmu = []
    for key, items in sorted(pmu_groups.items()):
        assert len(items) == 5
        pmu.append(dict(zip(['mode', 'n', 'chains', 'implementation', 'execution'], key),
            ns_per_lookup=statistics.median(x['ns'] for x in items),
            cycles_per_lookup=statistics.median(x['cycles'] for x in items),
            instructions_per_lookup=statistics.median(x['instructions'] for x in items),
            ipc=statistics.median(x['ipc'] for x in items),
            frontend_stall_fraction=statistics.median(x['frontend_stall_fraction'] for x in items) if key[0] == 'stalls' else None,
            backend_stalls='unavailable' if key[0] == 'stalls' else 'not_selected', samples=5))

    output = evidence / 'reports'
    output.mkdir(exist_ok=True)
    report = dict(format=1, report_entrypoint_sha256=sha(Path(__file__)), checked_input_sha256=checked,
        native_medians=native, shared_medians=shared, shared_endpoints=shared_endpoints,
        cohort_factor_endpoints=endpoints, pmu_medians=pmu,
        limits=['Ratios use matching controls in the same executable/case; seven repetitions are retained, not confidence intervals.',
                'The local/shared comparison retains the outlined Cohort negative performance cut; changed Slot stride accompanies saved argument bytes.',
                'PMU was collected in separate instrumented runs; no final-factor PMU or memory-stall attribution is inferred.',
                'Direct grain four is not a universal improvement; singleton regressions and no-hint controls remain in the output.'])
    (output / 'endpoints.json').write_text(json.dumps(report, indent=2) + '\n')
    write_csv(output / 'native-medians.csv', native)
    write_csv(output / 'shared-medians.csv', shared)
    write_csv(output / 'shared-endpoints.csv', shared_endpoints)
    write_csv(output / 'cohort-factor-endpoints.csv', endpoints)
    write_csv(output / 'pmu-medians.csv', pmu)
    print(output, 'verified inputs', len(checked), 'native cells', len(native), 'shared cells', len(shared),
          'shared endpoints', len(shared_endpoints), 'factor endpoints', len(endpoints), 'PMU cells', len(pmu))


if __name__ == '__main__':
    main()
