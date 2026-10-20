#!/usr/bin/env python3
"""Offline summaries for the retired allocation driver; no build or replay dispatch."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics


PREPARATION_FIELDS = [
    'prepared_pages', 'producer_requested_cpu', 'producer_observed_cpu',
    'preparation_startup_ns', 'preparation_startup_process_cpu_ns', 'preparation_startup_producer_cpu_ns',
    'producer_request_and_drain_cpu_ns', 'preparation_start_inventory', 'preparation_start_ready',
    'preparation_start_inflight', 'preparation_end_inventory', 'preparation_end_ready', 'preparation_end_inflight',
    'preparation_peak_inventory', 'preparation_peak_arena_charge', 'preparation_unused_inventory',
    'preparation_demands', 'preparation_ready_hits', 'preparation_stockouts', 'preparation_inline_fallbacks',
    'producer_request_completed_pages', 'producer_drain_completed_pages',
    'producer_request_completed_pack_bytes', 'producer_drain_completed_pack_bytes',
    'producer_discarded_completed_pages', 'producer_discarded_pack_bytes', 'producer_discarded_index_bytes',
    'preparation_private_metadata_bound_bytes']


def preparation_fields(directory, result):
    count = result.get('prepared_pages', 0)
    if type(count) is not int or count < 0:
        raise ValueError('Prepared page count must be a nonnegative integer')
    fields = dict.fromkeys(PREPARATION_FIELDS, None) | {'prepared_pages': count}
    if not count:
        return fields
    if result.get('preparation_file') != 'preparation.json':
        raise ValueError('Prepared treatment has no preparation receipt')
    receipt = json.loads((directory / 'preparation.json').read_text())
    if any(receipt.get(field) != result[field] for field in
           ('input_fingerprint', 'trace_fingerprint', 'completed_requests')):
        raise ValueError('Preparation receipt is not matched to completed trace')
    start, end, settled = [receipt[phase] for phase in ('startup', 'before_drain', 'settled')]
    for phase in (start, end, settled):
        if type(phase['storage_observed']) is not bool:
            raise ValueError('Preparation storage observation must be boolean')
        for field, value in phase.items():
            if field == 'storage_observed':
                continue
            if (field in ('preparing_bytes', 'preparing_reserved_bytes') and
                    value is None and not phase['storage_observed']):
                continue
            if type(value) is not int or value < 0:
                raise ValueError('Preparation phase counts must be nonnegative integers')
        if (phase['inventory_limit'] != count or phase['inventory'] > count or
                phase['peak_inventory'] > count):
            raise ValueError('Preparation receipt differs from declared inventory bound')
        if (phase['inventory'] != phase['ready'] + phase['inflight'] or
                phase['repair_wait'] > phase['inflight']):
            raise ValueError('Preparation physical inventory components disagree')
    placement = {(phase['requested_cpu'], phase['observed_cpu'], phase['owner_cpu'])
                 for phase in (start, end, settled)}
    if len(placement) != 1 or start['requested_cpu'] != start['observed_cpu']:
        raise ValueError('Preparation producer placement changed or was not achieved')
    if settled['inventory'] or settled['ready'] or settled['inflight'] or settled['repair_wait']:
        raise ValueError('Preparation physical inventory did not settle within bound')
    if settled['demands'] != settled['ready_hits'] + settled['stockouts'] or             settled['stockouts'] != settled['inline_fallbacks'] or             settled['completed_pages'] != settled['ready_hits'] + settled['discarded_completed_pages']:
        raise ValueError('Preparation demand/completed work accounting disagrees')
    if result['mode'] == 'latency':
        with (directory / 'preparation-samples.csv').open() as stream:
            flags = list(csv.DictReader(stream))
        next_request = 0
        for flag in flags:
            if int(flag['first_request']) != next_request or not 1 <= int(flag['requests']) <= result['batch_width']:
                raise ValueError('Preparation sample order/extent differs from trace')
            next_request += int(flag['requests'])
            if int(flag['demands']) != int(flag['ready_hits']) + int(flag['stockouts']) or                     int(flag['stockouts']) != int(flag['inline_fallbacks']):
                raise ValueError('Preparation per-sample demand accounting disagrees')
        if next_request != result['completed_requests']:
            raise ValueError('Preparation samples do not cover completed trace')
        for field in ('demands', 'ready_hits', 'stockouts', 'inline_fallbacks'):
            if sum(int(flag[field]) for flag in flags) != settled[field]:
                raise ValueError('Preparation samples differ from actual cumulative demands')
        filename = 'latencies.csv' if result['latency_unit'] == 'logical request' else 'batch-latencies.csv'
        latency_field = 'latency_ns' if filename == 'latencies.csv' else 'batch_latency_ns'
        with (directory / filename).open() as stream:
            latency = list(csv.DictReader(stream))
        index_field = 'request' if filename == 'latencies.csv' else 'first_request'
        if len(latency) != len(flags) or any(
                int(a['first_request']) != int(b[index_field]) or
                (filename == 'batch-latencies.csv' and int(a['requests']) != int(b['requests'])) or
                float(a['latency_ns']) != float(b[latency_field])
                for a, b in zip(flags, latency)):
            raise ValueError('Preparation flags are not joined to actual latency samples')
    fields.update({
        'producer_requested_cpu': start['requested_cpu'], 'producer_observed_cpu': start['observed_cpu'],
        'preparation_startup_ns': start['startup_ns'],
        'preparation_startup_process_cpu_ns': start['startup_process_cpu_ns'],
        'preparation_startup_producer_cpu_ns': start['startup_producer_cpu_ns'],
        'producer_request_and_drain_cpu_ns': receipt['producer_request_and_drain_cpu_ns'],
        'preparation_start_inventory': start['inventory'], 'preparation_start_ready': start['ready'],
        'preparation_start_inflight': start['inflight'],
        'preparation_end_inventory': end['inventory'], 'preparation_end_ready': end['ready'],
        'preparation_end_inflight': end['inflight'],
        'preparation_peak_inventory': settled['peak_inventory'],
        'preparation_peak_arena_charge': settled['peak_arena_charge'],
        'preparation_unused_inventory': settled['unused_inventory'],
        'producer_request_completed_pages': end['completed_pages'] - start['completed_pages'],
        'producer_drain_completed_pages': settled['completed_pages'] - end['completed_pages'],
        'producer_request_completed_pack_bytes': end['completed_pack_bytes'] - start['completed_pack_bytes'],
        'producer_drain_completed_pack_bytes': settled['completed_pack_bytes'] - end['completed_pack_bytes'],
        'producer_discarded_completed_pages': settled['discarded_completed_pages'],
        'producer_discarded_pack_bytes': settled['discarded_pack_bytes'],
        'producer_discarded_index_bytes': settled['discarded_index_bytes'],
        'preparation_private_metadata_bound_bytes': settled['private_metadata_bound_bytes'],
        **{'preparation_' + field: settled[field] for field in
           ('demands', 'ready_hits', 'stockouts', 'inline_fallbacks')}})
    return fields


def summarize(root):
    run = json.loads((root / 'run.json').read_text())
    rows = []
    directories = set()
    for item in run['runs']:
        directory = (root / item['directory']).resolve()
        if directory in directories:
            raise ValueError('Repeated physical case directory in campaign')
        directories.add(directory)
        result = json.loads((root / item['directory'] / 'result.json').read_text())
        if not result['validated']:
            raise ValueError('Unvalidated timing result')
        if result.get('offered_requests', result['requests']) != result['requests'] or \
           result.get('completed_requests', result['requests']) != result['requests']:
            raise ValueError('Incomplete offered trace')
        if result.get('pmu_mode', 'off') != 'off':
            sidecar = json.loads((root / item['directory'] / 'pmu.json').read_text())
            if sidecar['input_fingerprint'] != result['input_fingerprint'] or \
               sidecar['trace_fingerprint'] != result['trace_fingerprint'] or \
               sidecar['completed_requests'] != result['completed_requests'] or \
               sidecar['measurement']['mode'] != result['pmu_mode']:
                raise ValueError('PMU sidecar is not matched to the completed trace')
        if result.get('memory_receipt', 'off') == 'on':
            receipt = json.loads((root / item['directory'] / 'memory-regions.json').read_text())
            if (receipt['input_fingerprint'] != result['input_fingerprint'] or
                    receipt['trace_fingerprint'] != result['trace_fingerprint'] or
                    receipt['completed_requests'] != result['completed_requests']):
                raise ValueError('Memory regions are not matched to the completed trace')
        for field, default in {'batch_width': 1, 'batches': result['requests'],
                               'read_execution': 'serial', 'latency_unit': 'logical request',
                               'read_active': 16, 'cache_setup': 'loaded', 'warmup_passes': 0,
                               'warmup_ns': 0, 'pmu_mode': 'off', 'memory_receipt': 'off',
                               'process_cpu_ns': None, 'process_cpu_scope': 'unreported',
                               'candidate_issued_write_scope': 'unreported',
                               'candidate_logical_live_rows': 0, 'candidate_logical_live_field_bytes': 0,
                               'batch_p50_ns': 0, 'batch_p99_ns': 0}.items():
            result.setdefault(field, default)
        expected_count = (run['config'].get('prepared_pages', 0)
                          if result['backend'].startswith('packed') else 0)
        if result.get('prepared_pages', 0) != expected_count:
            raise ValueError('Preparation treatment differs from panel configuration')
        result.update(preparation_fields(root / item['directory'], result))
        if expected_count:
            expected_cpu = run['config'].get('preparer_cpu', -1)
            if expected_cpu == -1:
                expected_cpu = run['cpu']
            receipt = json.loads((root / item['directory'] / 'preparation.json').read_text())
            if (result['producer_requested_cpu'] != expected_cpu or
                    receipt['startup']['owner_cpu'] != run['cpu'] or
                    receipt['prefill'] != (run['config'].get('prepared_prefill', 'on') == 'on')):
                raise ValueError('Preparation CPU or prefill differs from panel configuration')
        rows.append(result | {'trial': item['trial']})
    fields = ['backend', 'workload', 'mode', 'records', 'trial', 'requests',
              'batch_width', 'batches', 'read_execution', 'latency_unit', 'batch_p50_ns', 'batch_p99_ns',
              'read_active', 'cache_setup', 'warmup_passes', 'warmup_ns', 'pmu_mode', 'memory_receipt',
              'offered_requests', 'completed_requests', 'requests_with_rows_or_changes',
              'requests_without_rows_or_changes',
              'requests_per_second', 'p50_ns', 'p99_ns', 'timer_pair_p50_ns',
              'elapsed_ns', 'thread_cpu_ns', 'process_cpu_ns', 'process_cpu_scope',
              'drain_ns', 'candidate_payload_bytes',
              'candidate_metadata_bytes', 'candidate_splits', 'candidate_lease_calls',
              'candidate_pool_mapped_bytes', 'candidate_pool_reserved_bytes',
              'candidate_pool_metadata_bytes', 'candidate_arena_charged_bytes',
              'candidate_start_live_payload_bytes', 'candidate_start_pool_mapped_bytes',
              'candidate_live_payload_bytes', 'candidate_pool_idle_bytes',
              'candidate_logical_live_rows', 'candidate_logical_live_field_bytes',
              'candidate_pool_allocations', 'candidate_pool_reuses', 'candidate_pool_growths', 'candidate_pool_trim_pages',
              'lmdb_file_used_bytes',
              'candidate_issued_write_bytes', 'candidate_issued_write_spans',
              'candidate_issued_write_scope',
              'candidate_index_write_bytes', 'candidate_index_write_spans',
              'input_fingerprint', 'trace_fingerprint', 'final_fingerprint'] + PREPARATION_FIELDS
    with (root / 'cases.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fields, extrasaction='ignore')
        writer.writeheader(); writer.writerows(rows)
    groups = {}
    for row in rows:
        key = (row['workload'], row['records'], row['mode'], row['batch_width'], row['latency_unit'],
               row['read_active'], row['cache_setup'], row['warmup_passes'], row['pmu_mode'], row['memory_receipt'])
        samples = groups.setdefault(key, {}).setdefault(row['backend'], [])
        if any(sample['trial'] == row['trial'] for sample in samples):
            raise ValueError('Duplicate trial in one comparison treatment')
        samples.append(row)
    comparisons = []
    for (workload, records, mode, batch_width, latency_unit, read_active, cache_setup,
         warmup_passes, pmu_mode, memory_receipt), backends in sorted(groups.items()):
        if 'lmdb' not in backends:
            raise ValueError('LMDB reference missing')
        for candidate, reference in [(c, r) for c in sorted(backends) if c.startswith('packed')
                                      for r in sorted(backends) if r.startswith('lmdb')]:
            left, right = backends[candidate], backends[reference]
            identities = {(r['input_fingerprint'], r['trace_fingerprint'], r['final_fingerprint'])
                          for r in left + right}
            if len(identities) != 1 or len(left) != len(right) or \
               {r['trial'] for r in left} != {r['trial'] for r in right}:
                raise ValueError('Unmatched trace, final state or trial coverage')
            executions = [{r['read_execution'] for r in samples} for samples in (left, right)]
            if any(len(values) != 1 for values in executions):
                raise ValueError('Mixed execution treatments in one comparison')
            treatment_fields = ('prepared_pages', 'producer_requested_cpu', 'producer_observed_cpu')
            if any(len({tuple(r[field] for field in treatment_fields) for r in samples}) != 1
                   for samples in (left, right)):
                raise ValueError('Mixed preparation or CPU treatments in one comparison')
            median = lambda samples, field: statistics.median(r[field] for r in samples)
            comparison = {'workload': workload, 'records': records, 'mode': mode,
                          'batch_width': batch_width, 'latency_unit': latency_unit,
                          'read_active': read_active, 'cache_setup': cache_setup,
                          'warmup_passes': warmup_passes, 'pmu_mode': pmu_mode, 'memory_receipt': memory_receipt,
                          'candidate_read_execution': next(iter(executions[0])),
                          'reference_read_execution': next(iter(executions[1])),
                          'panel_read_execution': run['config'].get('read_execution', 'serial'),
                          'candidate': candidate, 'reference': reference, 'trials': len(left),
                          'candidate_prepared_pages': left[0]['prepared_pages'],
                          'reference_prepared_pages': right[0]['prepared_pages'],
                          'producer_cpu': left[0]['producer_observed_cpu'],
                          'producer_adds_cpu_resource': left[0]['producer_observed_cpu'] is not None and
                              left[0]['producer_observed_cpu'] != run['cpu']}
            if mode == 'throughput':
                l, r = median(left, 'requests_per_second'), median(right, 'requests_per_second')
                comparison |= {'candidate_requests_per_second': l, 'lmdb_requests_per_second': r,
                               'throughput_ratio': l / r}
            else:
                for field in (['batch_p50_ns', 'batch_p99_ns'] if latency_unit == 'whole read batch' else ['p50_ns', 'p99_ns']):
                    l, r = median(left, field), median(right, field)
                    comparison |= {f'candidate_{field}': l, f'lmdb_{field}': r,
                                   f'{field}_ratio': l / r, f'{field}_headroom_ns': r - l}
            comparisons.append(comparison)
    (root / 'comparison.json').write_text(json.dumps(comparisons, indent=2) + '\n')
    validator = {
        'format': 1,
        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'run_json_sha256': hashlib.sha256((root / 'run.json').read_bytes()).hexdigest(),
        'cases_sha256': hashlib.sha256((root / 'cases.csv').read_bytes()).hexdigest(),
        'comparison_sha256': hashlib.sha256((root / 'comparison.json').read_bytes()).hexdigest(),
        'scope': 'summary validation; measured source and binary identities remain in their original receipts'}
    (root / 'summary-validator.json').write_text(json.dumps(validator, indent=2) + '\n')
    return comparisons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Existing collected campaign')
    parser.add_argument('--summarize-only', action='store_true', help='Accepted historical report spelling')
    args = parser.parse_args()
    summarize(args.output)


if __name__ == '__main__':
    main()
