#!/usr/bin/env python3
"""Capture, build, execute bounded sweeps and retain an auditable characterisation."""
from __future__ import annotations
import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[2]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run
from hardware import discover
from lookup import match


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['quick', 'screen'], default='quick')
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--smt', action='store_true')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/workspaces/memory-characterisation')
    parser.add_argument('--lookup', type=Path, help='Static lookup JSON; matching entry skips sweeps')
    parser.add_argument('--force-probe', action='store_true')
    parser.add_argument('--system', action='store_true', help='Temporary-file I/O and bounded transport diagnostics')
    parser.add_argument('--mib', type=int, help='Override largest MLP footprint, 16..1024 MiB')
    parser.add_argument('--seed', type=int, default=357712151618, help='Recorded deterministic permutation seed')
    args = parser.parse_args()
    if args.mib is not None and not 16 <= args.mib <= 1024:
        parser.error('--mib must be between 16 and 1024')
    if not hasattr(os, 'sched_getaffinity'):
        parser.error('Run on Linux (orb -m ubuntu from macOS)')
    cpu = min(os.sched_getaffinity(0)) if args.cpu is None else args.cpu
    hardware = discover(cpu)
    output = (args.output or ROOT / 'build/experiments/memory-characterisation' /
              datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')).resolve()
    if args.lookup and args.lookup.exists() and not args.force_probe and not args.smt:
        database = json.loads(args.lookup.read_text())
        entry = match(hardware, database)
        if entry:
            output.mkdir(parents=True, exist_ok=False)
            (output / 'hardware.json').write_text(json.dumps(hardware, indent=2) + '\n')
            (output / 'initialisation.json').write_text(json.dumps({'source': 'static-lookup',
                'entry': entry, 'runtime_page_options': hardware['pages']}, indent=2) + '\n')
            if args.system:
                subprocess.run([sys.executable, str(STUDY / 'system_probe.py'), str(output)], check=True)
            print(f'Static lookup: {output}', flush=True)
            return 0
    config = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()} | {'cpu': cpu}
    run = Run(ROOT, output, config, workspace=args.build_dir.resolve())
    error = None
    try:
        (output / 'hardware.json').write_text(json.dumps(hardware, indent=2) + '\n')
        for name, cmd in [('hardware', ['lscpu']), ('compiler', ['clang++-21', '--version'])]:
            run.step(name, cmd)
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=memory-characterisation', '-DSIXDB_TUNE=generic', '-DSIXDB_MARCH='])
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', 'memory_characterisation_probe', '-j', '2'])
        binary = run.build_dir / 'workbench/spikes/memory-characterisation/memory_characterisation_probe'
        (output / 'bin').mkdir()
        shutil.copy2(binary, output / 'bin' / binary.name)
        for name in ['compile_commands.json', 'CMakeCache.txt']:
            shutil.copy2(run.build_dir / name, output / name)
        binary = output / 'bin' / binary.name
        screen = args.profile == 'screen'
        common = [str(binary), '--cpu', str(cpu), '--line', str(hardware['line_bytes'] or 64),
                  '--mib', str(args.mib or (256 if screen else 64)), '--reps', '3' if screen else '2',
                  '--ms', '5' if screen else '2', '--trials', '120' if screen else '40', '--seed', str(args.seed)]
        if not screen:
            common.append('--quick')
        probes = [('isolated', [])]
        if screen:
            probes += [('thp', ['--suite', 'mlp', '--huge']), ('thp-tlb', ['--suite', 'tlb', '--huge'])]
        if args.smt:
            # One peer for each visible cache-sharing domain and SMT relation.
            chosen = {}
            for peer in hardware['peers']:
                key = (peer['relation'], tuple(peer['shared_cache_levels']), tuple(peer.get('numa_nodes', [])))
                chosen.setdefault(key, peer)
            for peer in chosen.values():
                tag = f"{peer['relation']}-cpu{peer['cpu']}"
                probes.append((tag + '-handoff', ['--suite', 'coherence', '--peer', str(peer['cpu'])]))
                if peer['relation'] == 'smt-sibling' or len(chosen) <= 3:
                    for mode in ['compute', 'stream']:
                        probes.append((tag + '-' + mode, ['--suite', 'mlp', '--peer', str(peer['cpu']), '--interference', mode]))
                    probes.append((tag + '-prefetch', ['--suite', 'prefetch', '--peer', str(peer['cpu'])]))
            if len(hardware['numa']) > 1:
                for node in hardware['numa']:
                    probes.append((f'memory-{node}', ['--suite', 'mlp', '--memory-node', node[4:]]))
        start = time.monotonic()
        for name, options in probes:
            optional_binding = '--memory-node' in options
            run.step(name, common + options, name + '.csv', check=not optional_binding)
            if optional_binding and run.receipt['commands'][-1]['returncode']:
                reason = (output / (name + '.stderr')).read_text()
                if 'mbind unavailable' not in reason:
                    raise RuntimeError(f'{name} failed: {reason}')
                from hardware import cpus
                node = 'node' + options[options.index('--memory-node') + 1]
                candidates = set(cpus(hardware['numa'][node]['cpulist'])) & set(hardware['allowed_cpus'])
                if candidates:
                    probes.append((name + '-first-touch', ['--suite', 'mlp', '--first-touch-cpu', str(min(candidates))]))
        seconds = time.monotonic() - start
        if args.system:
            run.step('system', [sys.executable, str(run.source_root / STUDY.relative_to(ROOT) / 'system_probe.py'), str(output)])
            if os.environ.get('SIXDB_INSTANCE_STORE_DIR'):
                (output / 'instance-store').mkdir()
                run.step('instance-store', [sys.executable, str(run.source_root / STUDY.relative_to(ROOT) / 'system_probe.py'),
                    str(output / 'instance-store'), os.environ['SIXDB_INSTANCE_STORE_DIR']])
        with (output / 'samples.csv').open('w') as dest:
            writer = None
            for name, _ in probes:
                with (output / (name + '.csv')).open() as source:
                    reader = csv.DictReader(source)
                    if writer is None:
                        writer = csv.DictWriter(dest, ['condition'] + reader.fieldnames)
                        writer.writeheader()
                    for row in reader:
                        writer.writerow({'condition': name} | row)
        (output / 'initialisation.json').write_text(json.dumps({'source': 'runtime-probe',
            'recognition': hardware['recognition'], 'probe_seconds_including_allocation': seconds,
            'profile': args.profile, 'lookup_key': hardware['lookup_key']}, indent=2) + '\n')
        run.step('analyse', [sys.executable, str(run.source_root / STUDY.relative_to(ROOT) / 'analyze.py'), str(output)])
        run.compact(['hardware.json', 'samples.csv', 'analysis.json', 'summary.md', 'initialisation.json'] +
                    (['system.json'] if args.system else []) +
                    (['instance-store/system.json'] if args.system and os.environ.get('SIXDB_INSTANCE_STORE_DIR') else []), [])
    except Exception as exc:
        error = exc
    finally:
        error = run.finish(error)
    if error:
        print(f'FAILED: {error}', file=sys.stderr)
        return 1
    print(f'Complete: {output / "summary.md"}', flush=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
