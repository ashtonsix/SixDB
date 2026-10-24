#!/usr/bin/env python3
"""Same-query working-set panels; audit raw schedules, answers and intervals."""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import statistics
import struct
import subprocess
import sys

STUDY = Path(__file__).resolve().parent
sys.path.insert(0, str(STUDY.parent))
from run import topology
from hardware import discover


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def logical(path, ids):
    values = []
    with Path(path).open('rb') as f:
        for block in ids:
            f.seek(block * 2048)
            values.extend(struct.unpack('<256Q', f.read(2048)))
    return values


def audit(records):
    config, = [r for r in records if r['event'] == 'config']
    allocation, = [r for r in records if r['event'] == 'allocation']
    maps, = [r for r in records if r['event'] == 'maps']
    assert maps['smaps']
    if maps['numa_maps_available']:
        assert maps['numa_maps']
    else:
        assert not maps['numa_maps']
    samples = [r for r in records if r['event'] == 'sample']
    assert len(records) == 3 + len(samples) and len(samples) == config['trials'] * 6
    assert len(set(config['source_block_ids'])) == len(config['source_block_ids'])
    a, b = [logical(config[k], config['source_block_ids']) for k in ('a', 'b')]
    assert len(a) == len(b) == config['rows_per_query']
    def coarse(block):
        return config['coarse'] == 'all' or (config['coarse'] == 'even' and block % 2 == 0) or (
            config['coarse'] == 'cluster' and block < (len(a) // 256 + 1) // 2)
    chosen = [v for i, v in enumerate(b) if coarse(i // 256) and (
        config['operation'] == 'sum' or a[i] >= config['threshold'])]
    expected_sum, expected_count = sum(chosen), len(chosen)
    assert int(config['expected_sum']) == expected_sum and config['expected_count'] == expected_count
    cpus = [w['cpu'] for w in allocation['workers']]
    assert len(set(cpus)) == len(cpus)
    across_workers = []
    for worker in allocation['workers']:
        assert sorted(worker['pool_order']) == list(range(config['pool']))
        assert worker['pool_order'] == allocation['workers'][0]['pool_order']
        for arm in worker['arms']:
            assert sum(r[1] for r in arm['ranges']) == arm['logical_range_bytes']
            assert sum(r[2] for r in arm['ranges']) == arm['capacity_range_bytes']
            spans = sorted((r[0], r[0] + r[1]) for r in arm['ranges'])
            assert all(0 < size <= capacity for _, size, capacity, _ in arm['ranges'])
            assert all(x[1] <= y[0] for x, y in zip(spans, spans[1:]))
        # Frame segment/index share ranges within a worker, but no worker's
        # actual data range may alias another worker's range.
        spans = {tuple((r[0], r[0] + r[1])) for arm in worker['arms'] for r in arm['ranges']}
        across_workers.extend((start, end, worker['cpu']) for start, end in spans)
    across_workers.sort()
    active = []
    for start, end, cpu in across_workers:
        active = [(e, owner) for e, owner in active if e > start]
        assert all(owner == cpu for _, owner in active)
        active.append((end, cpu))
    positions, predecessors = Counter(), Counter()
    for trial in range(config['trials']):
        sequence = [r for r in samples if r['trial'] == trial]
        assert [r['position'] for r in sequence] == list(range(6))
        assert sorted(r['treatment'] for r in sequence) == list(range(6))
        for row in sequence:
            positions[row['treatment'], row['position']] += 1
        for left, right in zip(sequence, sequence[1:]):
            predecessors[left['treatment'], right['treatment']] += 1
    assert len(positions) == 36 and set(positions.values()) == {config['trials'] // 6}
    assert len(predecessors) == 30 and set(predecessors.values()) == {config['trials'] // 6}
    for row in samples:
        assert row['verified'] and row['duplicate'] == row['treatment'] // 3
        assert [w['cpu'] for w in row['workers']] == cpus
        workers = row['workers']
        assert row['queries'] == sum(w['queries'] for w in workers)
        assert row['wall_ns'] == max(w['end_ns'] for w in workers) - min(w['start_ns'] for w in workers)
        assert row['overlap_ns'] == max(0, min(w['end_ns'] for w in workers) - max(w['start_ns'] for w in workers))
        for w in workers:
            assert w['actual_cpu'] == w['cpu'] and w['end_ns'] > w['start_ns'] and w['queries'] > 0
            assert w['mismatch'] == 0
            assert w['sum_lo'] + (w['sum_hi'] << 64) == expected_sum * w['queries'] % (1 << 128)
            assert w['count'] == expected_count * w['queries']
            assert w['distinct_replicas'] == min(config['pool'], w['queries'])
            if config['execution'] == 'common-window':
                # A delayed VM worker may start after the intended deadline.
                # Retain it as a failed-overlap observation, not a ceiling.
                assert row['deadline_ns'] <= w['end_ns']
                assert w['queries'] % config['quantum'] == 0
            else:
                assert row['deadline_ns'] is None and w['queries'] == config['pool']
    return config, allocation, samples


def summarize(config, allocation, samples):
    groups = defaultdict(list)
    for row in samples:
        groups[row['arm'], row['duplicate']].append(row['wall_ns'] / row['queries'])
    arms = []
    for arm in allocation['workers'][0]['arms']:
        primary, duplicate = (statistics.median(groups[arm['name'], d]) for d in (0, 1))
        arms.append(dict(name=arm['name'], ns_per_query=primary, duplicate_ns=duplicate,
            primary_over_duplicate=primary / duplicate, logical_bytes_per_worker=arm['logical_range_bytes'],
            capacity_bytes_per_worker=arm['capacity_range_bytes']))
    return dict(operation=config['operation'], execution=config['execution'], condition=config['condition'],
        rows_per_query=config['rows_per_query'], pool=config['pool'], workers=len(allocation['workers']),
        a=Path(config['a']).stem, b=Path(config['b']).stem, coarse=config['coarse'],
        min_overlap_fraction=min(r['overlap_ns'] / r['wall_ns'] for r in samples),
        late_start_samples=sum(any(w['start_ns'] >= r['deadline_ns'] for w in r['workers'])
            for r in samples if r['deadline_ns'] is not None),
        min_batch_ns=min(r['wall_ns'] for r in samples), arms=arms)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cases', default='sum:OS,sum:UserID,pair:UserID:URLHash',
        help='operation:A[:B]; SUM uses A as its payload')
    p.add_argument('--counts', default='1')
    p.add_argument('--cpu-set', help='Explicit available physical CPU IDs, in prefix order')
    p.add_argument('--pools', default='1,16,512')
    p.add_argument('--blocks', type=int, default=8)
    p.add_argument('--ms', type=int, default=10)
    p.add_argument('--trials', type=int, default=6)
    p.add_argument('--conditions', default='warm-repeat,flushed-first-pass')
    p.add_argument('--coarse', default='all')
    p.add_argument('--seed', type=int, default=1729)
    args = p.parse_args()
    if args.trials <= 0 or args.trials % 6:
        p.error('trials must be a positive multiple of six')
    physical = topology(sorted(os.sched_getaffinity(0)))
    available = {c['cpu']: c for c in physical}
    selected = [available[int(c)] for c in args.cpu_set.split(',')] if args.cpu_set else physical
    counts = sorted(set(map(int, args.counts.split(','))))
    pools = sorted(set(map(int, args.pools.split(','))))
    if not counts or min(counts) < 1 or max(counts) > len(selected) or not pools or min(pools) < 1:
        p.error('invalid counts/pools')
    cells = []
    inputs = set()
    for spec in args.cases.split(','):
        parts = spec.split(':')
        if len(parts) == 2 and parts[0] == 'sum': op, a = parts; b = a
        elif len(parts) == 3 and parts[0] == 'pair': op, a, b = parts
        else: p.error('case needs sum:B or pair:A:B')
        a_path, b_path = (args.inputs.resolve() / f'{name}.u64' for name in (a, b))
        inputs.update((a_path, b_path))
        cells.extend(itertools.product([(op, a_path, b_path)], counts, pools, args.conditions.split(',')))
    random.Random(args.seed).shuffle(cells)
    args.output.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    before = sha(binary)
    identity = dict(protocol=1, binary_sha256=before, input_sha256={str(path): sha(path) for path in sorted(inputs)},
        hardware=discover(selected[0]['cpu']), physical_cores=physical, selected_cores=selected,
        config={k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
        scope='Same-query immutable replicas. No measured bus traffic or inferred serving-cache tier.')
    (args.output / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    manifest, summaries = [], []
    for index, ((op, a, b), count, pool, condition) in enumerate(cells):
        command = [str(binary), '--a', str(a), '--b', str(b), '--operation', op,
            '--cpus', ','.join(str(c['cpu']) for c in selected[:count]), '--pool', str(pool),
            '--cell-blocks', str(args.blocks), '--ms', str(args.ms), '--trials', str(args.trials),
            '--condition', condition, '--coarse', args.coarse, '--visit', 'shuffled', '--seed', str(args.seed)]
        raw = args.output / f'cell-{index:03}.jsonl'
        stderr = raw.with_suffix('.stderr')
        with raw.open('w') as out, stderr.open('w') as err:
            result = subprocess.run(command, stdout=out, stderr=err, timeout=180)
        manifest.append(dict(cell=index, command=command, returncode=result.returncode, raw=raw.name))
        (args.output / 'commands.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(f'cell {index} failed: {stderr.read_text()}')
        records = [json.loads(line) for line in raw.read_text().splitlines()]
        config, allocation, samples = audit(records)
        summary = summarize(config, allocation, samples)
        summary['cell'] = index
        summaries.append(summary)
        print(json.dumps(summary), flush=True)
    assert sha(binary) == before
    assert {str(path): sha(path) for path in sorted(inputs)} == identity['input_sha256']
    (args.output / 'summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
    (args.output / 'complete.json').write_text(json.dumps(dict(cells=len(cells), all_answers_verified=True,
        schedule_and_allocations_audited=True, binary_sha256_after=sha(binary)), indent=2) + '\n')


if __name__ == '__main__':
    main()
