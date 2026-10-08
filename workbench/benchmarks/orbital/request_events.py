#!/usr/bin/env python3
"""Join untimed logical maintenance labels to an exact scalar latency replay."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from capture import verify


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def quantile(values, percent):
    values = sorted(values)
    return values[(len(values) - 1) * percent // 100] if values else None


def join(measured, replay, events, samples, match_maintenance=False):
    identity = ['backend', 'workload', 'records', 'seed', 'input_fingerprint',
                'trace_fingerprint', 'final_fingerprint', 'offered_requests',
                'completed_requests', 'logical_rows_read', 'logical_changes']
    for field in identity:
        if measured[field] != replay[field]:
            raise ValueError(f'replay identity differs: {field}')
    count = measured['completed_requests']
    if not count or len(events) != count or len(samples) != count:
        raise ValueError('incomplete event/sample coverage')
    if [r['request'] for r in events] != list(range(count)) or [i for i, _ in samples] != list(range(count)):
        raise ValueError('request indices are missing, duplicated or reordered')
    for column, field in [('splits', 'candidate_splits'), ('rows', 'logical_rows_read'), ('changes', 'logical_changes')]:
        if sum(row[column] for row in events) != measured[field] or replay[field] != measured[field]:
            raise ValueError(f'replay totals differ: {field}')
    maintenance = {}
    for column, field in [('pack_bytes', 'candidate_issued_write_bytes'), ('pack_spans', 'candidate_issued_write_spans'),
                          ('index_bytes', 'candidate_index_write_bytes'), ('index_spans', 'candidate_index_write_spans'),
                          ('lease_calls', 'candidate_lease_calls')]:
        observed = {'event_sum': sum(row[column] for row in events), 'replay_total': replay[field],
                    'measured_total': measured[field]}
        maintenance[field] = observed
        if match_maintenance and len(set(observed.values())) != 1:
            raise ValueError(f'replay maintenance totals differ: {field}')
    split = [ns for i, ns in samples if events[i]['splits']]
    other = [ns for i, ns in samples if not events[i]['splits']]
    top = sorted(samples, key=lambda item: item[1])[-max(1, (count + 99) // 100):]
    total = sum(ns for _, ns in samples)
    return {'matched_identity_fields': identity, 'requests': count, 'split_requests': len(split),
            'split_p50_ns': quantile(split, 50), 'split_p99_ns': quantile(split, 99),
            'non_split_p50_ns': quantile(other, 50), 'non_split_p99_ns': quantile(other, 99),
            'split_share_summed_request_latency': sum(split) / total if total else None,
            'split_fraction_slowest_1pct': sum(bool(events[i]['splits']) for i, _ in top) / len(top),
            'replay_page_creations': sum(r['pages_created'] for r in events),
            'maintenance_totals': maintenance, 'maintenance_matching_required': match_maintenance}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--measured', type=Path, required=True, help='scalar latency case directory')
    parser.add_argument('--source', type=Path, required=True, help='verified capture used for the event binary')
    parser.add_argument('--manifest', type=Path, required=True, help='measured worker source-manifest.json')
    parser.add_argument('--collection', type=Path, help='worker collection receipt; otherwise use the adjacent receipt if present')
    parser.add_argument('--match-maintenance', action='store_true', help='also require exact pack/index/lease totals for synchronous construction')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    capture = verify(args.source)
    target = json.loads(args.manifest.read_text())
    # Reuse identical composed implementation inputs. main.cpp alone adds the
    # untimed observer; no candidate change is admitted into a classification.
    source = capture['source_files_sha256']
    relevant = [name for name in source.keys() | target.keys()
                if name.endswith(('.cpp', '.h', '.hpp', '.inc', '.ipp', '.cmake', 'CMakeLists.txt')) and
                (name == 'CMakeLists.txt' or
                 name.startswith(('orbital/', 'loom/', 'ikea/', 'cmake/', 'workbench/benchmarks/orbital/')))
                and name != 'workbench/benchmarks/orbital/main.cpp']
    changed = [name for name in relevant if source.get(name) != target.get(name)]
    if changed:
        raise ValueError('composed source differs: ' + ', '.join(sorted(changed)[:8]))
    collection_path = args.collection or args.manifest.parent.parent / 'collection.json'
    collection_checked = collection_path.exists()
    if args.collection and not collection_checked:
        raise ValueError('missing supplied collection receipt')
    if collection_checked:
        members = json.loads(collection_path.read_text())['members']
        for path in [args.manifest, args.measured / 'result.json', args.measured / 'latencies.csv']:
            name = path.resolve().relative_to(args.manifest.parent.resolve()).as_posix()
            if name not in members or digest(path) != members[name]['sha256']:
                raise ValueError(f'measured collection bytes differ: {name}')
    measured = json.loads((args.measured / 'result.json').read_text())
    if (not measured['validated'] or measured['mode'] != 'latency' or measured['batch_width'] != 1
            or measured['read_execution'] != 'serial' or measured['cache_setup'] != 'loaded'
            or measured['latency_unit'] != 'logical request' or not measured['backend'].startswith('packed')):
        raise ValueError('choose a validated scalar loaded packed latency case')
    args.output.mkdir(parents=True, exist_ok=False)
    binary = args.output.resolve() / 'orbital_allocation_bench'
    shutil.copy2(args.binary, binary)
    binary_hash = digest(binary)
    replay_dir = args.output / 'replay'
    command = [str(binary), '--mode', 'events', '--output', str(replay_dir), '--backend', measured['backend'],
               '--workload', measured['workload'], '--records', str(measured['records']),
               '--operations', str(measured['requested_trace_length']), '--seed', str(measured['seed'])]
    subprocess.run(command, check=True)
    if digest(binary) != binary_hash:
        raise ValueError('event binary changed during replay')
    replay = json.loads((replay_dir / 'result.json').read_text())
    if not replay['validated'] or replay['mode'] != 'events' or replay['elapsed_ns'] != 0:
        raise ValueError('event replay unexpectedly collected timings or failed validation')
    with (replay_dir / 'request-events.csv').open() as stream:
        events = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(stream)]
    with (args.measured / 'latencies.csv').open() as stream:
        samples = [(int(row['request']), float(row['latency_ns'])) for row in csv.DictReader(stream)]
    report = {'schema': 1, 'classification': 'untimed deterministic logical replay; target latency samples',
              'event_source_digest': capture['source_digest'], 'event_binary_sha256': binary_hash,
              'helper_sha256': digest(Path(__file__)), 'measured_collection_hashes_checked': collection_checked,
              'measured_source_manifest_sha256': digest(args.manifest),
              'measured_result_sha256': digest(args.measured / 'result.json'),
              'measured_latencies_sha256': digest(args.measured / 'latencies.csv'),
              'command': command, 'summary': join(measured, replay, events, samples, args.match_maintenance),
              'limits': ['Events are replay labels, not target interval PMU or phase timings.',
                         'The caller must establish that structural split policy is deterministic.',
                         'Background creation, ready hits, stockouts and waits require flags from the actual measured execution.',
                         'Splits combine allocation, initialization, row transfer, index maintenance and publication.',
                         'Page creations do not identify each physical Pool allocation/growth.',
                         'Summed sampled request latency includes timer overhead; it is not throughput elapsed time.',
                         'Source matching excludes the driver observer; binary build provenance is supplied by its caller.']}
    (args.output / 'analysis.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['summary']))


if __name__ == '__main__':
    main()
