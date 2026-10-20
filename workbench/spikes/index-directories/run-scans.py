#!/usr/bin/env python3
"""Bounded complete-scan screen; native peers, one snapshot per scan."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from capture import verify


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, required=True)
    parser.add_argument('--helper-cpu', type=int, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--densities', action='store_true')
    mode.add_argument('--dense-probe', action='store_true')
    mode.add_argument('--lookahead-probe', action='store_true')
    mode.add_argument('--sparse-peer-probe', action='store_true')
    args = parser.parse_args()
    args.driver = args.driver.resolve()
    args.capture = args.capture.resolve()
    captured = verify(args.capture)
    args.output.mkdir(parents=True, exist_ok=False)
    receipt = {'kind': 'complete same-Scope/native-txn scan throughput',
               'source_digest': captured['source_digest'], 'capture': str(args.capture),
               'binary': str(args.driver), 'binary_before': sha(args.driver),
               'host': platform.uname()._asdict(), 'cpu': args.cpu,
               'helper_cpu': args.helper_cpu, 'runs': [],
               'mode': 'sparse-peer' if args.sparse_peer_probe else
                       'lookahead' if args.lookahead_probe else 'dense' if args.dense_probe else
                       'densities' if args.densities else 'basic',
               'counter_schema': 'native_cursor_gets counts mdb_cursor_get only = '
                                 'SET_RANGE seeks + NEXTs; txn renew/reset and any '
                                 'needed cursor renew are inside time, not this counter. '
                                 'Engine visited is logical candidate count, not physical loads. '
                                 'Offered is shared dense prefix-domain cardinality.',
               'limits': 'Dense one64B-row-per-Segment L4 physical-prefix component. '
                         'Logical16/F32 and Part grain fixed; physical allocation16/64/256. '
                         'Prepared external masks; predicate generation excluded. '
                         'Scope/root/seek/traversal/close or native renew/cursor/seek/reset '
                         'inside EACH scan; >=1M candidate prefixes per sample. '
                         'Setup/teardown outside foreground; no request-p99, coldness, '
                         'durability, complete Engine or Calico masked-pushdown claim. '
                         'Consumer bytes and logical visits are not physical traffic.'}
    rows = []
    try:
        for order in (0, 1):
            narrow = args.dense_probe or args.lookahead_probe
            populations = (262144,) if narrow else (8192, 262144) if not order else (262144, 8192)
            grains = ((16,64) if not order else (64,16)) if narrow else \
                (16, 64, 256) if not order else (256, 64, 16)
            if args.sparse_peer_probe:
                grains = (16,)
            for population in populations:
                for grain in grains:
                    name = f'o{order}-n{population}-g{grain}'
                    database = (args.output / name).resolve()  # driver creates it NEW
                    argv = ['taskset', '-c', str(args.cpu), str(args.driver), str(database),
                            str(population), '4' if args.lookahead_probe else '6' if
                            args.dense_probe or args.sparse_peer_probe else '2',
                            str(grain), str(args.helper_cpu), 'sparse-peer' if args.sparse_peer_probe else
                            'lookahead' if args.lookahead_probe else
                            'dense' if args.dense_probe else 'densities' if args.densities else 'basic']
                    result = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE)
                    (args.output / f'{name}.csv').write_text(result.stdout)
                    (args.output / f'{name}.log').write_text(result.stderr)
                    receipt['runs'].append({'id': name, 'argv': argv, 'returncode': result.returncode})
                    if result.returncode:
                        raise RuntimeError(f'{name}: {result.stderr[-2000:]}')
                    for row in csv.DictReader(io.StringIO(result.stdout)):
                        row.update(process=name, outer_order=order)
                        scans = int(row['scans_per_sample'])
                        visited = int(row['visited_per_scan'])
                        offered = int(row['offered_per_scan'])
                        selected = int(row['selected_per_scan'])
                        if scans * offered < 1 << 20:
                            raise RuntimeError('sample below declared candidate cardinality')
                        for clock in ('wall', 'owner', 'process'):
                            per_scan = int(row[f'{clock}_ns']) / scans
                            row[f'{clock}_ns_scan'] = per_scan
                            row[f'{clock}_ns_offered'] = per_scan / offered
                            row[f'{clock}_ns_visited'] = per_scan / visited if visited else None
                            row[f'{clock}_ns_selected'] = per_scan / selected if selected else None
                        rows.append(row)
                    # Only this new runner-created database, after backend close.
                    for file in database.iterdir():
                        file.unlink()
                    database.rmdir()
        outputs = {}
        groups = {}
        for row in rows:
            domain = (row['population'], row['range'], row['mask_kept_per64'])
            outputs.setdefault(domain, set()).add((row['scans_per_sample'],
                row['offered_per_scan'], row['selected_per_scan'], row['checksum']))
            key = domain + (row['grain'], row['arm'])
            groups.setdefault(key, []).append(row)
        if any(len(values) != 1 for values in outputs.values()):
            raise RuntimeError('peer/grain/process useful output or denominator differs')
        with (args.output / 'measurements.csv').open('w') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        summary = []
        for key, samples in groups.items():
            record = dict(zip(('population', 'range', 'mask_kept_per64', 'grain', 'arm'), key))
            record['samples'] = len(samples)
            record['processes'] = len({s['process'] for s in samples})
            for metric in ('wall_ns_scan', 'owner_ns_scan', 'process_ns_scan',
                           'wall_ns_visited', 'wall_ns_selected'):
                # Actual visits differ for run seeks and zero guards; offered
                # candidates are the common workload denominator.
                values = [s[metric] for s in samples if s[metric] is not None]
                record[metric] = ({'median': statistics.median(values), 'min': min(values),
                                   'max': max(values)} if values else None)
            values = [s['wall_ns_offered'] for s in samples]
            record['wall_ns_offered'] = {'median': statistics.median(values),
                                        'min': min(values), 'max': max(values)}
            summary.append(record)
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        receipt['status'] = 'complete'
    finally:
        receipt['runner_sha256'] = sha(__file__)
        receipt['binary_after'] = sha(args.driver)
        (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if receipt['binary_before'] != receipt['binary_after']:
        raise RuntimeError('driver changed during screen')
    print(f'{len(rows)} scan samples, all complete outputs/denominators match; {args.output}')


if __name__ == '__main__':
    main()
