#!/usr/bin/env python3
"""Validate a complete immutable Ring panel and retain every scheduling arm."""
import argparse
import csv
import gzip
import json
import math
import re
import statistics
from pathlib import Path

ARMS = ('abseil', 'abseil_prefetch', 'direct', 'manual', 'manual_hints', 'ring', 'ring_hints')
CASES = {'check': (300, ('forced_chain',)), 'hot': (1800, ('compact', 'fuller')),
         'llc': (110000, ('compact', 'fuller')), 'large': (3600000, ('compact', 'fuller')),
         'dram-check': (301, ('forced_chain',)), 'dram': (57600000, ('compact', 'fuller'))}
CONTEXT = ('policy_revision', 'line_bytes', 'page_bytes', 'hint', 'requested_bytes',
           'owned_capacity_bytes', 'owner_metadata_bytes', 'abseil_requested_bytes',
           'ring_storage_bytes', 'job_storage_bytes', 'configure_ns')
GEOMETRY = ('a', 'f', 'c', 'unit', 'slots', 'fingerprints', 'suffixes', 'values',
            'suffix_bytes', 'value_bytes')


def residence_receipt(root, stem):
    assert Path(stem).name == stem, 'receipt stem must not escape residence directory'
    files = {name: root / f'{stem}.{name}' for name in ('smaps', 'smaps_rollup', 'numa_maps', 'status', 'owners.tsv')}
    files = {name: p if p.is_file() else Path(str(p) + '.gz') for name, p in files.items()}
    assert all(p.is_file() and p.stat().st_size for p in files.values()), files
    def read(name):
        path = files[name]
        return gzip.open(path, 'rt').read() if path.suffix == '.gz' else path.read_text()
    totals = {}
    for line in read('smaps_rollup').splitlines():
        field, *value = line.split()
        if len(value) == 2 and value[1] == 'kB':
            totals[field.rstrip(':') + '_bytes'] = int(value[0]) * 1024
    assert 'Rss_bytes' in totals
    owners = list(csv.DictReader(read('owners.tsv').splitlines(), delimiter='\t'))
    candidate = [r for r in owners if r['kind'] == 'candidate']
    assert len(candidate) >= 2
    for r in owners:
        assert int(r['address'], 16) >= 0 and int(r['initialized_bytes']) >= 0
    assert len([r for r in owners if r['kind'] == 'abseil_entry_anchor']) == 1
    return dict(files={k: str(v) for k, v in files.items()}, process_smaps_rollup=totals,
        numa_maps_available=not read('numa_maps').startswith('unavailable:'),
        candidate_requested_bytes=sum(int(r['initialized_bytes']) for r in candidate),
        candidate_accessible_capacity_bytes=sum(int(r['accessible_capacity_bytes']) for r in candidate),
        scope='Live process snapshot includes both contenders, allocator inventory and runtime. '
              'The public Abseil entry pointer is a mapping anchor, not an allocation bound. '
              'smaps/NUMA and requested bytes do not establish request service level.')


def summarize(path, mode, residence_root=None):
    n, layouts = CASES[mode]
    timing, contexts, populations, geometries, workloads, stages, closed = {}, {}, {}, {}, {}, {}, {}
    generators, residence = {}, {}
    for row in csv.reader(path.read_text().splitlines()):
        assert row, 'empty record'
        kind, layout, count, *rest = row
        assert layout in layouts and int(count) == n, row
        if kind == 'generator':
            assert mode in ('dram', 'dram-check') and len(rest) == 7
            mixed, method, seed, low, high, checksum, xor_keys = rest
            key = (layout, int(mixed)); assert key not in generators and key[1] in (0, 1)
            assert method == 'bijective39' and int(seed) == 0x49d375a621
            assert 0 <= int(low) <= int(high) <= n
            if mode == 'dram':
                assert int(low) * 256 > n * 0.9 and int(high) * 256 < n * 1.1
            generators[key] = dict(method=method, seed=int(seed), high_byte_min=int(low), high_byte_max=int(high),
                                   sum=int(checksum), xor=int(xor_keys), retained_key_bytes=0)
            continue
        if kind == 'residence':
            assert mode in ('dram', 'dram-check') and len(rest) == 3
            mixed, phase, stem = rest;key = (layout, int(mixed), phase)
            assert key not in residence and key[1] in (0, 1) and phase in ('before', 'after')
            residence[key] = residence_receipt(residence_root or path.parent / 'residence', stem)
            continue
        if kind in ('population', 'geometry'):
            mixed = int(rest[0]); key = (layout, mixed)
            assert mixed in (0, 1)
            if kind == 'population':
                assert key not in populations and len(rest) == 4
                populations[key] = tuple(map(int, rest[1:]))
                assert sum(populations[key]) == n
            else:
                tier = rest[1]; key += (tier,)
                assert tier in ('primary', 'secondary') and key not in geometries and len(rest) == 12
                geometries[key] = dict(zip(GEOMETRY, map(int, rest[2:])))
                g = geometries[key]
                assert g['a'] + g['f'] + g['c'] == 40 and 0 < g['slots'] <= 32
                assert g['values'] + g['slots'] * g['value_bytes'] <= g['unit']
            continue
        concurrency, mixed = map(int, rest[:2]); key = (layout, concurrency, mixed)
        assert concurrency in (1, 8, 32) and mixed in (0, 1)
        values = rest[2:]
        if kind == 'context':
            assert key not in contexts and len(values) == len(CONTEXT)
            contexts[key] = dict(zip(CONTEXT, [int(x) for x in values[:-1]] + [float(values[-1])]))
            assert all(v >= 0 for v in contexts[key].values())
        elif kind == 'workload':
            assert key not in workloads and len(values) == 4
            requests, hits, misses, graphs = map(int, values)
            assert graphs > 0 and requests == hits + misses and hits == n * graphs
            assert (misses == 0) if not mixed else (0 < misses <= hits)
            workloads[key] = dict(requests=requests, hits=hits, misses=misses, graphs=graphs)
        elif kind == 'close':
            assert key not in closed and len(values) == 1 and float(values[0]) >= 0
            closed[key] = float(values[0])
        elif kind == 'time':
            rep, arm, requests, ns, *visits = values
            assert arm in ARMS and len(visits) == 4
            tkey = key + (int(rep), arm)
            assert tkey not in timing and math.isfinite(float(ns)) and float(ns) > 0
            assert int(requests) == workloads[key]['requests']
            logical = tuple(map(int, visits))
            assert logical[0] == int(requests) and 0 <= logical[1] <= logical[0]
            assert logical[2] == logical[3] and logical[2] >= 0
            assert key not in stages or stages[key] == logical
            stages[key] = logical; timing[tkey] = float(ns)
        else:
            raise AssertionError(('unknown record', row))
    keys = {(l, c, m) for l in layouts for c in (1, 8, 32) for m in (0, 1)}
    assert contexts.keys() == workloads.keys() == closed.keys() == stages.keys() == keys
    assert populations.keys() == {(l, m) for l in layouts for m in (0, 1)}
    assert geometries.keys() == {(l, m, t) for l in layouts for m in (0, 1) for t in ('primary', 'secondary')}
    if mode in ('dram', 'dram-check'):
        assert generators.keys() == {(l, m) for l in layouts for m in (0, 1)}
        assert len({json.dumps(v, sort_keys=True) for v in generators.values()}) == 1, 'same input across arms'
    else:
        assert not generators
    if mode in ('dram', 'dram-check'):
        assert residence.keys() == {(l, m, p) for l in layouts for m in (0, 1) for p in ('before', 'after')}
        for l, m, phase in residence:
            receipt = residence[l, m, phase]; context = contexts[l, 1, m]
            assert receipt['candidate_requested_bytes'] == context['requested_bytes']
            assert receipt['candidate_accessible_capacity_bytes'] == context['owned_capacity_bytes']
            if mode == 'dram':
                assert context['requested_bytes'] > 480 * 1024**2
                p, q = geometries[l, m, 'primary'], geometries[l, m, 'secondary']
                assert p['a'] == 23 and p['unit'] == (64 if l == 'compact' else 128)
                assert q['a'] == (22 if l == 'compact' else 19) and q['unit'] == 128
    else:
        assert not residence
    reps = sorted({k[3] for k in timing})
    assert reps and reps == list(range(len(reps)))
    assert timing.keys() == {k + (r, a) for k in keys for r in reps for a in ARMS}
    host_evidence = None
    if mode == 'dram':
        hardware_path, pmu_path = path.parent / 'hardware.json', path.parent / 'pmu/probe.json'
        hardware, pmu = json.loads(hardware_path.read_text()), json.loads(pmu_path.read_text())
        caches = []
        for cache in hardware['caches']:
            size = re.fullmatch(r'(\d+)([KMG]?)', cache['size'] or '')
            if size and cache['type'] in ('Data', 'Unified'):
                caches.append((int(cache['level']), int(size[1]) * 1024 ** ('KMG'.find(size[2]) + 1 if size[2] else 0)))
        assert caches, 'reported cache geometry required for above-LLC qualification'
        last_level = max(level for level, size in caches)
        llc_bytes = max(size for level, size in caches if level == last_level)
        for l in layouts:
            primary = geometries[l, 0, 'primary']
            assert (1 << primary['a']) * primary['unit'] > llc_bytes, 'primary does not exceed reported last-level cache'
        host_evidence = dict(hardware=str(hardware_path), pmu_availability=str(pmu_path),
            architecture=hardware['recognised_architecture'], reported_last_level_bytes=llc_bytes,
            pmu_statuses=[dict(identity=p['identity'], status=p['status']) for p in pmu['probes']],
            scope='PMU probe checks access only. No workload counters are measured by this driver; '
                  'above reported LLC capacity does not prove the fraction of requests served by DRAM.')
    summaries = []
    for key in sorted(keys):
        l, c, m = key
        for arm in ARMS:
            samples = [timing[key + (r, arm)] for r in reps]
            # Paired within the same repetition; best ordinary baseline per rep.
            ratios = [min(timing[key + (r, a)] for a in ARMS[:2]) / timing[key + (r, arm)] for r in reps]
            summaries.append(dict(layout=l, n=n, concurrency=c, mixed=bool(m), arm=arm,
                repetitions=len(reps), ns=statistics.median(samples), min_ns=min(samples), max_ns=max(samples),
                best_abseil_over_candidate=statistics.median(ratios)))
    accounting = [dict(layout=l, n=n, concurrency=c, mixed=bool(m), **contexts[l, c, m],
        **workloads[l, c, m], close_ns=closed[l, c, m],
        population=dict(zip(('primary', 'secondary', 'chain'), populations[l, m])),
        logical_candidate_visits=dict(zip(('primary', 'secondary', 'chain_directory', 'chain_payload'), stages[l, c, m])),
        geometry={tier: geometries[l, m, tier] for tier in ('primary', 'secondary')}) for l, c, m in sorted(keys)]
    return dict(input=str(path), mode=mode, timing_rows=len(timing), timings=summaries, accounting=accounting,
        generators=[dict(layout=l, mixed=bool(m), **g) for (l, m), g in sorted(generators.items())],
        residence=[dict(layout=l, mixed=bool(m), phase=p, **v) for (l, m, p), v in sorted(residence.items())],
        host_evidence=host_evidence,
        scope='Fresh immutable answer-dependent graphs; mixed uses guaranteed-absent high-bit mates. '
              'Logical candidate visits are not baseline visits, prefetch count, memory traffic, or service-level evidence. '
              'Case names do not prove cache residence. Owned capacity is not RSS.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--mode', required=True, choices=CASES)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--residence-root', type=Path)
    args = parser.parse_args()
    result = summarize(args.csv, args.mode, args.residence_root)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    for l in CASES[args.mode][1]:
        for m in (False, True):
            for c in (1, 8, 32):
                cells = {x['arm']: x['ns'] for x in result['timings']
                         if (x['layout'], x['mixed'], x['concurrency']) == (l, m, c)}
                print(f'{l:12s} mixed={int(m)} C={c:2d}: ' + ' '.join(f'{a} {cells[a]:.3f}' for a in ARMS))
    print(f"Validated {result['timing_rows']} timing rows with geometry, ownership and graph accounting")
