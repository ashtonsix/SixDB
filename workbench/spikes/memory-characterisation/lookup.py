#!/usr/bin/env python3
"""Build a qualified lookup from retained evidence; provide the cheap match operation."""
from __future__ import annotations
import hashlib
import csv
import json
import statistics as stats
from collections import defaultdict
from pathlib import Path
from analyze import analyse

STUDY = Path(__file__).resolve().parent
FORMAT = 1


def match(hardware, database):
    if database.get('format') != FORMAT:
        return None
    entry = database.get('entries', {}).get(hardware['lookup_key'])
    if not entry or entry.get('status') != 'validated' or entry.get('probe_format') != FORMAT:
        return None
    # The hash is an index; compare its full context as well.
    if entry.get('lookup_context') != hardware['lookup_context']:
        return None
    return entry


def qualify(curves):
    """Find a near-optimal intersection, retaining sensitivity instead of exact K."""
    if len(curves) < 2:
        return {'status': 'insufficient-independent-runs', 'candidate_k': []}
    candidates = None
    best = []
    for point in curves:
        optimum = min(p['ns_per_load'] for p in point['curve'])
        best.append(optimum)
        good = {p['k'] for p in point['curve'] if p['ns_per_load'] <= 1.15 * optimum}
        candidates = good if candidates is None else candidates & good
    stable = max(best) <= 1.3 * min(best)
    return {'status': 'validated' if stable and candidates else 'unresolved-repeatability',
            'candidate_k': sorted(candidates or []), 'best_ns_per_load_range': [min(best), max(best)],
            'serial_ns_range': [min(c['serial_ns_per_load'] for c in curves), max(c['serial_ns_per_load'] for c in curves)],
            'individual_10pct_k': [c['smallest_k_within_10pct_of_best'] for c in curves],
            'right_censored': all(c['right_censored'] for c in curves),
            'near_best_tolerance': .15, 'scope': 'isolated CPU, 256 MiB random encoded-pointer chains, base pages'}


def consistent(points):
    if len(points) < 4 or not all(p['controls_separated'] for p in points):
        return 'unresolved'
    if all(p['normalised_saving'] >= .3 for p in points):
        return 'observed-benefit'
    if all(p['normalised_saving'] < .15 for p in points):
        return 'no-clear-benefit'
    return 'unresolved'


def build():
    campaign = json.loads((STUDY / 'campaign.json').read_text())
    database = {'format': FORMAT, 'date': campaign['date'], 'entries': {},
        'scope': 'spike observations qualified by CPU/environment; no exact hardware resource-count claims'}
    pooled_curves = defaultdict(list)
    table = ['# Fleet comparison', '',
             '256 MiB, base pages, isolated CPU. K ranges are the tested points within 15% of best in both independent-seed runs.', '',
             '| Instance | Architecture | Screen / repeat K (10%) | Common K (15%) | Best ns/load range | Quick seconds | Lookup |',
             '| --- | --- | --- | --- | --- | ---: | --- |']
    storage = ['','## Filesystem I/O and transport observations', '',
        'Medians from short tests, not sustained service limits or tail guarantees. Root filesystem unless marked instance store.', '',
        '| Instance | 4 KiB direct read µs | Write + fdatasync µs | TCP loopback 64 B µs | HTTPS connect / TLS / first byte ms |',
        '| --- | ---: | ---: | ---: | --- |']
    for machine in campaign['machines']:
        roots = [STUDY / 'evidence' / machine['name'] / x for x in ['screen', 'repeat']]
        hardware = [json.loads((p / 'hardware.json').read_text()) for p in roots]
        provenance = [json.loads((p / 'provenance.json').read_text()) for p in roots]
        if hardware[0]['lookup_key'] != hardware[1]['lookup_key']:
            raise ValueError(machine['name'] + ': unlike hardware contexts')
        if len({p['config']['seed'] for p in provenance}) != 2:
            raise ValueError(machine['name'] + ': independent seeds required')
        for root, receipt in zip(roots, provenance):
            if not receipt.get('source_unchanged'):
                raise ValueError('source changed')
            for name, expected in receipt['files_sha256'].items():
                if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
                    raise ValueError(f'retained evidence hash mismatch: {root / name}')
        analyses = [analyse(p, write=False) for p in roots]
        curves = [next(c for c in a['mlp'] if c['condition'] == 'isolated' and c['page_mode'] == 'base'
                       and c['bytes'] == 256 * 2**20) for a in analyses]
        qualification = qualify(curves)
        h = hardware[0]
        arch = h['recognised_architecture']
        if arch is None and h['identity']['vendor_id'] == 'AuthenticAMD' and h['identity']['cpu family'] == '23' and h['identity']['model'] == '49':
            arch = 'Zen 2'
        observations = [p for a in analyses for p in a['prefetch'] if p['condition'] == 'isolated']
        adjacent = {parity: consistent([p for p in observations if p['family'] == 'adjacent'
            and p['variant'] == parity and p['ahead'] == 1]) for parity in ['parity0', 'parity1']}
        learning = {}
        for stride in [1, 2, 4, -1, -2]:
            points = defaultdict(list)
            for p in observations:
                if p['family'] == 'learning' and p['stride'] == stride:
                    points[p['train']].append(p)
            lengths = sorted(points)
            suffix = [n for n in lengths if n >= 2 and sum(v >= n for v in lengths) >= 2 and
                      all(consistent(points[v]) == 'observed-benefit' for v in lengths if v >= n)]
            learning[str(stride)] = min(suffix) if suffix else None
        entry = {'probe_format': FORMAT, 'status': qualification['status'],
            'instance_examples': [machine['name']], 'architecture': arch, 'lookup_context': h['lookup_context'],
            'line_bytes_reported': h['line_bytes'], 'mlp': qualification,
            'adjacent_forward_observation': adjacent, 'consistent_training_suffix': learning,
            'stream_capacity': None, 'exact_outstanding_misses': None, 'cache_to_cache_route': None,
            'conditional_mlp_observations': [{k: point[k] for k in
                ['condition', 'page_mode', 'bytes', 'smallest_k_within_10pct_of_best', 'best_ns_per_load', 'serial_ns_per_load']}
                for point in analyses[0]['mlp'] if point['bytes'] == 256 * 2**20 and point['condition'] != 'isolated'],
            'limitations': ['idle isolated condition; active sibling load is a separate profile',
                'training suffix is a conditioned latency observation, not exact activation length',
                'seed repeats use the same worker; cross-instance evidence only where multiple examples are listed',
                'storage/network and current page allocation must be checked at runtime'],
            'evidence': [str(p.relative_to(STUDY)) for p in roots],
            'evidence_source_digests': [p['source_digest'] for p in provenance]}
        handoffs = defaultdict(list)
        with (roots[0] / 'samples.csv').open() as stream:
            for row in csv.DictReader(stream):
                if row['family'] == 'handoff':
                    handoffs[row['condition'], row['variant']].append(float(row['median']))
        entry['handoff_observations'] = [{'condition': condition, 'state': state, 'median_ns': stats.median(values),
             'instance': machine['name'], 'scope': 'one screen; repeated samples, no route inference'}
             for (condition, state), values in handoffs.items()]
        key = h['lookup_key']
        pooled_curves[key].extend(curves)
        if key in database['entries']:
            previous = database['entries'][key]
            previous['instance_examples'] += entry['instance_examples']
            previous['evidence'] += entry['evidence']
            previous['evidence_source_digests'] += entry['evidence_source_digests']
            previous['handoff_observations'] += entry['handoff_observations']
            previous['conditional_mlp_observations'] += entry['conditional_mlp_observations']
            previous['mlp'] = qualify(pooled_curves[key])
            previous['status'] = previous['mlp']['status']
            for parity, value in adjacent.items():
                if previous['adjacent_forward_observation'][parity] != value:
                    previous['adjacent_forward_observation'][parity] = 'unresolved'
            for stride, value in learning.items():
                prior = previous['consistent_training_suffix'][stride]
                previous['consistent_training_suffix'][stride] = max(prior, value) if prior and value else None
        else:
            database['entries'][key] = entry
        init = json.loads((roots[1] / 'initialisation.json').read_text())
        ks = ', '.join(map(str, qualification['candidate_k'])) or 'unresolved'
        costs = qualification['best_ns_per_load_range']
        table.append(f"| {machine['name']} | {arch or 'unknown'} | {qualification['individual_10pct_k']} | "
                     f"{ks} | {costs[0]:.2f}–{costs[1]:.2f} | {init['probe_seconds_including_allocation']:.2f} | {entry['status']} |")
        for suffix, path in [('', roots[0] / 'system.json'), (' instance store', roots[0] / 'instance-store/system.json')]:
            if not path.exists():
                continue
            system = json.loads(path.read_text())
            measures = system['storage'].get('measurements', {})
            direct = measures.get('4k-direct-random-read', {}).get('median_ns')
            sync = measures.get('4k-buffered-write-fdatasync', {}).get('median_ns')
            loop = system['network'].get('loopback', {}).get('64', {}).get('median_ns')
            remote = system['network'].get('regional_https', [])
            remote = [p for p in remote if p.get('exitcode') == 0]
            timing = ' / '.join(f"{stats.median(p[k] for p in remote)*1000:.2f}" for k in
                               ['time_connect', 'time_appconnect', 'time_starttransfer']) if remote else 'unavailable'
            fmt = lambda value: f'{value / 1000:.1f}' if value is not None else 'unavailable'
            storage.append(f"| {machine['name']}{suffix} | {fmt(direct)} | {fmt(sync)} | {fmt(loop)} | {timing} |")
    (STUDY / 'lookup.json').write_text(json.dumps(database, indent=2) + '\n')
    (STUDY / 'FLEET.md').write_text('\n'.join(table + storage) + '\n')
    print(f"Built {len(database['entries'])} qualified entries")
    return database


if __name__ == '__main__':
    build()
