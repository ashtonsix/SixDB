#!/usr/bin/env python3
"""One bounded full-cycle producer screen; no generic benchmark framework."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import platform
import statistics
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--baseline-driver', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, required=True)
    parser.add_argument('--helper-cpu', type=int, required=True)
    parser.add_argument('--commands', type=int, default=262144)
    args = parser.parse_args()
    args.driver = args.driver.resolve()
    if args.baseline_driver:
        args.baseline_driver = args.baseline_driver.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    # batch, hot rows, point reads/request, SUM period, explicit fold period,
    # actual failed-prepare+retry period. No timing is inferred from counters.
    cases = {
        'b1-hot2-append': (1, 2, 0, 0, 0, 0),
        'b1-hot64-queries-fold-refusal': (1, 64, 8, 64, 4096, 8191),
        'b64-hot2-coalesced': (64, 2, 0, 0, 0, 0),
        'b64-hot64-queries-fold-refusal': (64, 64, 8, 16, 256, 511),
    }
    arms = ['plain', 'indexed', 'eager', 'lmdb-get', 'lmdb-cursor']
    binaries = [args.driver] + ([args.baseline_driver] if args.baseline_driver else [])
    receipt = {'format': 1, 'host': platform.uname()._asdict(),
               'commands': args.commands, 'cpu': args.cpu, 'helper_cpu': args.helper_cpu,
               'binaries_before': {str(p): sha(p) for p in binaries}, 'runs': [],
               'limits': 'Warm finite64-row/5-layer single-owner CPU-only producer control. '
               'BE4 LMDB, not best-of-all numeric-key policies. No Calico atomic64/old-cut '
               'comparison, validated coldness, p99, native Zen5 or general Engine claim. '
               'Whole-history includes backend setup, final oracle, destruction and shutdown. '
               'Setup/private input replay outside foreground, external command/oracle preparation '
               'outside whole-history for every arm. No forced peer compaction. '
               'Injected LMDB abort is declared retry work, not the same physical failure cause.'}
    rows = []
    try:
        for name, case in cases.items():
            batch, hot, points, sum_period, fold_period, refusal_period = case
            current = list(arms)
            if args.baseline_driver and name == 'b1-hot2-append':
                current += ['baseline-plain']
            # Each arm occupies every position once, in independent processes.
            for trial in range(len(current)):
                order = current[trial % len(current):] + current[:trial % len(current)]
                for position, arm in enumerate(order):
                    binary = args.baseline_driver if arm.startswith('baseline-') else args.driver
                    policy = arm.removeprefix('baseline-')
                    run_id = f'{name}-{trial}-{position}-{arm}'
                    database = args.output / run_id
                    database.mkdir()
                    argv = ['taskset', '-c', str(args.cpu), str(binary), policy,
                            str(database.resolve()), str(args.commands), str(batch), str(hot),
                            str(points), str(sum_period), str(fold_period), str(refusal_period),
                            str(args.helper_cpu)]
                    result = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, check=False)
                    (args.output / f'{run_id}.csv').write_text(result.stdout)
                    (args.output / f'{run_id}.log').write_text(result.stderr)
                    receipt['runs'].append({'id': run_id, 'case': name, 'trial': trial,
                                            'position': position, 'arm': arm, 'argv': argv,
                                            'returncode': result.returncode})
                    if result.returncode:
                        raise RuntimeError(f'{run_id}: {result.stderr[-2000:]}')
                    for row in csv.DictReader(io.StringIO(result.stdout)):
                        row.update(case=name, trial=trial, position=position, arm=arm)
                        rows.append(row)
                    # Preserve physical LMDB files through stats/oracles/process close,
                    # then remove only these runner-created databases, outside clocks.
                    for file in database.iterdir():
                        file.unlink()
                    database.rmdir()
        checksums = {}
        for row in rows:
            checksums.setdefault((row['case'], row['phase']), set()).add(row['checksum'])
        if any(len(values) != 1 for values in checksums.values()):
            raise RuntimeError('Cross-arm useful output checksums differ')
        with (args.output / 'measurements.csv').open('w') as output:
            writer = csv.DictWriter(output, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        groups = {}
        for row in rows:
            key = (row['case'], row['phase'], row['arm'])
            groups.setdefault(key, []).append(row)
        summary = []
        for (case, phase, arm), samples in groups.items():
            item = {'case': case, 'phase': phase, 'arm': arm, 'repetitions': len(samples)}
            for metric in ['wall_ns_request', 'wall_ns_command', 'owner_ns_command', 'process_ns_command']:
                values = [float(x[metric]) for x in samples]
                item[metric] = {'median': statistics.median(values), 'min': min(values), 'max': max(values)}
            summary.append(item)
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2))
        receipt['status'] = 'complete'
    finally:
        receipt['binaries_after'] = {str(p): sha(p) for p in binaries}
        receipt['runner_sha256'] = sha(__file__)
        (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2))
    if receipt['binaries_before'] != receipt['binaries_after']:
        raise RuntimeError('Measured binaries changed')
    print(f'{len(rows)} measurements; all checksums match; {args.output}')


if __name__ == '__main__':
    main()
