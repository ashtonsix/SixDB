#!/usr/bin/env python3
"""Validate and compact retained dispatch/cohort worker rows; never run a binary."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

ARMS = {'abseil', 'abseil-prefetch', 'packed', 'direct', 'manual', 'manual-hints', 'ring', 'ring-hints'}
SIZES = (128, 4096, 1048576, 4194304)
CHAINS = (1, 8, 32)
FIXED = ('checksum', 'packed_requested_bytes', 'direct_requested_bytes', 'abseil_requested_bytes',
         'key_input_bytes', 'primary_keys', 'secondary_keys', 'overflow_keys', 'hint_code', 'rounds')
MASK = (1 << 64) - 1


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text())


def expected_checksums():
    """Independent sum of the authored key set, without map/lookup/serialized rows."""
    result, total = {}, 0
    for i in range(1, max(SIZES) + 1):
        value = i
        value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & MASK
        value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & MASK
        value ^= value >> 31
        total = (total + value) & MASK
        if i in SIZES:
            result[i] = total
    return result


def context(path):
    result = {}
    for line in path.read_text().splitlines():
        key, value = line.split('=', 1)
        assert key not in result, (path, 'duplicate context', key)
        result[key] = value
    return result


def executable_order(path):
    """Preserve known unquoted GNU decimal-comma receipt fields explicitly."""
    rows = list(csv.reader(path.open()))
    header, body = rows[0], rows[1:]
    result, repaired = [], 0
    for row in body:
        if len(row) == len(header) + 2 and header[-2:] == ['start_utc', 'end_utc']:
            row = row[:-4] + [row[-4] + ',' + row[-3], row[-2] + ',' + row[-1]]
            repaired += 1
        assert len(row) == len(header), (path, 'order row width', row)
        result.append(dict(zip(header, row)))
    return result, repaired


def summarize_job(job, checksums):
    root = job / 'results'
    status = load(root / 'worker-result.json')
    result = {'job_id': job.name, 'worker_status': status, 'source': load(root / 'source-provenance.json'),
              'dispatch_sources': load(root / 'dispatch-sources.json'), 'files': {}, 'cells': []}
    collection = job / 'collection.json'
    if collection.exists():
        result['collection_artifact'] = load(collection).get('artifact')
    for name in ['source-provenance.json', 'source-manifest.json', 'dispatch-sources.json',
                 'worker-result.json', 'compiler.txt', 'executable-order.csv']:
        path = root / name
        if path.exists():
            result['files'][name] = sha(path)
    sources, provenance = result['dispatch_sources'], result['source']
    manifest = load(root / 'source-manifest.json')
    assert all(manifest.get(name) == digest for name, digest in sources.items()), (job.name, 'runtime/captured source mismatch')
    assert sources['controls/old/loom/ring.h'] == provenance['selected_manifest_verified']['loom/include/loom/ring.h']
    actual_ring, copied_note = sources['controls/new/loom/ring.h'], provenance['copied_optimized_ring_sha256']
    if actual_ring != copied_note:
        # Known first marker capture: the captured and runtime-computed sources
        # agree, while a copied planning note describes a later layout revision.
        assert actual_ring == 'bc98eb245dda4b33130bb8a2688a49c2673663caa86daa084c55db0657c6142f'
        assert copied_note == '4c116153c9a1a909d9688a5c2c97cb940bf9130097fa4bd70327fb0c26620752'
        result['provenance_discrepancy'] = {'actual_captured_and_runtime_ring_sha256': actual_ring,
            'stale_copied_note_sha256': copied_note,
            'interpretation': 'Classify this job by actual source bytes, retaining the stale copy-note discrepancy.'}
    for name in ['workbench/spikes/hashtable/map.h', 'workbench/spikes/hashtable/ring.cpp']:
        assert sources[name] == provenance['selected_manifest_verified'][name], (job.name, name)
    assert sources['controls/explicit/ring.cpp'] == provenance['explicit_driver_sha256']
    if 'compatible_driver_sha256' in provenance:
        assert sources['controls/new/ring.cpp'] == provenance['compatible_driver_sha256']
    if status['state'] != 'complete' or status.get('script_returncode') != 0:
        result['qualification'] = 'terminal failed job; no timing cells admitted'
        return result
    order, repaired = executable_order(root / 'executable-order.csv')
    result['order_timestamp_decimal_comma_rows_decoded'] = repaired
    result['executable_order'] = order
    cohort = 'controls/cohort/ring.cpp' in sources
    result['campaign'] = 'cohort' if cohort else 'dispatch'
    result['optimized_ring_sha256'] = sources['controls/new/loom/ring.h']
    if cohort:
        pattern = re.compile(r'^(neon|avx2|avx512)-grain(\d+)-(\d+)-(\d+)\.csv$')
    else:
        pattern = re.compile(r'^(neon|avx2|avx512)-(old|new|explicit)-(\d+)-(\d+)\.csv$')
    receipts = []
    result['hardware_profiles'] = {}
    common_by_case = {}
    for path in sorted(root.glob('*.csv')):
        match = pattern.fullmatch(path.name)
        if not match:
            continue
        isa, variant, n, chains = match.groups()
        n, chains = int(n), int(chains)
        assert n in SIZES and chains in CHAINS
        grain = int(variant) if cohort else 1
        effective = min(chains, grain)
        rows = list(csv.DictReader(path.open()))
        assert all(None not in row for row in rows), (path, 'malformed measurement row')
        assert len(rows) == 56, (path, 'expected eight arms x seven repeats', len(rows))
        assert {(int(row['rep']), row['execution']) for row in rows} == {(rep, arm) for rep in range(7) for arm in ARMS}
        ctx_path = path.with_suffix('.context')
        ctx = context(ctx_path)
        hardware_path = root / f'hardware-{isa}.json'
        hardware = load(hardware_path)
        if isa not in result['hardware_profiles']:
            result['hardware_profiles'][isa] = {'path': hardware_path.name, 'sha256': sha(hardware_path),
                'behavior_context': hardware['behavior_context'], 'policy': hardware['policy'],
                'selected_cpu': hardware['reported']['selected_cpu'], 'context_qualified': hardware['context_qualified']}
        assert ctx['hardware_context'] == hardware['behavior_context']
        assert ctx['calibration_enabled'] == 'false' and hardware['calibration_enabled'] is False
        assert int(ctx['line_bytes']) == hardware['policy']['line_bytes']
        assert int(ctx['page_bytes']) == hardware['policy']['page_bytes']
        assert int(ctx['policy_revision']) == hardware['policy']['revision']
        assert int(ctx['demand_code']) == 0 and int(ctx['selected_hint']) in range(4)
        assert int(ctx['pressure']) in (0, 1)
        if cohort:
            assert ctx['prepared_map_fact'] == 'true' and ctx['ragged_check'] == 'passed'
            assert int(ctx['configured_grain']) == grain and int(ctx['effective_grain']) == effective
            assert int(ctx['groups']) == (chains + grain - 1) // grain
            assert ctx['logical_visits_origin'] == 'derived_equal_lengths'
        first = {key: int(rows[0][key]) for key in FIXED}
        assert first['checksum'] == checksums[n]
        assert first['key_input_bytes'] == 8 * n
        assert first['primary_keys'] + first['secondary_keys'] + first['overflow_keys'] == n
        assert first['packed_requested_bytes'] == first['direct_requested_bytes']
        assert first['rounds'] == max(1, 262144 // n)
        assert first['hint_code'] == int(ctx['selected_hint'])
        case_key = (isa, n, chains)
        if case_key in common_by_case:
            assert first == common_by_case[case_key], (path, 'matched source work differs')
        common_by_case[case_key] = first
        for row in rows:
            assert int(row['n']) == n and int(row['chains']) == chains
            assert {key: int(row[key]) for key in FIXED} == first
            assert math.isfinite(float(row['ns_per_lookup'])) and float(row['ns_per_lookup']) > 0
            ring = row['execution'] in ('ring', 'ring-hints')
            visits = 2 * n * first['rounds'] if ring else 0
            assert int(row['ring_visits']) == visits // effective
            if cohort:
                assert int(row['configured_grain']) == grain and int(row['effective_grain']) == effective
                assert int(row['groups']) == (chains + grain - 1) // grain
                assert int(row['useful_item_stage_visits']) == visits
                assert row['useful_item_stage_visits_origin'] == 'derived_equal_lengths'
        file_entry = {'measurement': path.name, 'csv_sha256': sha(path), 'context_sha256': sha(ctx_path),
                      'hardware_sha256': sha(hardware_path), 'hardware_context_sha256': hashlib.sha256(ctx['hardware_context'].encode()).hexdigest(),
                      'n': n, 'chains': chains, 'variant': variant, 'isa': isa,
                      'context': {k: v for k, v in ctx.items() if k != 'hardware_context'}, 'rows': len(rows)}
        receipts.append(file_entry)
        for arm in sorted(ARMS):
            selected = [row for row in rows if row['execution'] == arm]
            values = [float(row['ns_per_lookup']) for row in selected]
            result['cells'].append({'isa': isa, 'variant': variant, 'n': n, 'chains': chains, 'execution': arm,
                'ns_per_lookup_median': statistics.median(values), 'ns_per_lookup_min': min(values), 'ns_per_lookup_max': max(values),
                'samples': values, 'ring_visits': int(selected[0]['ring_visits']), 'useful_item_stage_visits': int(selected[0].get('useful_item_stage_visits', selected[0]['ring_visits'])),
                'ring_storage_bytes': int(selected[0]['ring_storage_bytes']), **first})
    isas = sorted({r['isa'] for r in receipts})
    assert isas
    for isa in isas:
        variants = ('1', '2', '4') if cohort else ('old', 'new', 'explicit')
        expected = {(isa, variant, n, c) for variant in variants for n in SIZES for c in CHAINS}
        actual = {(r['isa'], r['variant'], r['n'], r['chains']) for r in receipts if r['isa'] == isa}
        assert actual == expected, (job.name, isa, 'missing or extra planned cells', expected - actual, actual - expected)
    result.update(qualification='complete independent checksum and matched accounting checks',
                  measurement_files=receipts, measurement_rows=sum(r['rows'] for r in receipts), isas=isas)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workers', type=Path, default=Path('build/workers'))
    parser.add_argument('--jobs', nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    checksums = expected_checksums()
    jobs = [summarize_job(args.workers / job, checksums) for job in args.jobs]
    result = {'format': 1, 'status': 'validated', 'independent_key_set_checksums': checksums, 'jobs': jobs,
        'limits': ['Samples from different source hashes, machines and cohorts remain separate.',
                   'Warm calibration probes are excluded; seven repetitions per measured cell remain explicit.',
                   'Whole setup/enqueue/drain scope comes from captured driver; timings do not isolate instruction latency.',
                   'Geometry/context/hint codes and logical work are checked; PMU and cache serving tier are not inferred.',
                   'Cohort useful visits are derived equal-length bookkeeping; actual Ring visits are recorded separately.']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    compact = []
    for job in jobs:
        for cell in job['cells']:
            row = {'job_id': job['job_id'], 'ring_sha256': job['optimized_ring_sha256'],
                   'campaign': job['campaign'], **{k: v for k, v in cell.items() if k != 'samples'}}
            row['samples_json'] = json.dumps(cell['samples'], separators=(',', ':'))
            compact.append(row)
    if compact:
        with args.output.with_suffix('.csv').open('w') as out:
            writer = csv.DictWriter(out, fieldnames=list(compact[0]))
            writer.writeheader()
            writer.writerows(compact)
    print(args.output, 'jobs', len(jobs), 'admitted rows', sum(job.get('measurement_rows', 0) for job in jobs))
    for job in jobs:
        print(job['job_id'], job['worker_status']['state'], job.get('campaign'), job.get('optimized_ring_sha256', '')[:12], job.get('measurement_rows', 0))


if __name__ == '__main__':
    main()
