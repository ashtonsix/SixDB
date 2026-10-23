#!/usr/bin/env python3
"""Topology-aware read-throughput curves, with exact raw intervals retained."""
from __future__ import annotations
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import subprocess
import sys

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[3]
sys.path.insert(0, str(ROOT / 'workbench/spikes/memory-characterisation'))
from hardware import discover, cpus, read


def byte_size(value):
    value = value.strip().upper()
    for suffix, factor in [('K', 1024), ('M', 1024**2), ('G', 1024**3)]:
        if value.endswith(suffix):
            return int(value[:-1]) * factor
    return int(value)


def topology(allowed):
    """One CPU per physical core, packed by actual reported cache memberships."""
    cores = {}
    for cpu in allowed:
        root = Path(f'/sys/devices/system/cpu/cpu{cpu}')
        siblings = tuple(cpus(read(root / 'topology/thread_siblings_list')) or [cpu])
        package = read(root / 'topology/physical_package_id')
        core = read(root / 'topology/core_id')
        levels = []
        for cache in (root / 'cache').glob('index*'):
            if read(cache / 'type') in ('Data', 'Unified'):
                levels.append(dict(level=int(read(cache / 'level')),
                    bytes=byte_size(read(cache / 'size')),
                    members=cpus(read(cache / 'shared_cpu_list'))))
        last = max(levels, key=lambda x: x['level']) if levels else None
        # Sibling membership is the authoritative core grouping when visible.
        cores.setdefault(siblings, dict(cpu=cpu, siblings=list(siblings), package=package,
            core=core, caches=levels, llc=last,
            numa_nodes=[p.name for p in root.glob('node[0-9]*')]))
    return sorted(cores.values(), key=lambda c: (
        tuple(c['llc']['members']) if c['llc'] else (c['cpu'],), c['cpu']))


def audit_sample(row):
    workers = row['workers']
    assert workers and len({w['cpu'] for w in workers}) == len(workers)
    assert all(w['cpu'] == w['actual_cpu'] and w['end_ns'] > w['start_ns'] for w in workers)
    expected = max(w['end_ns'] for w in workers) - min(w['start_ns'] for w in workers)
    overlap = max(0, min(w['end_ns'] for w in workers) - max(w['start_ns'] for w in workers))
    assert row['wall_ns'] == expected and row['overlap_ns'] == overlap
    assert row['verified'] is True
    if row['protocol'] >= 2:
        units = [w['units'] for w in workers]
        assert all(n > 0 for n in units)
        assert row['total_units'] == sum(units)
        assert row['total_useful_bytes'] == sum(units) * 8
        assert row['total_nominal_line_bytes'] == sum(units) * (row['line'] if row['mode'] == 'chase' else 8)
        for w in workers:
            distinct = min(row['bytes_per_worker'] // row['line'], w['units']) * row['line'] if row['mode'] == 'chase' else (
                min(row['bytes_per_worker'] // 8, w['units']) * 8 + row['line'] - 1) // row['line'] * row['line']
            assert w['distinct_read_line_bytes'] == distinct
        if row['execution'] == 'common-window':
            assert row['units_per_worker'] is None and row['useful_bytes_per_worker'] is None
            assert row['nominal_line_bytes_per_worker'] is None
            assert row['duration_ns'] > 0
            assert all(w['start_ns'] < row['common_deadline_ns'] <= w['end_ns'] for w in workers)
            if row['mode'] == 'chase': assert all(n % row['chains'] == 0 for n in units)
            return
        assert len(set(units)) == 1 and row['units_per_worker'] == units[0]
    assert row['useful_bytes_per_worker'] == row['units_per_worker'] * 8
    if row['mode'] == 'scan':
        assert row['chains'] == 1
        assert row['units_per_worker'] == row['passes'] * row['bytes_per_worker'] // 8
        assert row['nominal_line_bytes_per_worker'] == row['useful_bytes_per_worker']
    else:
        lines = row['bytes_per_worker'] // row['line']
        assert row['units_per_worker'] == row['passes'] * lines + row['chains']
        assert row['nominal_line_bytes_per_worker'] == row['units_per_worker'] * row['line']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--profile', choices=['pilot', 'full'], default='pilot')
    p.add_argument('--cpu-limit', type=int)
    p.add_argument('--counts', help='Explicit prefix counts from packed physical-core order')
    p.add_argument('--sizes', help='Per-worker byte footprints, comma separated')
    p.add_argument('--chains', default='1,4,16,32,64')
    p.add_argument('--trials', type=int, default=4)
    p.add_argument('--ms', type=int, default=20)
    p.add_argument('--seed', type=int, default=1729)
    p.add_argument('--steady', action='store_true', help='Every worker reads through one common-duration window')
    args = p.parse_args()
    allowed = sorted(os.sched_getaffinity(0))
    physical = topology(allowed)
    limit = args.cpu_limit or (min(8, len(physical)) if args.profile == 'pilot' else len(physical))
    if not 1 <= limit <= len(physical):
        p.error('cpu-limit must fit the available physical cores')
    selected = physical[:limit]
    default_counts = {1, limit}
    default_counts.update(2**k for k in range(limit.bit_length()) if 2**k <= limit)
    domains = {}
    for core in selected:
        key = tuple(core['llc']['members']) if core['llc'] else (core['cpu'],)
        domains.setdefault(key, []).append(core['cpu'])
    default_counts.add(len(next(iter(domains.values()))))
    first_package = selected[0]['package']
    package_prefix = list(itertools.takewhile(lambda c: c['package'] == first_package, selected))
    if first_package is not None and package_prefix:
        default_counts.add(len(package_prefix))
    counts = sorted(set(map(int, args.counts.split(','))) if args.counts else default_counts)
    if not counts or min(counts) < 1 or max(counts) > limit:
        p.error('counts must fit the selected physical cores')
    default_sizes = '8192,262144,4194304,134217728' if args.profile == 'pilot' else '8192,131072,1048576,2097152,8388608,268435456'
    sizes = sorted(set(byte_size(v) for v in (args.sizes or default_sizes).split(',')))
    chains = sorted(set(map(int, args.chains.split(','))))
    if not chains or not set(chains) <= {1, 4, 16, 32, 64}:
        p.error('unsupported chain width')
    page = os.sysconf('SC_PAGE_SIZE')
    hardware = discover(selected[0]['cpu'])
    line = hardware['line_bytes']
    if not line or any(size < page or size % page or (size // line) % max(chains) for size in sizes):
        p.error('footprints need complete pages and divisible cycle lengths')
    meminfo = dict(row.split(':', 1) for row in Path('/proc/meminfo').read_text().splitlines())
    available = int(meminfo['MemAvailable'].split()[0]) * 1024
    # Body plus retained permutation and checks, with room for the worker/runtime.
    if max(sizes) * max(counts) * 1.2 > available * 0.5:
        p.error('requested mappings exceed half of current MemAvailable')
    args.output.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    before = hashlib.sha256(binary.read_bytes()).hexdigest()
    settings = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
    identity = dict(protocol=1, config=settings, hardware=hardware, physical_cores=physical,
        selected_cores=selected, allowed_cpus=allowed, domain_members=[list(k) for k in domains],
        counts=counts, sizes=sizes, chains=chains, binary_sha256=before,
        conditioning='Each worker immediately repeats its full read before the timed interval.',
        traffic='Useful and nominal-line rates only; no measured DRAM traffic or serving-tier assertion.',
        placement='Private guarded mapping, first-touched on its pinned worker; actual smaps/numa_maps retained.')
    (args.output / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    cells = list(itertools.product(counts, sizes, [('scan', 1)] + [('chase', n) for n in chains]))
    random.Random(args.seed).shuffle(cells)
    manifest = []
    with (args.output / 'samples.jsonl').open('w') as samples, (args.output / 'mappings.jsonl').open('w') as mappings:
        for index, (count, size, (mode, width)) in enumerate(cells):
            cpuset = [c['cpu'] for c in selected[:count]]
            command = [str(binary), '--cpus', ','.join(map(str, cpuset)), '--bytes', str(size),
                '--line', str(line), '--mode', mode, '--chains', str(width),
                '--trials', str(args.trials), '--ms', str(args.ms), '--seed', str(args.seed)]
            if args.steady: command += ['--steady']
            result = subprocess.run(command, capture_output=True, text=True, timeout=300)
            (args.output / f'cell-{index:04d}.stderr').write_text(result.stderr)
            records = []
            for text in result.stdout.splitlines():
                row = json.loads(text)
                row['cell'] = index
                if row['type'] == 'sample':
                    audit_sample(row)
                    assert [w['cpu'] for w in row['workers']] == cpuset
                    samples.write(json.dumps(row) + '\n')
                    records.append(row)
                elif row['type'] == 'mapping':
                    mappings.write(json.dumps(row) + '\n')
                else:
                    raise AssertionError(f'Unknown record: {row}')
            samples.flush(); mappings.flush()
            manifest.append(dict(cell=index, cpus=cpuset, bytes_per_worker=size, mode=mode,
                chains=width, command=command, returncode=result.returncode, samples=len(records)))
            (args.output / 'commands.json').write_text(json.dumps(manifest, indent=2) + '\n')
            if result.returncode:
                raise RuntimeError(f'cell {index} failed: {result.stderr}')
            assert len(records) == args.trials * 2
            for trial in range(args.trials):
                arms = [r['arm'] for r in records if r['trial'] == trial]
                assert arms == (['primary', 'duplicate'] if trial % 2 == 0 else ['duplicate', 'primary'])
            print(json.dumps(dict(cell=index, total=len(cells), cpus=count, size=size, mode=mode,
                chains=width, state='complete')), flush=True)
    after = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert before == after
    (args.output / 'complete.json').write_text(json.dumps(dict(cells=len(cells),
        samples=len(cells) * args.trials * 2, binary_sha256_after=after, all_answers_verified=True), indent=2) + '\n')


if __name__ == '__main__':
    main()
