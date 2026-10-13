#!/usr/bin/env python3
"""Independently qualify retained Loom PMU receipts; no build or binary run."""
import argparse
import csv
import difflib
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('retained_comparisons', HERE / 'aggregate.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
ARMS = shared.ARMS
SIZES = (4096, 4194304)
IMPLEMENTATIONS = ('old', 'explicit', 'grain4')
MODES = ('basic', 'stalls')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text())


def qualify(m, mode):
    assert m['mode'] == mode and m['status'] == 'complete'
    assert m['time_enabled_ns'] == m['time_running_ns'] > 0
    assert m['coverage'] == 1 and not m['scaled'] and not m['multiplexed']
    assert m['scope'] == 'calling_thread_user_space'
    assert m['pid'] == 0 and m['cpu'] == -1 and not m['inherit']
    assert m['exclude_kernel'] and m['exclude_hv']
    assert not m['errors'] and not m['errors_truncated']
    e = {x['name']: x for x in m['events']}
    assert len(e) == len(m['events'])
    expected = {'cpu_cycles': 0, 'retired_instructions': 1}
    if mode == 'stalls':
        expected.update(frontend_stall_cycles=7, backend_stall_cycles=8)
    assert set(e) == set(expected)
    ids = []
    for name, config in expected.items():
        x = e[name]
        assert x['type'] == 0 and int(x['config'], 0) == config
        if name == 'backend_stall_cycles':
            assert not x['required'] and not x['opened'] and not x['in_group']
            assert x['id'] is None and x['count'] is None and x['status'] == 'unavailable'
            assert len(x['errors']) == 1 and x['errors'][0]['phase'] == 'open'
            assert x['errors'][0]['errno'] == 22
            continue
        assert x['required'] == (name in ('cpu_cycles', 'retired_instructions'))
        assert x['opened'] and x['in_group'] and x['status'] == 'measured'
        assert isinstance(x['id'], int) and isinstance(x['count'], int) and x['count'] > 0
        assert not x['errors']
        ids.append(x['id'])
    assert len(ids) == len(set(ids))
    assert set(m['opened_event_set']) == {name for name, x in e.items() if x['in_group']}
    ipc = e['retired_instructions']['count'] / e['cpu_cycles']['count']
    assert math.isclose(ipc, m['ipc'], rel_tol=1e-14) and m['ipc_scope'] == 'observed_same_group'
    return e, ipc


def distribution(samples):
    return {'median': statistics.median(samples), 'min': min(samples), 'max': max(samples), 'samples': samples}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--job', default='20261013T021257Z-82cd7665')
    parser.add_argument('--workers', type=Path, default=Path('build/workers'))
    parser.add_argument('--source', type=Path, default=HERE.parent / 'native-pmu-source')
    parser.add_argument('--output', type=Path, default=HERE / 'pmu-evidence.json')
    args = parser.parse_args()
    root = args.workers / args.job / 'results'
    result = {'format': 1, 'job_id': args.job, 'worker_status': load(root / 'worker-result.json'),
              'files': {}, 'probes': {}, 'sources': {}, 'samples': [], 'cells': []}
    assert result['worker_status']['state'] == 'complete' and result['worker_status']['script_returncode'] == 0
    status = load(root / 'pmu-workload-status.json')
    assert status['status'] == 'collected' and status['sample_count'] == status['accepted_count'] == 480
    assert status['no_software_substitutes'] and status['no_raw_encodings']
    manifest = load(root / 'source-manifest.json')
    pmu_provenance = load(root / 'pmu-source-provenance.json')
    source_paths = ['run-pmu.sh', 'controls/source-provenance.json', 'controls/pmu/provenance.json',
                    'controls/old/loom/ring.h', 'controls/new/loom/ring.h', 'loom/include/loom/ring.h',
                    'workbench/spikes/hashtable/map.h', 'workbench/spikes/hashtable/ring.cpp',
                    'controls/explicit/ring.cpp', 'controls/cohort/ring.cpp', 'controls/pmu/check_receipts.py']
    source_paths += list(pmu_provenance['pmu_sources'])
    for impl in IMPLEMENTATIONS:
        source_paths.extend(['controls/pmu/' + impl + '.cpp', 'controls/pmu/' + impl + '-instrumentation.patch'])
    for name in source_paths:
        actual = sha(args.source / name)
        assert actual == manifest[name], (name, 'local source differs from captured manifest')
        result['sources'][name] = actual
    assert pmu_provenance['ring_header_sha256'] == manifest['controls/new/loom/ring.h'] == manifest['loom/include/loom/ring.h']
    for name, digest in pmu_provenance['pmu_sources'].items():
        assert manifest[name] == digest
    for impl, digest in pmu_provenance['instrumented_driver_sha256'].items():
        assert manifest['controls/pmu/' + impl + '.cpp'] == digest
    # Verify each retained instrumentation diff describes precisely its driver;
    # its only modifications are at includes, timed interval, and timed call.
    original = {'old': 'workbench/spikes/hashtable/ring.cpp', 'explicit': 'controls/explicit/ring.cpp', 'grain4': 'controls/cohort/ring.cpp'}
    diff_headers = {'old': 'workbench/spikes/hashtable/ring.cpp', 'explicit': 'controls/explicit/ring.cpp', 'grain4': 'controls/cohort/ring.cpp'}
    for impl in IMPLEMENTATIONS:
        original_text = (args.source / original[impl]).read_text().splitlines(keepends=True)
        patched_text = (args.source / ('controls/pmu/' + impl + '.cpp')).read_text().splitlines(keepends=True)
        patch = (args.source / ('controls/pmu/' + impl + '-instrumentation.patch')).read_text()
        actual = ''.join(difflib.unified_diff(original_text, patched_text, fromfile=diff_headers[impl], tofile='pmu/' + impl + '.cpp'))
        assert actual == patch, (impl, 'instrumentation patch does not reconstruct driver')
        assert sha(root / (impl + '-instrumentation.patch')) == sha(args.source / ('controls/pmu/' + impl + '-instrumentation.patch'))
    for mode in MODES:
        probe = load(root / ('probe-' + mode + '.json'))
        qualify(probe['measurement'], mode)
        assert load(root / ('probe-' + mode + '-status.json'))['accepted']
        result['probes'][mode] = probe
    hardware = load(root / 'hardware-neon.json')
    assert not hardware['calibration_enabled'] and hardware['reported']['selected_cpu'] == 0
    result['hardware'] = hardware
    comp = load(root / 'compile-neon.json')
    driver_compiles = [x for x in comp if '/controls/pmu/' in x['file']]
    assert len(driver_compiles) == 3
    for x in driver_compiles:
        impl = Path(x['file']).stem
        command = x['command']
        assert '-O3' in command and '-std=c++23' in command and '-mtune=neoverse-v2' in command
        assert '-march=armv8-a+simd' in command and '-fno-vectorize' in command and '-fno-slp-vectorize' in command
        assert '-flto' not in command
        wanted = 'old' if impl == 'old' else 'new'
        assert command.index('-I/opt/sixdb/source/controls/' + wanted) < command.index('-I/opt/sixdb/source/loom/include')
    result['driver_compiles'] = driver_compiles
    order, repaired = shared.executable_order(root / 'executable-order.csv')
    assert len(order) == 12 and {(x['mode'], x['policy'], int(x['n'])) for x in order} == {(m, i, n) for m in MODES for i in IMPLEMENTATIONS for n in SIZES}
    assert all(int(x['reps']) == 5 and int(x['chains']) == 32 for x in order)
    result['executable_order'] = order
    result['order_timestamp_decimal_comma_rows_decoded'] = repaired
    checksums = shared.expected_checksums()
    result['independent_key_set_checksums'] = {n: checksums[n] for n in SIZES}
    for mode in MODES:
        for n in SIZES:
            for impl in IMPLEMENTATIONS:
                stem = f'neon-{mode}-{impl}-{n}-32'
                csv_path, ctx_path = root / (stem + '.csv'), root / (stem + '.context')
                rows = list(csv.DictReader(csv_path.open()))
                assert len(rows) == 40 and {(int(x['rep']), x['execution']) for x in rows} == {(r, arm) for r in range(5) for arm in ARMS}
                ctx = shared.context(ctx_path)
                assert ctx['hardware_context'] == hardware['behavior_context']
                assert ctx['calibration_enabled'] == 'false'
                assert {k:ctx[k] for k in ['policy_revision','pressure','line_bytes','page_bytes','demand_code','selected_hint']} == dict(policy_revision='0',pressure='0',line_bytes='64',page_bytes='4096',demand_code='0',selected_hint='1')
                if impl == 'grain4':
                    assert {k:ctx[k] for k in ['configured_grain','effective_grain','groups','prepared_map_fact','logical_visits_origin','ragged_check']} == dict(configured_grain='4',effective_grain='4',groups='8',prepared_map_fact='true',logical_visits_origin='derived_equal_lengths',ragged_check='passed')
                receipt_dir = root / (stem + '.receipts')
                paths = list(receipt_dir.glob('*.json'))
                assert len(paths) == 40
                reported = load(root / (stem + '.pmu-status.json'))
                assert reported['accepted'] and reported['sample_count'] == reported['accepted_count'] == 40
                for row in rows:
                    rounds = max(1,262144//n)
                    assert int(row['n']) == n and int(row['chains']) == 32 and int(row['rounds']) == rounds
                    assert int(row['checksum']) == checksums[n]
                    assert int(row['key_input_bytes']) == n*8 and int(row['hint_code']) == 1
                    assert sum(int(row[k]) for k in ['primary_keys','secondary_keys','overflow_keys']) == n
                    ring = row['execution'] in ('ring','ring-hints')
                    effective = 4 if impl == 'grain4' else 1
                    visits = 2*n*rounds if ring else 0
                    assert int(row['ring_visits']) == visits//effective
                    if impl == 'grain4':
                        assert [int(row[k]) for k in ['configured_grain','effective_grain','groups']] == [4,4,8]
                        assert int(row['useful_item_stage_visits']) == visits
                        assert row['useful_item_stage_visits_origin'] == 'derived_equal_lengths'
                    name = f"{row['rep']}-{row['execution']}.json"
                    path = receipt_dir / name
                    raw = load(path)
                    assert raw['schema'] == 1 and raw['implementation'] == impl and raw['mode'] == mode
                    assert raw['execution'] == row['execution'] and raw['rep'] == int(row['rep'])
                    assert raw['n'] == n and raw['chains'] == 32 and raw['rounds'] == rounds and raw['logical_lookups'] == n*rounds
                    assert raw['validated'] and raw['operation_setup_enqueue_drain_charged']
                    assert raw['cold_setup_warmup_validation_receipt_excluded'] and raw['clock_reads_in_hardware_interval']
                    assert raw['cut'] == 'full_operation_interval_plus_clock_and_user_boundary_code'
                    e, ipc = qualify(raw['measurement'], mode)
                    l = raw['logical_lookups']; cycles = e['cpu_cycles']['count']; instructions = e['retired_instructions']['count']
                    # Receipt wall duration was printed at six significant digits;
                    # CSV used %.6f after dividing the same duration.
                    ns = float(row['ns_per_lookup'])
                    assert math.isclose(raw['wall_interval_ns']/l, ns, rel_tol=6e-6)
                    assert ns > 0 and math.isfinite(ns)
                    sample = {'file': str(path.relative_to(root)), 'sha256': sha(path), 'mode': mode, 'implementation': impl,
                              'n': n, 'chains': 32, 'execution': raw['execution'], 'rep': raw['rep'], 'logical_lookups': l,
                              'ring_visits': int(row['ring_visits']), 'useful_item_stage_visits': visits,
                              'ns_per_lookup': ns, 'cycles_per_lookup': cycles/l, 'instructions_per_lookup': instructions/l,
                              'ipc': ipc, 'cycles': cycles, 'instructions': instructions,
                              'wall_interval_ns': raw['wall_interval_ns'], 'enabled_running_ns': raw['measurement']['time_enabled_ns'],
                              'enabled_relative_to_wall': raw['measurement']['time_enabled_ns']/raw['wall_interval_ns']-1}
                    if mode == 'stalls':
                        front = e['frontend_stall_cycles']['count']
                        sample.update(frontend_stall_cycles=front, frontend_stall_cycles_per_lookup=front/l, frontend_stall_cycles_fraction=front/cycles,
                                      backend_stall_status='unavailable', backend_stall_errno=22)
                    result['samples'].append(sample)
                for name in [csv_path.name, ctx_path.name, stem + '.pmu-status.json']:
                    result['files'][name] = sha(root / name)
    assert len(result['samples']) == 480
    metrics = ['ns_per_lookup','cycles_per_lookup','instructions_per_lookup','ipc','enabled_relative_to_wall']
    for mode in MODES:
        for n in SIZES:
            for impl in IMPLEMENTATIONS:
                for arm in sorted(ARMS):
                    selected = [s for s in result['samples'] if (s['mode'],s['n'],s['implementation'],s['execution']) == (mode,n,impl,arm)]
                    assert len(selected) == 5
                    keys = metrics + (['frontend_stall_cycles_per_lookup','frontend_stall_cycles_fraction'] if mode == 'stalls' else [])
                    result['cells'].append({'mode':mode,'n':n,'chains':32,'implementation':impl,'execution':arm,
                                            **{key:distribution([s[key] for s in selected]) for key in keys}})
    for name in ['source-manifest.json','pmu-source-provenance.json','worker-result.json','pmu-workload-status.json',
                 'hardware-neon.json','compiler.txt','compile-neon.json','perf-event-paranoid.txt','binary-sha256-neon.txt','executable-order.csv',
                 'probe-basic.json','probe-stalls.json','probe-basic-status.json','probe-stalls-status.json']:
        result['files'][name] = sha(root / name)
    result['qualification'] = '480 source-matched, checksum-qualified, raw-to-CSV matched hardware intervals; required pair and frontend complete without scaling or multiplexing'
    result['limits'] = [
        'Backend generic stall event is unavailable with EINVAL; no backend, memory-stall, cache-tier, outstanding-miss, or stall-cause decomposition.',
        'Generic frontend stall cycles are counted by the kernel PMU mapping; they do not identify a specific frontend cause.',
        'PMU interval includes both steady-clock reads and user-side start/stop boundary instructions; kernel/hypervisor are excluded.',
        'Job setup, enqueue and drain are charged; cold Session setup, FD opens, perf reads, validation, receipts and warmup are excluded.',
        'Instrumented executable measurements are separate from throughput baseline, not proof of unchanged native code layout.',
        'Same-work controls alternate arm order within repetitions; implementation executables are sequential and reverse by size, not contemporaneous pairs.',
        'Coverage 1 means no counter multiplexing; it does not prove an uninterrupted sample. PMU enabled time differs from wall time in some samples; no frequency inferred.',
        'All arms use C32 independent dependent chains. Direct is not a single-request serial-latency control.',
        'IPC is instructions/cycles; higher IPC can accompany more instructions and slower operations.',
        'Grain-four useful stage visits are derived equal-length work accounting; actual Ring visits are four times fewer.',
        'Tiny maps are warmed; a large resident allocation is not proof of each load being served by DRAM.'
    ]
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    compact=[]
    for c in result['cells']:
        compact.append({k:(v['median'] if isinstance(v,dict) else v) for k,v in c.items()})
    keys=list(dict.fromkeys(key for row in compact for key in row))
    with args.output.with_suffix('.csv').open('w') as stream:
        writer=csv.DictWriter(stream,fieldnames=keys);writer.writeheader();writer.writerows(compact)
    print(args.output, len(result['samples']), 'validated hardware samples', len(result['cells']), 'cells')


if __name__ == '__main__':
    main()
