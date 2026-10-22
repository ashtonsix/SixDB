#!/usr/bin/env python3
"""Bounded local whole-image predicate comparison; no residence/fleet claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--input', type=Path, nargs='+', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cpu', type=int, default=7)
    p.add_argument('--trials', type=int, default=7)
    p.add_argument('--polars', action='store_true', help='actual Polars controls on identical all-mask inputs')
    a = p.parse_args()
    if a.trials < 1 or a.cpu not in os.sched_getaffinity(0):
        p.error('positive trials and an allowed CPU required')
    os.sched_setaffinity(0, {a.cpu})
    a.output.mkdir(parents=True, exist_ok=False)
    files = sorted(Path('workbench/spikes/ikea-frame/filter').glob('*'))
    files += [Path('workbench/spikes/ikea-frame/encoding') / n for n in ['material.h', 'material.cpp']]
    files += sorted(Path('ikea/include').rglob('*.h'))
    identity = {'binary_sha256': sha(a.binary), 'source_sha256': {str(f): sha(f) for f in files if f.is_file()},
                'platform': platform.platform(), 'cpu': a.cpu, 'trials': a.trials,
                'condition': 'warm whole-image COUNT; ARM VM exploratory; not DRAM or stream throughput',
                'inputs': {str(f): {'sha256': sha(f), 'bytes': f.stat().st_size} for f in a.input}}
    build = a.binary.parents[4]
    commands = json.loads((build / 'compile_commands.json').read_text())
    identity['compile_commands'] = [c for c in commands if '/ikea-frame/filter/' in c['file']]
    identity['compiler'] = subprocess.check_output(['clang++-21', '--version'], text=True)
    (a.output / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    groups = {}
    query_times = {}
    selected_rows = {}
    started = time.monotonic()
    for process in range(2):
        paths = a.input if process == 0 else reversed(a.input)
        for path in paths:
            label = path.parent.name + '-' + path.stem
            command = ['taskset', '-c', str(a.cpu), str(a.binary), str(path), str(a.trials)]
            if process:
                command.append('reverse')
            output = subprocess.run(command, check=True, capture_output=True, text=True).stdout
            (a.output / f'p{process}-{label}.jsonl').write_text(output)
            for line in output.splitlines():
                record = json.loads(line)
                if record['type'] == 'sample':
                    key = label, record['mask'], record['query'], record['arm']
                    rows = record['selected_rows']
                    groups.setdefault(key, []).append(record['ns'] / rows if rows else None)
                    query_times.setdefault(key, []).append(record['ns'])
                    assert key not in selected_rows or selected_rows[key] == rows
                    selected_rows[key] = rows
            print(process, label, 'exact consumed results', flush=True)
    assert sha(a.binary) == identity['binary_sha256'], 'ELF changed during comparison'
    for path, expected in identity['source_sha256'].items():
        assert sha(Path(path)) == expected, f'source changed: {path}'
    summary = [{'input': k[0], 'mask': k[1], 'query': k[2], 'arm': k[3],
                'selected_rows': selected_rows[k],
                'ns_per_selected_value': statistics.median(v) if selected_rows[k] else None,
                'min': min(v) if selected_rows[k] else None,
                'max': max(v) if selected_rows[k] else None, 'samples': v,
                'ns_per_query': statistics.median(query_times[k]), 'samples_ns': query_times[k]}
               for k, v in sorted(groups.items())]
    (a.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    if a.polars:
        polars_controls(a)
    print(len(groups), 'paired cells, seconds', round(time.monotonic() - started, 2))


def polars_controls(a):
    # Import only after limiting the pool and CPU. Preparation/casting is outside
    # query timing for both native material and encoded Frame images.
    os.environ['POLARS_MAX_THREADS'] = '1'
    import numpy as np
    import polars as pl
    records = []
    for path in a.input:
        label = path.parent.name + '-' + path.stem
        source = np.fromfile(path, dtype='<u8')
        base, maximum = int(source.min()), int(source.max())
        width = (maximum - base).bit_length()
        lane = 8 if width <= 8 else 16 if width <= 16 else 32 if width <= 32 else 64
        raw = pl.Series('x', source)
        ordered = bool(np.all(source[:-1] <= source[1:]))
        residuals = (source - base).astype(getattr(np, f'uint{lane}'))
        narrow = pl.Series('r', residuals)
        queries = [json.loads(s) for s in (a.output / f'p0-{label}.jsonl').read_text().splitlines()
                   if json.loads(s)['type'] == 'query' and json.loads(s)['mask'] == 0]
        for q in queries:
            threshold = q['threshold']
            rhs = threshold - base
            def native():
                return int((raw >= threshold).sum())
            def native_for():
                if rhs <= 0:
                    return len(source)
                if threshold > maximum:
                    return 0
                if rhs >> width:
                    return 0
                return int((narrow >= rhs).sum())
            arms = [('polars-u64', native), ('polars-native-for', native_for)]
            if ordered:
                def ordered_for():
                    if rhs <= 0:
                        return len(source)
                    if threshold > maximum:
                        return 0
                    return len(source) - int(narrow.search_sorted(rhs, side='left'))
                arms.append(('polars-ordered-for', ordered_for))
            samples = {name: [] for name, _ in arms}
            for name, fn in arms:
                assert fn() == q['answer'], (label, name, q)
            for trial in range(14):
                for name, fn in (arms if trial % 2 == 0 else reversed(arms)):
                    begin = time.perf_counter_ns()
                    answer = fn()
                    ns = time.perf_counter_ns() - begin
                    assert answer == q['answer'], (label, name, q)
                    samples[name].append(ns)
            for name, ns in samples.items():
                records.append({'input': label, 'query': q['query'], 'threshold': threshold,
                                'arm': name, 'answer': q['answer'], 'rows': len(source),
                                'polars': pl.__version__, 'threads': pl.thread_pool_size(),
                                'source_ordered': ordered,
                                'native_for_lane_bits': lane, 'native_for_base': base,
                                'samples_ns': ns, 'median_ns_per_value': statistics.median(ns) / len(source)})
        print('Polars exact', label, flush=True)
    (a.output / 'polars.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
