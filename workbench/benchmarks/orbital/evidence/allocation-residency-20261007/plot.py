#!/usr/bin/env python3
"""Regenerate this frozen comparison from its hash-checked selected inputs."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
from statistics import median

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm
import numpy as np


def selected(root, name):
    path = root / name / 'selected.jsonl'
    provenance = json.loads(path.with_name('provenance.json').read_text())
    assert hashlib.sha256(path.read_bytes()).hexdigest() == provenance['files_sha256'][path.name]
    return {e['source_file']: e for e in map(json.loads, path.read_text().splitlines())}


def table(entry):
    return [dict(zip(entry['columns'], row, strict=True)) for row in entry['rows']]


def scalar(root, output):
    data = selected(root, 'common-scalar')
    cases = data['orbital-allocation/paired/comparison.json']['record']
    workloads = ['point-hit', 'point-miss', 'dependent-16', 'scan-16', 'stock-range',
                 'order-status', 'point-update', 'point-rmw', 'insert-random', 'delete',
                 'append', 'insert-scan', 'ycsb-a', 'ycsb-b', 'ycsb-c', 'ycsb-d',
                 'ycsb-e', 'ycsb-f', 'order-entry']
    sizes = [8192, 262144, 4194304]
    throughput = np.empty((len(workloads), 3))
    tail = np.empty_like(throughput)
    rows = []
    for i, workload in enumerate(workloads):
        for j, records in enumerate(sizes):
            pair = [r for r in cases if r['workload'] == workload and r['records'] == records
                    and r['candidate'] == 'packed-owner' and r['reference'] == 'lmdb-read-batch64']
            assert len(pair) == 2 and all(r['trials'] == 3 and r['batch_width'] == 1
                                         and r['latency_unit'] == 'logical request' for r in pair)
            rate = next(r for r in pair if r['mode'] == 'throughput')
            latency = next(r for r in pair if r['mode'] == 'latency')
            throughput[i, j] = rate['throughput_ratio']
            tail[i, j] = latency['p99_ns_headroom_ns'] / 1000
            rows.append({'workload': workload, 'records': records,
                         'throughput_ratio': rate['throughput_ratio'],
                         'p50_headroom_ns': latency['p50_ns_headroom_ns'],
                         'p99_headroom_ns': latency['p99_ns_headroom_ns']})
    fig, axes = plt.subplots(1, 2, figsize=(10.5, 8.7), layout='constrained', sharey=True)
    im0 = axes[0].imshow(np.log2(throughput), cmap='RdBu',
                         norm=TwoSlopeNorm(vmin=-1, vcenter=0, vmax=5), aspect='auto')
    im1 = axes[1].imshow(tail, cmap='RdBu',
                         norm=TwoSlopeNorm(vmin=-14, vcenter=0, vmax=14), aspect='auto')
    for ax, title in zip(axes, ['Throughput / LMDB', 'LMDB p99 minus candidate p99 (µs)']):
        ax.set_title(title, pad=14)
        ax.set_xticks(range(3), ['8K rows', '262K rows', '4.2M rows'])
        ax.xaxis.tick_top()
        ax.set_yticks(range(len(workloads)), workloads)
        ax.tick_params(length=0)
        ax.spines[['left', 'right', 'bottom', 'top']].set_visible(False)
    for i in range(len(workloads)):
        for j in range(3):
            axes[0].text(j, i, f'{throughput[i, j]:.2f}×', ha='center', va='center',
                         color='white' if math.log2(throughput[i, j]) > 2.5 else '#182030')
            axes[1].text(j, i, f'{tail[i, j]:+.2f}', ha='center', va='center',
                         color='white' if abs(tail[i, j]) > 8 else '#182030')
    fig.colorbar(im0, ax=axes[0], location='bottom', ticks=[-1, 0, 1, 2, 3, 4, 5],
                 format=lambda v, _: f'{2.0 ** v:g}×', shrink=.95)
    fig.colorbar(im1, ax=axes[1], location='bottom', shrink=.95)
    fig.suptitle('Scalar allocation stage: headroom remains incomplete\n'
                 'Real LMDB reference reuses read transactions for up to 64 requests', fontsize=13)
    fig.supxlabel('Writes use one transaction per request. Ratios of three trial medians; fixed trace.\n'
                  'Closed-loop request latency; cache service unqualified; omitted database costs unpriced.', fontsize=9)
    fig.savefig(output / 'scalar-headroom.png', dpi=180)
    fig.savefig(output / 'scalar-headroom.svg')
    plt.close(fig)
    return rows


def widths(root, output):
    cells = defaultdict(list)
    counts = {}
    for recipe, name in [('historical', 'historical-width'), ('frontier', 'frontier16')]:
        data = selected(root, name)
        count = 0
        for path, entry in data.items():
            if not path.endswith('/cases.csv'):
                continue
            manifest = data[path.replace('cases.csv', 'run.json')]['record']
            assert manifest['status'] == 'complete'
            config = manifest['config']
            rows = table(entry)
            assert len(rows) == 81
            for r in rows:
                assert r['mode'] == 'throughput' and int(r['batch_width']) == 64
                assert int(r['offered_requests']) == int(r['completed_requests']) == 100000
                key = (recipe, int(r['records']), r['workload'], config['read_execution'],
                       int(r['read_active']), r['cache_setup'], r['backend'])
                cells[key].append(float(r['elapsed_ns']) / int(r['completed_requests']))
                count += 1
        counts[recipe] = count
    assert counts == {'historical': 1944, 'frontier': 486}
    assert all(len(v) == 3 for v in cells.values())
    fig, axes = plt.subplots(2, 3, figsize=(10.5, 6.5), layout='constrained')
    rows = []
    for i, setup in enumerate(['loaded', 'warm-trace']):
        for j, workload in enumerate(['point-hit', 'point-miss', 'dependent-16']):
            ax = axes[i, j]
            historical = {n: median(cells['historical', 4194304, workload, 'ring', n, setup, 'packed-owner'])
                          for n in [1, 16, 32, 64]}
            frontier = median(cells['frontier', 4194304, workload, 'ring', 16, setup, 'packed-owner'])
            ax.plot(list(historical), list(historical.values()), '-o', color='#4776a8', label='Historical recipe')
            ax.scatter([16], [frontier], color='#c95729', marker='D', s=45, zorder=3, label='Finer stages: width 16 only')
            ax.set_xscale('log', base=2)
            ax.set_xticks([1, 16, 32, 64], ['1', '16', '32', '64'])
            ax.set_yscale('log')
            ax.set_title(f'{workload} · {setup}', fontsize=11)
            ax.grid(alpha=.2)
            if j == 0:
                ax.set_ylabel('ns / logical request (lower is better)')
            if i == 1:
                ax.set_xlabel('Configured active limit')
            rows.append({'setup': setup, 'workload': workload,
                         'historical_ring_ns_per_request_by_active': historical,
                         'frontier_ring_active16_ns_per_request': frontier,
                         'frontier16_over_historical16_throughput': historical[16] / frontier})
    axes[0, 1].legend(fontsize=8)
    fig.suptitle('4.2M-row Ring reads: tuning can change the recipe comparison', fontsize=14)
    fig.supxlabel('64 offered requests; same guest, sequential panels, three trial medians.\n'
                  'Width also changes overflow promotion. Chain16 contains sixteen lookups; cache service unresolved.', fontsize=9)
    fig.savefig(output / 'width-history.png', dpi=180)
    fig.savefig(output / 'width-history.svg')
    plt.close(fig)
    return rows


def counters(root):
    intervals = [e['record'] for path, e in selected(root, 'graviton-events').items()
                 if '/pmu-intervals/' in path]
    assert len(intervals) == 360
    unavailable = defaultdict(int)
    l1 = defaultdict(list)
    for r in intervals:
        m = r['measurement']
        assert m['status'] == 'complete' and m['coverage'] == 1 and r['observed_load_service_tier'] is None
        for e in m['events']:
            if e['name'].endswith('-refill'):
                assert not e['opened'] and e['status'] == 'unavailable' and e['count'] is None
                assert e['errors'][0]['errno'] == 22
                unavailable[e['name']] += 1
            elif e['name'] == 'l1d-load-misses' and r['active_chains'] == 1:
                assert e['status'] == 'measured'
                l1[r['bytes']].append(e['count'] / r['loads'])
    assert len(unavailable) == 6 and set(unavailable.values()) == {360}
    composed = selected(root, 'graviton-composed')
    rows = []
    for path, e in composed.items():
        if not path.endswith('/result.json'):
            continue
        r = e['record']
        assert r['validated'] and r['observed_load_service_tier'] is None
        pmu = composed[path.replace('result.json', 'pmu.json')]['record']
        assert pmu['measurement']['coverage'] == 1
        for field in ['input_fingerprint', 'trace_fingerprint', 'completed_requests']:
            assert pmu[field] == r[field]
        if r['backend'] == 'packed-owner' and r['records'] == 4194304 and r['workload'] == 'point-hit' and r['cache_setup'] == 'loaded':
            m = pmu['measurement']
            events = {x['name']: x['count'] for x in m['events']}
            rows.append({'execution': r['read_execution'], 'ns_per_request': r['elapsed_ns'] / r['completed_requests'],
                         'instructions_per_request': events['retired_instructions'] / r['completed_requests'],
                         'generic_l1_events_per_request': events['l1d-load-misses'] / r['completed_requests'],
                         'ipc': m['ipc']})
    assert len(rows) == 3
    return {'real_memory_intervals': 360, 'unavailable_errno22_by_event': dict(unavailable),
            'generic_l1_events_per_load_k1_by_bytes': {k: median(v) for k, v in sorted(l1.items())},
            'large_loaded_composed_point_hit': rows,
            'limits': 'L1 signal responsive; refill source unresolved. Single-trial composed PMU diagnostic; caller-only CPU/events.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, default=Path(__file__).resolve().parent,
                        help='Complete selected inputs recovered from the evidence archive')
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    root = args.inputs.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
    summary = {'format': 1, 'status': 'point-in-time comparison; headroom failed/pending',
               'cohorts': json.loads((root / 'cohorts.json').read_text())['cohorts'],
               'scalar': scalar(root, args.output), 'large_ring': widths(root, args.output),
               'counter_qualification': counters(root)}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('Regenerated 57 scalar cells, 6 width/history comparisons and actual PMU diagnostics.')


if __name__ == '__main__':
    main()
