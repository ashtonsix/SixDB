#!/usr/bin/env python3
"""Reproduce the finite native policy grids and retain comparisons, including costs."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/simulator'))
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from campaign import grid, run_cases

POLICIES = ('ordered', 'eligible', 'drain', 'head')


def cases(suite, seeds=(1, 7, 19)):
    common = {'policy': POLICIES, 'seed': seeds}
    base = {'until': 1_000_000_000, 'events': 2_000_000, 'retry': 80_000_000,
            'interval': 20_000, 'region': 20_000_000, 'points': 48}
    if suite == 'main':
        yield from grid(dict(base, shape='regional', bridge=True), **common,
                        **{'bridge-cut': (False, True), 'shared': (False, True), 'points': (12, 48)})
        yield from grid(dict(base, shape='regional'), **common,
                        **{'progress-rounds': (8, 32), 'until': (10_000_000, 40_000_000)})
        yield from grid(dict(base, shape='chain'), **common, shared=(False, True))
        yield from grid(dict(base, shape='sparse-wan', points=64), **common, interval=(20_000, 1_000_000))
        yield from grid(dict(base, shape='younger-wan'), **common, region=(20_000_000, 80_000_000))
        yield from grid(dict(base, shape='unrelated-old'), **common, points=(32, 128),
                        region=(20_000_000, 80_000_000), until=(10_000_000, 1_000_000_000))
        yield from grid(dict(base, shape='ordinary', interval=500_000), **common)
        yield from grid(dict(base, shape='hot'), **common)
        yield from grid(dict(base, shape='regional', bridge=True, points=12), **common,
                        incident=('consumer-reset', 'coordinator-reset'))
    elif suite == 'long':
        yield from grid(dict(base, shape='sparse-wan', points=192, until=2_000_000_000),
                        **common, interval=(1_000_000, 4_000_000))
    elif suite == 'held':
        yield from grid(dict(base, shape='held-broad', points=16), **common,
                        region=(5_000_000, 20_000_000, 80_000_000))
    else:
        raise ValueError(f'unknown native suite {suite}')


def compact_trial(row):
    if 'result' not in row:
        return row
    r = row['result']
    keep = ('cohorts', 'completion_latency_ns', 'wire_bytes', 'durable_bytes', 'memory_peak',
            'policy_study', 'observed_until_ns', 'execution', 'triggered_incidents',
            'missing_incidents', 'violations')
    selected = {key: r[key] for key in keep if key in r}
    plans = {p['tx']: p for p in r.get('plans', [])}
    queues, broad, waves = {}, [], {}
    for m in r.get('milestones', []):
        plan = plans[m['tx']]
        for part in m['participants']:
            begin, end = part['acquire_input_ns'], part['granted_ns']
            if begin is None:
                continue
            wait = (end if end is not None else r['observed_until_ns']) - begin
            summary = queues.setdefault(plan['cohort'], {'samples': 0, 'censored': 0, 'max_ns': 0})
            summary['samples'] += 1
            summary['censored'] += end is None
            summary['max_ns'] = max(summary['max_ns'], wait)
            if plan['cohort'].startswith('broad') or plan['cohort'] == 'bridge':
                broad.append({'tx': plan['tx'], 'shard': part['shard'], 'queue_ns': begin,
                              'grant_ns': end, 'fix_ns': part['fixed_ns']})
            if row['case'].get('shape') == 'younger-wan' and plan['cohort'] == 'z-only':
                region = row['case']['region']
                wave = 'early' if plan['at'] < 4 * region + 15_000_000 else (
                    'middle' if plan['at'] < 4 * region + 65_000_000 else 'late')
                waves[wave] = max(waves.get(wave, 0), wait)
    selected['queue_waits'] = queues
    if broad:
        selected['broad_phases'] = broad
    if waves:
        selected['z_wave_max_queue_ns'] = waves
    return {'case': row['case'], 'result': selected}


def select(inputs, output):
    """Every competitor/repetition/cohort stays; full per-tx data stays ignored."""
    trials, sources = [], []
    for path in inputs:
        data = path.read_bytes()
        sources.append({'path': str(path), 'sha256': hashlib.sha256(data).hexdigest()})
        if path.suffix == '.jsonl':
            # Historical held-broad programmatic wrapper on the same frozen core.
            for line in data.splitlines():
                r = json.loads(line)
                if 'held_broad' not in r:
                    trials.append(compact_trial(r))
                    continue
                params = r['held_broad']
                trials.append({'case': dict(params, shape='held-broad'), 'result': {
                    key: r[key] for key in ('cohorts', 'wire_bytes', 'durable_bytes', 'memory_peak',
                        'observed_until_ns', 'execution', 'violations', 'triggered_incidents', 'missing_incidents')}})
        else:
            summary = json.loads(data)
            trials.extend(compact_trial(row) for row in summary['trials'])
            trials.extend(summary.get('errors', []))
    receipt = {'format': 1, 'selection': 'All competitors, repetitions, cohorts, failures, costs and broad waits; '
               'younger-WAN z waves kept separately. Full choices/phase histories remain in ignored storage.',
               'inputs': sources, 'selector_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               'trials': trials}
    output.write_text(json.dumps(receipt, separators=(',', ':')) + '\n')


def verify_source(source, run_receipt=None):
    if run_receipt is None:
        from capture import verify
        return verify(source)
    # Run's locked workspace stores its manifest in the run receipt; standalone
    # capture.py uses an adjacent receipt. Both retain the same file/hash map.
    receipt = json.loads(run_receipt.read_text())
    if not source.samefile(receipt['source_root']):
        raise ValueError('run receipt names a different source workspace')
    hashes = {name: hashlib.sha256((source / name).read_bytes()).hexdigest()
              for name in receipt['source_files_sha256']}
    if hashes != receipt['source_files_sha256']:
        raise ValueError('captured workspace sources changed')
    return receipt


def run_existing(source, binary, suite, output, seeds, run_receipt=None):
    receipt = verify_source(source, run_receipt)
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    failed = run_cases(binary, (dict(c, name=f'policy-{suite}-{i}') for i, c in enumerate(cases(suite, seeds))),
                       output, suite=f'reservation-policy-{suite}')
    verify_source(source, run_receipt)
    if hashlib.sha256(binary.read_bytes()).hexdigest() != binary_hash:
        raise ValueError('binary changed during campaign')
    (output / 'provenance.json').write_text(json.dumps({'source_digest': receipt['source_digest'],
        'binary_sha256': binary_hash, 'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}, indent=2) + '\n')
    select([output / 'summary.json'], output / 'selected.json')
    return failed


def captured(suites, output, workspace, seeds):
    from experiment import Run
    run = Run(ROOT, output, {'suites': suites, 'seeds': seeds}, workspace=workspace)
    failure = None
    try:
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo'])
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', 'simulator_regional_run', 'simulator_regional_check', '-j', '4'])
        run.step('check', [str(run.build_dir / 'workbench/simulator/simulator_regional_check')])
        for suite in suites:
            run.step(suite, [sys.executable, str(run.source_root / Path(__file__).resolve().relative_to(ROOT)),
                '--source', str(run.source_root), '--receipt', str(output / 'run.json'), '--binary', str(run.build_dir / 'workbench/simulator/simulator_regional_run'),
                '--suite', suite, '--seeds', ','.join(map(str, seeds)), '--output', str(output / suite)])
        run.compact([f'{suite}/{name}' for suite in suites for name in ('selected.json', 'provenance.json')], [])
    except Exception as error:
        failure = error
    failure = run.finish(failure)
    if failure:
        raise failure


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('main', 'long', 'held', 'all'), default='all')
    parser.add_argument('--seeds', default='1,7,19')
    parser.add_argument('--source', type=Path)
    parser.add_argument('--receipt', type=Path, help='Run workspace receipt; standalone captures use their adjacent receipt')
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workspace', type=Path, default=ROOT / 'build/workspaces/reservation-policy')
    parser.add_argument('--select', type=Path, nargs='+', help='select existing summary JSON or historical held-wrapper JSONL')
    args = parser.parse_args()
    if args.select:
        select(args.select, args.output)
    elif args.binary:
        if not args.source or args.suite == 'all':
            parser.error('--binary needs --source capture and one suite')
        raise SystemExit(run_existing(args.source.resolve(), args.binary.resolve(), args.suite, args.output.resolve(), tuple(map(int, args.seeds.split(','))), args.receipt))
    else:
        captured(('main', 'long', 'held') if args.suite == 'all' else (args.suite,),
                 args.output.resolve(), args.workspace.resolve(), tuple(map(int, args.seeds.split(','))))
