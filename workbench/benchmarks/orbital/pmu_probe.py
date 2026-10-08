#!/usr/bin/env python3
"""Record actual guest PMU availability before a counter campaign."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError as error:
        return {'error': str(error)}


def catalog(cpu):
    devices = {}
    for device in sorted(Path('/sys/bus/event_source/devices').glob('*')):
        entry = {name: read(device / name) for name in ['type', 'cpumask', 'cpus']
                 if (device / name).exists()}
        for folder in ['format', 'events', 'caps']:
            entry[folder] = {p.name: read(p) for p in sorted((device / folder).glob('*'))
                             if p.is_file()}
        devices[device.name] = entry
    cache_root = Path(f'/sys/devices/system/cpu/cpu{cpu}/cache')
    return {'kernel': platform.release(), 'uid': os.geteuid(), 'cpu': cpu,
            'allowed_cpus': sorted(os.sched_getaffinity(0)),
            'perf_event_paranoid': read('/proc/sys/kernel/perf_event_paranoid'),
            'cpuinfo': read('/proc/cpuinfo'),
            'cache': [{p.name: read(p) for p in sorted(index.glob('*')) if p.is_file()}
                      for index in sorted(cache_root.glob('index*'))],
            'event_sources': devices}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=int(os.environ.get('SIXDB_CPU', -1)))
    parser.add_argument('--event', action='append', default=[], help='name:type:config; probe each separately')
    parser.add_argument('--compare-root', action='store_true', help='also try sudo -n; no policy changes')
    args = parser.parse_args()
    if args.cpu not in os.sched_getaffinity(0):
        parser.error('choose an allowed CPU with --cpu or SIXDB_CPU')
    args.binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=False)
    measured = args.output.resolve() / 'orbital_pmu_probe'
    shutil.copy2(args.binary, measured)
    manifest = {'schema': 1, 'scope': 'availability only; not workload IPC or data residency',
                'binary_sha256': hashlib.sha256(measured.read_bytes()).hexdigest(),
                'binary': measured.name,
                'catalog': 'catalog.json', 'probes': []}
    (args.output / 'catalog.json').write_text(json.dumps(catalog(args.cpu), indent=2) + '\n')
    requests = [('off', []), ('basic', []), ('stalls', [])]
    requests += [('basic', ['--pmu-event', event]) for event in args.event]
    for identity in (['user', 'root'] if args.compare_root else ['user']):
        for index, (mode, extra) in enumerate(requests):
            command = (['sudo', '-n'] if identity == 'root' else []) + [
                'taskset', '-c', str(args.cpu), str(measured), '--pmu', mode,
                '--iterations', '1000000', *extra]
            result = subprocess.run(command, text=True, capture_output=True, timeout=20)
            name = f'{identity}-{index}-{mode}'
            (args.output / f'{name}.stdout').write_text(result.stdout)
            (args.output / f'{name}.stderr').write_text(result.stderr)
            item = {'identity': identity, 'command': command, 'exit_code': result.returncode,
                    'stdout': f'{name}.stdout', 'stderr': f'{name}.stderr'}
            if result.returncode == 0:
                receipt = json.loads(result.stdout)
                item['status'] = receipt['measurement']['status']
                item['receipt'] = f'{name}.json'
                (args.output / item['receipt']).write_text(json.dumps(receipt, indent=2) + '\n')
            else:
                item['status'] = 'process_error'
            manifest['probes'].append(item)
            (args.output / 'probe.json').write_text(json.dumps(manifest, indent=2) + '\n')
            print(f'{name}: {item["status"]}', flush=True)


if __name__ == '__main__':
    main()
