#!/usr/bin/env python3
"""Capture one spatial consumer experiment; never provisions a worker."""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import sys

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
sys.path.insert(0, str(ROOT / 'workbench/spikes/memory-characterisation'))
from experiment import Run
from hardware import discover


def cache_bytes(text):
    value = text.upper().strip()
    return int(value[:-1]) * {'K': 1024, 'M': 1024**2, 'G': 1024**3}[value[-1]] if value[-1] in 'KMG' else int(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['smoke', 'screen'], default='smoke')
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--workspace', type=Path, default=ROOT / 'build/workspaces/layout-spatial')
    parser.add_argument('--large-mib', type=int, help='Logical 96B-record footprint (16..1024 MiB); not a residency assertion')
    parser.add_argument('--seeds', default='12971,918273')
    parser.add_argument('--phases', help='Comma-separated byte offsets; defaults 0,32 (smoke), or 0,32,64,96 (screen)')
    parser.add_argument('--reps', type=int)
    parser.add_argument('--ms', type=float)
    parser.add_argument('--compute', type=int, default=0, help='Useful arithmetic rounds; vary only in a targeted follow-up')
    parser.add_argument('--ordered', action='store_true')
    parser.add_argument('--prefetch-extension', action='store_true')
    parser.add_argument('--march', default='')
    parser.add_argument('--tune', default='generic')
    args = parser.parse_args()
    if not hasattr(os, 'sched_getaffinity'):
        parser.error('Run on Linux; prefix with orb -m ubuntu on macOS')
    if args.large_mib is not None and not 16 <= args.large_mib <= 1024:
        parser.error('--large-mib must be 16..1024')
    if not 0 <= args.compute <= 64:
        parser.error('--compute must be 0..64')
    cpu = min(os.sched_getaffinity(0)) if args.cpu is None else args.cpu
    hardware = discover(cpu)
    if hardware['line_bytes'] is None:
        parser.error('No reported data-line size; refusing an implicit 64B model')
    caches = [cache_bytes(c['size']) for c in hardware['caches'] if c['size'] and c['type'] in ['Data', 'Unified']]
    llc = max(caches, default=0)
    large_mib = args.large_mib or max(64, min(512, (6 * llc + 2**20 - 1) // 2**20))
    # Keep the smallest core-only plane around four LLC capacities where the cap permits.
    footprints = [('hot-control', 128), ('larger-smoke', 8192)] if args.profile == 'smoke' else [
        ('hot-control', 128), ('larger', (large_mib * 2**20 // 96 // 32) * 32)]
    seeds = [int(x) for x in args.seeds.split(',')]
    phases = [int(x) for x in (args.phases or ('0,32' if args.profile == 'smoke' else '0,32,64,96')).split(',')]
    if not seeds or len(set(seeds)) != len(seeds) or any(x < 0 or x >= 2**64 for x in seeds):
        parser.error('Seeds must be distinct unsigned 64-bit integers')
    if not phases or len(set(phases)) != len(phases) or any(x < 0 or x % 8 or x >= hardware['pages']['base_bytes'] for x in phases):
        parser.error('Phases must be distinct 8B multiples within one page')
    reps = args.reps if args.reps is not None else (1 if args.profile == 'smoke' else 3)
    ms = args.ms if args.ms is not None else (1 if args.profile == 'smoke' else 5)
    if not 1 <= reps <= 20 or not 0 < ms <= 1000:
        parser.error('Repetitions must be 1..20 and ms in (0,1000]')
    output = (args.output or ROOT / 'build/experiments/layout-spatial' /
              datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')).resolve()
    config = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()} | {'cpu': cpu, 'reps': reps, 'ms': ms}
    run = Run(ROOT, output, config, workspace=args.workspace.resolve())
    error = None
    try:
        (output / 'hardware.json').write_text(json.dumps(hardware, indent=2) + '\n')
        plan = {'format': 1, 'line_bytes': hardware['line_bytes'], 'largest_reported_cache_bytes': llc,
            'footprints': [{'name': label, 'rows': rows, 'logical_bytes': rows * 96,
                'smallest_core_only_bytes': rows * 64,
                'core_bytes_over_reported_llc': rows * 64 / llc if llc else None} for label, rows in footprints],
            'seeds': seeds, 'phases': phases, 'extension_phase': 'same offset as core base, separate mapping for split',
            'qualification': 'Reported cache capacity is context, not measured residency. Smoke sizes establish no DRAM claim.'}
        (output / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=layout-analyser', '-DSIXDB_MARCH=' + args.march,
            '-DSIXDB_TUNE=' + args.tune])
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', 'layout_spatial_probe', '-j', '2'])
        original = run.build_dir / 'workbench/spikes/layout-analyser/spatial/layout_spatial_probe'
        (output / 'bin').mkdir()
        shutil.copy2(original, output / 'bin/layout_spatial_probe')
        binary = str(output / 'bin/layout_spatial_probe')
        for name in ['compile_commands.json', 'CMakeCache.txt']:
            shutil.copy2(run.build_dir / name, output / name)
        run.step('check', [sys.executable, str(run.source_root / STUDY.relative_to(ROOT) / 'check.py'), binary, '--cpu', str(cpu)], 'checks.txt')
        options = ['--cpu', str(cpu), '--line', str(hardware['line_bytes']), '--reps', str(reps), '--ms', str(ms),
            '--compute', str(args.compute)] + (['--ordered'] if args.ordered else []) + (
            ['--prefetch-extension'] if args.prefetch_extension else [])
        with (output / 'samples.csv').open('w', newline='') as out, (output / 'placement.txt').open('w') as receipts:
            writer = None
            for label, rows in footprints:
                for seed in seeds:
                    for phase in phases:
                        tag = f'{label}-s{seed}-p{phase}'
                        run.step(tag, [binary, '--rows', str(rows), '--seed', str(seed), '--phase', str(phase),
                            '--extension-phase', str(phase)] + options, tag + '.csv')
                        receipts.write(f'=== {tag} ===\n' + (output / (tag + '.stderr')).read_text())
                        with (output / (tag + '.csv')).open() as inp:
                            reader = csv.DictReader(inp)
                            if writer is None:
                                writer = csv.DictWriter(out, ['footprint'] + reader.fieldnames, lineterminator='\n')
                                writer.writeheader()
                            for row in reader:
                                writer.writerow({'footprint': label} | row)
        run.step('report', [sys.executable, str(run.source_root / STUDY.relative_to(ROOT) / 'report.py'), str(output)])
        run.compact(['samples.csv', 'hardware.json', 'plan.json', 'placement.txt', 'checks.txt', 'summary.md', 'analysis.json'],
                    ['python3', str(STUDY.relative_to(ROOT) / 'report.py'), '{evidence}'])
    except Exception as exc:
        error = exc
    finally:
        error = run.finish(error)
    if error:
        print(f'FAILED: {error}', file=sys.stderr)
        return 1
    print(f'Complete: {output / "summary.md"}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
