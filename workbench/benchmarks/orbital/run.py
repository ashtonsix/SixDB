#!/usr/bin/env python3
"""Replay paired exact traces; throughput and latency use separate fresh stores."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import time


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
    parser.add_argument('binary', type=Path, nargs='?')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--summarize-only', action='store_true')
    parser.add_argument('--cpu', type=int, default=int(os.environ.get('SIXDB_CPU', -1)))
    parser.add_argument('--records', default='8192,262144,4194304')
    parser.add_argument('--workloads', default='point-hit,point-miss,point-update,point-rmw,insert-random,delete,scan-16,append,dependent-16,ycsb-a,ycsb-b,ycsb-c,ycsb-d,ycsb-e,ycsb-f,order-entry,order-status,stock-range,insert-scan')
    parser.add_argument('--backends', default='packed-owner,lmdb,packed,packed-retained,lmdb-read-batch64')
    parser.add_argument('--operations', type=int, default=100000)
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--seed', type=int, default=20261007)
    parser.add_argument('--batch-width', type=int, default=1)
    parser.add_argument('--read-execution', choices=['serial', 'native', 'manual', 'ring'], default='serial')
    parser.add_argument('--modes', default='throughput,latency')
    parser.add_argument('--pmu', choices=['off', 'basic', 'stalls'], default='off')
    parser.add_argument('--pmu-event', action='append', default=[])
    parser.add_argument('--memory-receipt', choices=['off', 'on'], default='off')
    parser.add_argument('--read-active', type=int, default=16)
    parser.add_argument('--cache-setup', choices=['loaded', 'warm-trace'], default='loaded')
    parser.add_argument('--warmup-passes', type=int, default=1)
    parser.add_argument('--prepared-pages', type=int, default=0)
    parser.add_argument('--preparer-cpu', type=int, default=-1)
    parser.add_argument('--prepared-prefill', choices=['on', 'off'], default='on')
    args = parser.parse_args()
    if args.summarize_only:
        summarize(args.output); return
    if not args.binary or args.cpu not in os.sched_getaffinity(0) or args.repetitions < 1:
        parser.error('binary, allowed --cpu and positive repetitions required')
    modes = args.modes.split(',')
    if not modes or len(set(modes)) != len(modes) or not set(modes) <= {'latency', 'throughput'}:
        parser.error('choose distinct latency/throughput modes')
    if args.pmu != 'off' and modes != ['throughput']:
        parser.error('PMU collection requires a separate --modes throughput diagnostic')
    if args.memory_receipt == 'on' and modes != ['throughput']:
        parser.error('Memory receipts require a separate --modes throughput diagnostic')
    if args.pmu_event and args.pmu == 'off':
        parser.error('--pmu-event requires --pmu basic or stalls')
    if not 1 <= args.batch_width <= 64:
        parser.error('read batch width must be in [1,64]')
    if not 1 <= args.read_active <= 64 or not 1 <= args.warmup_passes <= 16:
        parser.error('active read width must be in [1,64]; warmup passes in [1,16]')
    if args.cache_setup == 'warm-trace' and not set(args.workloads.split(',')) <= {'point-hit','point-miss','scan-16','dependent-16','ycsb-c','order-status','stock-range'}:
        parser.error('warm-trace conditioning supports read-only workloads')
    if (args.batch_width > 1 or args.read_execution != 'serial') and not set(args.workloads.split(',')) <= {'point-hit','point-miss','dependent-16','ycsb-c'}:
        parser.error('read batches support point hit/miss, YCSB-C and dependent reads')
    if args.read_execution != 'serial' and not set(args.backends.split(',')) <= {'packed-owner','lmdb','lmdb-read-batch64'}:
        parser.error('staged candidate requires packed-owner with genuine LMDB references')
    if not 0 <= args.prepared_pages <= 4096 or args.preparer_cpu < -1:
        parser.error('prepared pages must be in [0,4096], preparer CPU >= -1')
    if args.preparer_cpu != -1 and args.preparer_cpu not in os.sched_getaffinity(0):
        parser.error('explicit preparer CPU must be in runner allowed affinity')
    backend_names = args.backends.split(',')
    if ('lmdb' not in backend_names or len(set(backend_names)) != len(backend_names) or
            not set(backend_names) <= {'lmdb', 'packed-owner', 'packed', 'packed-retained', 'lmdb-read-batch64'}):
        parser.error('include lmdb and distinct supported candidate backends')
    args.binary = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    # Measure a retained immutable copy, so a concurrent incremental rebuild
    # cannot silently change binaries halfway through the paired campaign.
    measured_binary = args.output.resolve() / 'orbital_allocation_bench'
    shutil.copy2(args.binary, measured_binary)
    binary_hash = hashlib.sha256(measured_binary.read_bytes()).hexdigest()
    run = {'format': 1, 'status': 'running', 'binary_sha256': binary_hash,
           'binary': 'orbital_allocation_bench', 'platform': platform.platform(), 'cpu': args.cpu,
           'allowed_cpus': sorted(os.sched_getaffinity(0)), 'config': vars(args) | {'binary': str(args.binary), 'output': str(args.output)},
           'lmdb_reference': {'common': 'NOSYNC/NOMETASYNC; ordinary locking/COW',
                              'lmdb': 'one transaction per logical request',
                              'lmdb-read-batch64': 'up to64 adjacent read requests per real snapshot; write request interrupts/commits separately'},
           'candidate_omits': ['Engine', 'Shore', 'versioning', 'isolation', 'durability'], 'runs': []}
    run['host_lscpu'] = json.loads(subprocess.check_output(['lscpu', '-J']))
    cache_root = Path(f'/sys/devices/system/cpu/cpu{args.cpu}/cache')
    run['cpu_cache'] = [{field: (index / field).read_text().strip()
                        for field in ['level', 'type', 'size', 'shared_cpu_list', 'id']
                        if (index / field).exists()} for index in sorted(cache_root.glob('index*'))]
    run['packages'] = subprocess.run(['dpkg-query', '-W', 'liblmdb0', 'liblmdb-dev'], text=True, capture_output=True).stdout
    run['residency'] = {'input_preparation': 'fresh store loaded and execution bank prepared before timing',
                        'cache_conditioning': args.cache_setup,
                        'warmup_passes': args.warmup_passes if args.cache_setup == 'warm-trace' else 0,
                        'scope': 'exact validated trace replayed after setup; no explicit flush or eviction',
                        'observed_load_service_tier': None,
                        'interpretation': 'allocated/mapped footprint and cache geometry do not establish timed load residency'}
    run['preparation'] = {'pages': args.prepared_pages, 'requested_cpu': args.preparer_cpu,
                          'prefill': args.prepared_prefill,
                          'startup': 'explicitly after load/bank/sample/timer/conditioning; cost reported separately',
                          'resource_scope': 'default helper inherits caller pin; explicit distinct CPU is extra resource',
                          'reference': 'LMDB receives prepared-pages=0; no added producer'}
    def save():
        (args.output / 'run.json').write_text(json.dumps(run, indent=2, default=str) + '\n')
    save()
    try:
        for records in map(int, args.records.split(',')):
            for workload in args.workloads.split(','):
                for trial in range(args.repetitions):
                    # Reverse paired backend order between trials; no competing
                    # timing processes and no independent source/build changes.
                    order = backend_names if trial % 2 == 0 else list(reversed(backend_names))
                    for mode in modes:
                        for backend in order:
                            name = f'{records}-{workload}-{trial}-{mode}-{backend}'
                            destination = args.output / name
                            command = ['taskset', '-c', str(args.cpu), str(measured_binary),
                                       '--backend', backend, '--workload', workload,
                                       '--records', str(records), '--operations', str(args.operations),
                                       '--seed', str(args.seed), '--mode', mode, '--output', str(destination)]
                            command += ['--batch-width', str(args.batch_width), '--read-execution', args.read_execution]
                            if backend.startswith('packed') and args.prepared_pages:
                                command += ['--prepared-pages', str(args.prepared_pages),
                                            '--preparer-cpu', str(args.preparer_cpu),
                                            '--prepared-prefill', args.prepared_prefill]
                            if args.read_active != 16:
                                command += ['--read-active', str(args.read_active)]
                            if args.cache_setup != 'loaded':
                                command += ['--cache-setup', args.cache_setup,
                                            '--warmup-passes', str(args.warmup_passes)]
                            if args.pmu != 'off':
                                command += ['--pmu', args.pmu]
                                for event in args.pmu_event:
                                    command += ['--pmu-event', event]
                            if args.memory_receipt == 'on':
                                command += ['--memory-receipt', 'on']
                            started = time.monotonic()
                            with (args.output / (name + '.log')).open('w') as log:
                                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
                            run['runs'].append({'directory': name, 'trial': trial, 'command': command,
                                                'seconds': time.monotonic() - started})
                            save(); print(f'{name}: complete', flush=True)
        summarize(args.output)
        if hashlib.sha256(measured_binary.read_bytes()).hexdigest() != binary_hash:
            raise ValueError('Measured binary changed during campaign')
        run['status'] = 'complete'; save()
    except BaseException as error:
        run['status'] = 'failed'; run['error'] = str(error); save(); raise


if __name__ == '__main__':
    main()
