#!/usr/bin/env python3
"""Small, non-destructive filesystem and transport diagnostics; no raw device writes."""
from __future__ import annotations
import concurrent.futures
import json
import mmap
import os
from pathlib import Path
import random
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def distribution(values):
    values = sorted(values)
    return {'n': len(values), 'p10_ns': values[int(.1 * (len(values) - 1))],
            'median_ns': statistics.median(values), 'p90_ns': values[int(.9 * (len(values) - 1))],
            'max_ns': max(values)}


def discovery():
    devices = {}
    for root in sorted(Path('/sys/block').glob('*')):
        if root.name.startswith(('loop', 'ram', 'zram')):
            continue
        devices[root.name] = {key: read(root / key) for key in ['size', 'device/model', 'device/numa_node',
            'queue/logical_block_size', 'queue/physical_block_size', 'queue/rotational',
            'queue/scheduler', 'queue/read_ahead_kb', 'queue/nr_requests', 'queue/max_sectors_kb']}
    numa = {root.name: {key: read(root / key) for key in ['cpulist', 'distance', 'meminfo']}
            for root in sorted(Path('/sys/devices/system/node').glob('node[0-9]*'))}
    network = {root.name: {key: read(root / key) for key in ['mtu', 'speed', 'duplex', 'device/numa_node']}
               for root in sorted(Path('/sys/class/net').glob('*'))}
    return {'block_devices': devices, 'numa': numa, 'network_interfaces': network,
            'mountinfo': read('/proc/self/mountinfo'), 'meminfo': read('/proc/meminfo')}


def storage(parent):
    result = {'directory': str(parent), 'file_bytes': 64 * 1024**2,
              'scope': 'temporary file on output filesystem; no raw-device access', 'measurements': {}}
    rng = random.Random(5349)
    with tempfile.TemporaryDirectory(prefix='memory-characterisation-', dir=parent) as directory:
        path = Path(directory) / 'io.bin'
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
        try:
            payload = os.urandom(1024**2)
            for _ in range(64):
                if os.write(fd, payload) != len(payload):
                    raise OSError('short initialization write')
            os.fdatasync(fd)
            os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
            for case in ['buffered-after-dontneed', 'buffered-warm']:
                start = time.perf_counter_ns()
                total = 0
                for offset in range(0, result['file_bytes'], len(payload)):
                    total += len(os.pread(fd, len(payload), offset))
                elapsed = time.perf_counter_ns() - start
                result['measurements'][case] = {'bytes': total, 'elapsed_ns': elapsed,
                    'MiB_per_second': total * 1e9 / elapsed / 2**20}
            times = []
            for _ in range(32):
                offset = rng.randrange(result['file_bytes'] // 4096) * 4096
                start = time.perf_counter_ns()
                if os.pwrite(fd, payload[:4096], offset) != 4096:
                    raise OSError('short durable write')
                os.fdatasync(fd)
                times.append(time.perf_counter_ns() - start)
            result['measurements']['4k-buffered-write-fdatasync'] = distribution(times)
            try:
                direct = os.open(path, os.O_RDONLY | os.O_DIRECT)
                try:
                    def batch(offsets):
                        with mmap.mmap(-1, 4096) as buffer:
                            measured = []
                            for offset in offsets:
                                start = time.perf_counter_ns()
                                n = os.preadv(direct, [buffer], offset)
                                measured.append(time.perf_counter_ns() - start)
                                if n != 4096:
                                    raise OSError('short direct read')
                            return measured
                    offsets = [rng.randrange(result['file_bytes'] // 4096) * 4096 for _ in range(128)]
                    result['measurements']['4k-direct-random-read'] = distribution(batch(offsets))
                    for depth in [1, 2, 4, 8, 16]:
                        with concurrent.futures.ThreadPoolExecutor(max_workers=depth) as pool:
                            # Python thread/dispatch overhead is deliberately charged.
                            start = time.perf_counter_ns()
                            lists = list(pool.map(batch, [offsets for _ in range(depth)]))
                            elapsed = time.perf_counter_ns() - start
                        result['measurements'][f'4k-direct-app-concurrency-{depth}'] = {
                            'operations': 128 * depth, 'elapsed_ns': elapsed,
                            'IOPS': 128 * depth * 1e9 / elapsed,
                            'service': distribution([v for values in lists for v in values])}
                finally:
                    os.close(direct)
            except OSError as exc:
                result['direct_io_error'] = str(exc)
        finally:
            os.close(fd)
    return result


def network():
    result = {'loopback': {}, 'regional_https': []}
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    def echo():
        connection, _ = listener.accept()
        with connection:
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            while payload := connection.recv(1024**2):
                connection.sendall(payload)
    thread = threading.Thread(target=echo, daemon=True)
    thread.start()
    with socket.create_connection(listener.getsockname(), timeout=5) as connection:
        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        for size, repeats in [(1, 200), (64, 200), (4096, 100), (1024**2, 8)]:
            payload = b'x' * size
            times = []
            for _ in range(repeats):
                start = time.perf_counter_ns()
                connection.sendall(payload)
                remaining = size
                while remaining:
                    response = connection.recv(remaining)
                    if not response:
                        raise OSError('echo closed early')
                    remaining -= len(response)
                times.append(time.perf_counter_ns() - start)
            result['loopback'][str(size)] = distribution(times)
    thread.join(timeout=5)
    listener.close()
    if shutil.which('curl'):
        for _ in range(3):
            completed = subprocess.run(['curl', '--silent', '--show-error', '--head', '--output', '/dev/null',
                '--connect-timeout', '3', '--max-time', '5', '--write-out', '%{json}',
                'https://s3.us-east-1.amazonaws.com/'], capture_output=True, text=True)
            try:
                item = json.loads(completed.stdout)
                result['regional_https'].append({key: item.get(key) for key in
                    ['http_code', 'remote_ip', 'time_namelookup', 'time_connect', 'time_appconnect',
                     'time_starttransfer', 'time_total', 'exitcode']})
            except json.JSONDecodeError:
                result['regional_https'].append({'error': completed.stderr.strip()})
    result['scope'] = 'TCP loopback echo RTT and fresh HTTPS HEAD connections; no peer bandwidth or network tail claims'
    return result


if __name__ == '__main__':
    output = Path(sys.argv[1])
    start = time.monotonic()
    result = {'discovery': discovery()}
    storage_root = Path(sys.argv[2]) if len(sys.argv) > 2 else output
    for name, probe in [('storage', lambda: storage(storage_root)), ('network', network)]:
        try:
            result[name] = probe()
        except Exception as exc:
            result[name] = {'error': str(exc)}
    result['elapsed_seconds'] = time.monotonic() - start
    (output / 'system.json').write_text(json.dumps(result, indent=2) + '\n')
