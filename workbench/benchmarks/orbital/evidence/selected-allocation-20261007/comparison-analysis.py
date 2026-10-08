#!/usr/bin/env python3
"""Compare qualified complete panels; retain actual readiness and resource costs."""
import csv
import hashlib
import json
from pathlib import Path
from statistics import median

HERE = Path(__file__).resolve().parent
PANELS = ('all19-synchronous', 'all19-prepared-same-cpu')
OUT = HERE / 'qualified' / 'sync-same-cpu-comparison'
OUT.mkdir(parents=True, exist_ok=True)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read(path):
    return json.loads(path.read_text())

def quantile(values, percent):
    ordered = sorted(values)
    return ordered[(len(ordered) - 1) * percent // 100] if ordered else None

panels = []
for name in PANELS:
    path = HERE / 'qualified' / name
    qualification = read(path / 'qualification.json')
    assert qualification['qualified_cases'] == 1710
    assert qualification['original_worker_state'] == 'complete'
    assert sha(path / 'owner-headroom.json') == qualification['owner_headroom_sha256']
    summary = read(path / 'summary-validator.json')
    assert summary['runner_sha256'] == qualification['validator_sha256']
    for filename, field in (('run.json', 'run_json_sha256'), ('cases.csv', 'cases_sha256'),
                            ('comparison.json', 'comparison_sha256')):
        assert sha(path / filename) == summary[field], filename
    manifest = read(path / 'run.json')
    assert manifest['status'] == 'complete'
    job = qualification['job']
    worker = Path.cwd() / 'build/workers' / job
    collection = read(worker / 'collection.json')
    cases = {}
    for item in manifest['runs']:
        directory = path / item['directory']
        for filename in ('result.json', 'preparation.json', 'preparation-samples.csv', 'latencies.csv'):
            original = directory / filename
            if original.exists():
                member = original.resolve().relative_to((worker / 'results').resolve()).as_posix()
                assert sha(original) == collection['members'][member]['sha256'], member
        result = read(directory / 'result.json')
        key = (result['records'], result['workload'], result['mode'], item['trial'], result['backend'])
        assert key not in cases
        cases[key] = (directory, result)
    host = read(worker / 'results/host.json')
    panels.append({'name': name, 'path': path, 'qualification': qualification, 'manifest': manifest,
                   'cases': cases, 'host': host, 'artifact': collection['artifact'],
                   'headroom': read(path / 'owner-headroom.json')})

sync, prep = panels
for field in ('source_digest', 'measured_binary_sha256', 'validator_sha256'):
    assert sync['qualification'][field] == prep['qualification'][field], field
assert sync['host']['instance']['instanceId'] == prep['host']['instance']['instanceId']
assert sync['cases'].keys() == prep['cases'].keys()
identity_fields = ('input_fingerprint', 'trace_fingerprint', 'final_fingerprint', 'requests',
                   'completed_requests', 'seed', 'cache_setup', 'read_execution', 'batch_width')
for key, (_, a) in sync['cases'].items():
    b = prep['cases'][key][1]
    assert all(a[field] == b[field] for field in identity_fields), key

conditions, resources, cells, lifecycles = [], [], [], []
for panel in panels:
    worker = Path.cwd() / 'build/workers' / panel['qualification']['job']
    collection = read(worker / 'collection.json')
    pressure = worker / 'results/orbital-allocation/pressure'
    context = read(pressure / 'context.json')
    assert context['verified'] and context['config']['slots'] == 16
    assert collection['members']['orbital-allocation/pressure/orbital_lifecycle_bench']['sha256'] == context['binary_sha256']
    rows = []
    for trial in range(3):
        for runtime in ('finite', 'window', 'mixed'):
            filename = pressure / f'{trial:02d}-{runtime}' / 'result.json'
            member = filename.relative_to(worker / 'results').as_posix()
            assert sha(filename) == collection['members'][member]['sha256']
            r = read(filename)
            assert r['runtime'] == runtime and r['verified']
            assert r['completed_cycles'] == r['offered_cycles'] == 64
            assert r['slots'] == 16 and r['caller_cpu'] == 0
            assert r['foreign_cpu'] == r['vm_helper_cpu'] == 1
            assert r['after_shutdown_charged_bytes'] == r['after_trim_charged_bytes'] == 0
            assert r['foreign_returns'] > 0 and r['issued_write_bytes'] > 0
            rows.append(r | {'panel': panel['name'], 'trial': trial,
                             'measured_binary_sha256': context['binary_sha256']})
    assert all(len({r['checksum'] for r in rows if r['runtime'] == runtime}) == 1
               for runtime in ('finite', 'window', 'mixed'))
    lifecycles += rows
for key, (directory, result) in prep['cases'].items():
    records, workload, mode, trial, backend = key
    if backend != 'packed-owner':
        continue
    receipt = read(directory / 'preparation.json')
    start, end, settled = (receipt[p] for p in ('startup', 'before_drain', 'settled'))
    assert all(p['requested_cpu'] == p['observed_cpu'] == p['owner_cpu'] == 0
               for p in (start, end, settled))
    assert all(settled[f] == 0 for f in ('inventory', 'ready', 'inflight', 'repair_wait',
                                       'ready_backing_bytes', 'preparing_bytes', 'preparing_reserved_bytes'))
    assert settled['peak_inventory'] <= 8
    base = {'records': records, 'workload': workload, 'mode': mode, 'trial': trial}
    requests = result['completed_requests']
    resource = base | {f: settled[f] for f in ('demands', 'ready_hits', 'stockouts', 'inline_fallbacks',
                'completed_pages', 'discarded_completed_pages', 'discarded_pack_bytes', 'discarded_pack_spans',
                'discarded_index_bytes', 'discarded_index_spans', 'unused_inventory', 'peak_inventory',
                'peak_arena_charge', 'arena_charge', 'private_metadata_bound_bytes', 'constructor_failures',
                'admission_failures', 'cleanup_retries')}
    resource.update({f'start_{f}': start[f] for f in ('inventory', 'ready', 'inflight', 'completed_pages',
        'completed_pack_bytes', 'completed_pack_spans', 'completed_index_bytes', 'completed_index_spans',
        'startup_ns', 'startup_process_cpu_ns', 'startup_producer_cpu_ns')})
    resource.update({f'end_{f}': end[f] for f in ('inventory', 'ready', 'inflight', 'completed_pages')})
    for kind in ('pages', 'pack_bytes', 'pack_spans', 'index_bytes', 'index_spans'):
        field = 'completed_' + kind
        resource['request_completed_' + kind] = end[field] - start[field]
        resource['drain_completed_' + kind] = settled[field] - end[field]
    resource.update({'process_cpu_ns_per_request': result['process_cpu_ns'] / requests,
        'caller_cpu_ns_per_request': result['thread_cpu_ns'] / requests,
        'producer_cpu_ns_per_request': receipt['producer_request_and_drain_cpu_ns'] / requests,
        'drain_ns': result['drain_ns'], 'settled_inventory': settled['inventory'],
        'stockout_fraction_of_page_demands': settled['stockouts'] / settled['demands'] if settled['demands'] else None})
    resources.append(resource)
    if mode != 'latency':
        continue
    groups = {'no_page_demand': [], 'ready_only': [], 'any_stockout': []}
    totals = {f: 0 for f in ('demands', 'ready_hits', 'stockouts', 'inline_fallbacks')}
    with (directory / 'preparation-samples.csv').open() as stream:
        for index, flag in enumerate(csv.DictReader(stream)):
            assert int(flag['first_request']) == index and int(flag['requests']) == 1
            for f in totals:
                totals[f] += int(flag[f])
            group = 'any_stockout' if int(flag['stockouts']) else 'ready_only' if int(flag['demands']) else 'no_page_demand'
            groups[group].append(int(flag['latency_ns']))
    assert sum(map(len, groups.values())) == requests
    assert all(totals[f] == settled[f] for f in totals)
    for group, values in groups.items():
        conditions.append(base | {'condition': group, 'samples': len(values),
            'p50_ns': quantile(values, 50), 'p99_ns': quantile(values, 99),
            'sample_time_sum_ns': sum(values)})

for records, workload in sorted({(k[0], k[1]) for k in sync['cases']}):
    row = {'records': records, 'workload': workload}
    for panel in panels:
        prefix = 'sync' if panel is sync else 'prep'
        for mode in ('throughput', 'latency'):
            owner = [panel['cases'][(records, workload, mode, trial, 'packed-owner')][1] for trial in range(3)]
            fields = ('requests_per_second',) if mode == 'throughput' else ('p50_ns', 'p99_ns')
            for f in fields:
                row[prefix + '_' + f] = median(r[f] for r in owner)
            for f in ('process_cpu_ns', 'thread_cpu_ns'):
                row[prefix + '_' + mode + '_' + f + '_per_request'] = median(r[f] / r['completed_requests'] for r in owner)
        for reference in ('lmdb', 'lmdb-read-batch64'):
            h = next(h for h in panel['headroom'] if h['records'] == records and h['workload'] == workload and h['reference'] == reference)
            for f in ('throughput_ratio', 'p50_ns_headroom_ns', 'p99_ns_headroom_ns'):
                row[prefix + '_' + reference + '_' + f] = h[f]
            for mode in ('throughput', 'latency'):
                controls = [panel['cases'][(records, workload, mode, t, reference)][1] for t in range(3)]
                for f in (('requests_per_second',) if mode == 'throughput' else ('p50_ns', 'p99_ns')):
                    row[prefix + '_' + reference + '_' + f] = median(r[f] for r in controls)
    row['prep_over_sync_throughput_ratio'] = row['prep_requests_per_second'] / row['sync_requests_per_second']
    row['prep_p50_change_ns'] = row['prep_p50_ns'] - row['sync_p50_ns']
    row['prep_p99_change_ns'] = row['prep_p99_ns'] - row['sync_p99_ns']
    cells.append(row)

for name, rows in (('cells', cells), ('resource-trials', resources),
                   ('conditioned-latency-trials', conditions), ('lifecycle-trials', lifecycles)):
    (OUT / (name + '.json')).write_text(json.dumps(rows, indent=2) + '\n')
    with (OUT / (name + '.csv')).open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
receipt = {'source_digest': sync['qualification']['source_digest'],
    'measured_binary_sha256': sync['qualification']['measured_binary_sha256'],
    'validator_sha256': sync['qualification']['validator_sha256'], 'analyzer_sha256': sha(Path(__file__)),
    'instance': sync['host']['instance']['instanceId'], 'cpu': 0, 'producer_cpu': 0,
    'adds_cpu_resource': False, 'qualified_cases_per_panel': 1710, 'matched_cells': len(cells),
    'resources': len(resources), 'conditional_trials': len(conditions),
    'verified_lifecycle_trials': len(lifecycles),
    'panels': [{k: p[k] for k in ('name', 'qualification', 'artifact')} for p in panels],
    'hashes': {p.name: sha(p) for p in OUT.iterdir() if p.suffix in ('.json', '.csv') and p.name != 'comparison-receipt.json'},
    'limits': ['Sequential three-trial medians on one guest and one fixed seed; no randomized cross-panel order or trace diversity.',
        'Loaded fresh-store history; LLC/DRAM load service unobserved. No isolation, durability, versioning, Engine or Shore.',
        'Conditional samples are actual flags joined to request latencies; they do not isolate a physical substep cause.',
        'All-process CPU includes helper and measured drain; startup costs separate. Producer CPU includes wake/service.',
        'Settled preparation inventory/backing zero; Arena charge includes ordinary live storage. Store teardown zero-charge has separate tests.',
        'Canceled/unused successful construction counted; partial failed constructor writes unquantified.',
        'Sample quantiles use floor((n-1)*percent/100); sample time sums include timer overhead and are not throughput time.']}
(OUT / 'comparison-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps({k:receipt[k] for k in ('source_digest', 'instance', 'matched_cells', 'resources', 'conditional_trials')}))
