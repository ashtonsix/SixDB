#!/usr/bin/env python3
"""Replay hash-pinned Google Benchmark binaries sequentially, without compiling."""
import argparse
import collections
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import time

import artifacts
import datasets

ROOT = Path(__file__).resolve().parents[2]


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def validate(spec):
    variants = spec['variants']
    if (not variants or not isinstance(spec['order'], list) or not spec['order']
            or any(label not in variants for label in spec['order'])):
        raise ValueError('choose a nonempty trial order using declared variants')
    if type(spec['repetitions']) is not int or spec['repetitions'] < 1:
        raise ValueError('repetitions must be a positive integer')
    if not math.isfinite(spec['min_time']) or spec['min_time'] <= 0:
        raise ValueError('min_time must be positive and finite')
    for label, variant in variants.items():
        if not re.fullmatch(r'[a-zA-Z0-9_-]+', label):
            raise ValueError('invalid variant label')
        if not re.fullmatch(r'[0-9a-f]{64}', variant['artifact']['sha256']):
            raise ValueError(f'{label}: invalid artifact identity')
        for name, digest in variant['files_sha256'].items():
            relative = PurePosixPath(name)
            if not relative.parts or relative.is_absolute() or '..' in relative.parts:
                raise ValueError(f'{label}: invalid archive member path: {name}')
            if not re.fullmatch(r'[0-9a-f]{64}', digest):
                raise ValueError(f'{label}: invalid expected hash: {name}')
        for name in (variant['binary'], variant.get('source_receipt', 'job.json')):
            if name not in variant['files_sha256']:
                raise ValueError(f'{label}: binary and source receipt must have expected hashes: {name}')
        expected = variant.get('expected_cases', spec.get('expected_cases'))
        if (not isinstance(expected, list) or not expected
                or any(not isinstance(name, str) or not name for name in expected)
                or len(set(expected)) != len(expected)):
            raise ValueError(f'{label}: provide distinct expected_cases globally or per variant')


def replay(spec_path, output, *, cpu=None):
    spec_bytes = Path(spec_path).read_bytes()
    spec = json.loads(spec_bytes)
    cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0)))) if cpu is None else cpu
    if cpu not in os.sched_getaffinity(0):
        raise ValueError('CPU is outside the allowed affinity mask')
    output = Path(output).expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / 'inputs.json').write_bytes(spec_bytes)
    receipt = {'format': 1, 'scope': 'archived binaries; executable fixture checks, no compilation',
               'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
               'host': {'name': platform.node(), 'platform': platform.platform()},
               'cpu': cpu, 'spec_sha256': hashlib.sha256(spec_bytes).hexdigest(),
               'variants': {}, 'trials': [], 'status': 'running'}
    receipt_path = output / 'replay.json'
    save(receipt_path, receipt)
    try:
        validate(spec)
        for label, variant in spec['variants'].items():
            reference = variant['artifact']
            cached = datasets.cache_root() / 'worker-bundles' / reference['sha256']
            if not cached.exists():
                selection = hashlib.sha256(json.dumps(sorted(variant['files_sha256'])).encode()).hexdigest()
                cached = datasets.cache_root() / 'selected-worker-bundles' / reference['sha256'] / selection
            destination = output / 'binary-sources' / label
            with datasets.locked('replay-' + reference['sha256']):
                if not cached.exists():
                    artifacts.restore_bundle(reference, cached, selected=list(variant['files_sha256']))
                for name, expected in variant['files_sha256'].items():
                    relative = Path(name)
                    if not (cached / relative).is_file():
                        raise ValueError(f'archived input missing: {label}/{name} in {cached}')
                    target = destination / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(cached / relative, target)
                    if datasets.digest(target) != expected:
                        raise ValueError(f'archived input changed: {label}/{name} in {cached}')
            source_receipt = variant.get('source_receipt', 'job.json')
            if 'source_receipt' in variant:
                source_digest = datasets.digest(destination / source_receipt)
            else:
                source_digest = json.loads((destination / source_receipt).read_text())['source']['digest']
            if source_digest != variant['source_digest']:
                raise ValueError(f'{label}: binary source identity differs from its retained source receipt')
            receipt['variants'][label] = {'binary': str((destination / variant['binary']).relative_to(output)),
                                           'sha256': variant['files_sha256'][variant['binary']],
                                           'source_digest': variant['source_digest']}
            save(receipt_path, receipt)
        environment = os.environ | spec.get('environment', {}) | {'SIXDB_CPU': str(cpu)}
        for index, label in enumerate(spec['order'], 1):
            name = f'{index:02d}-{label}'
            samples = output / (name + '.json')
            binary = output / receipt['variants'][label]['binary']
            resources = output / (name + '.resources.json')
            command = ['taskset', '-c', str(cpu), '/usr/bin/time', '-q', '-o', str(resources),
                       '-f', '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"max_rss_kib":%M}',
                       str(binary),
                       '--benchmark_filter=' + spec['filter'],
                       f"--benchmark_min_time={spec['min_time']}s",
                       f"--benchmark_repetitions={spec['repetitions']}",
                       '--benchmark_enable_random_interleaving=false',
                       '--benchmark_out_format=json', '--benchmark_out=' + str(samples)]
            trial = {'name': name, 'variant': label, 'argv': command, 'cwd': str(ROOT)}
            receipt['trials'].append(trial)
            save(receipt_path, receipt)
            print(name, flush=True)
            started = time.monotonic()
            with (output / (name + '.txt')).open('w') as log:
                result = subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
            trial.update(returncode=result.returncode, seconds=time.monotonic() - started)
            if resources.exists():
                trial['resources'] = json.loads(resources.read_text())
            if result.returncode:
                raise RuntimeError(f'{name} exited {result.returncode}; see its log')
            if not samples.is_file():
                raise RuntimeError(f'{name}: benchmark produced no samples file; see its log')
            data = json.loads(samples.read_text())
            rows = data.get('benchmarks', [])
            errors = [f"{row.get('run_name', row.get('name', '?'))}: {row.get('error_message', 'case failed')}"
                      for row in rows if row.get('error_occurred')]
            if errors:
                raise RuntimeError(f"{name}: benchmark case errors: {'; '.join(errors)}")
            raw = [row for row in rows if row.get('run_type') == 'iteration']
            counts = collections.Counter(row['run_name'] for row in raw)
            expected = spec['variants'][label].get('expected_cases', spec.get('expected_cases'))
            missing, extra = sorted(set(expected) - set(counts)), sorted(set(counts) - set(expected))
            wrong = {case: count for case, count in counts.items() if count != spec['repetitions']}
            if missing or extra or wrong:
                raise RuntimeError(f'{name}: unexpected cases/repetitions: missing={missing}, extra={extra}, '
                                   f'counts={wrong} (expected {spec["repetitions"]} each)')
            if any(row['iterations'] <= 0 or not math.isfinite(row['cpu_time']) for row in raw):
                raise RuntimeError(f'{name}: invalid benchmark results')
            trial.update(samples=samples.name, samples_sha256=datasets.digest(samples), cases=len(counts))
            save(receipt_path, receipt)
        receipt['status'] = 'complete'
    except Exception as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        save(receipt_path, receipt)
    print(f'Replay complete: {output}', flush=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('spec', type=Path, help='artifact/member hashes, expected cases, filter and trial order')
    parser.add_argument('--output', type=Path, help='new local output directory; defaults to SIXDB_RESULTS/replay')
    parser.add_argument('--cpu', type=int, help='CPU to pin; defaults to SIXDB_CPU or first allowed CPU')
    args = parser.parse_args()
    output = args.output
    if output is None:
        if not os.environ.get('SIXDB_RESULTS'):
            parser.error('set --output for a local run or SIXDB_RESULTS on a worker')
        output = Path(os.environ['SIXDB_RESULTS']) / 'replay'
    replay(args.spec, output, cpu=args.cpu)


if __name__ == '__main__':
    main()
