#!/usr/bin/env python3
"""Probe cache events separately so an oversized group cannot hide sensitivity."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--catalog', type=Path, required=True)
    p.add_argument('--cpu', type=int, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    catalog = json.loads(a.catalog.read_text())
    events = [('llc-load-misses', 3, '0x10002')]
    arm = [(name, int(d['type'])) for name, d in catalog['event_sources'].items()
           if name.startswith(('armv8_pmuv3', 'armv9_pmuv3'))]
    if arm:
        kinds = [('raw', 4)] + [(name, kind) for name, kind in arm if kind != 4]
        for prefix, kind in kinds:
            events += [(prefix + '-' + level, kind, code) for level, code in
                       [('l1d-refill', '0x3'), ('l2d-refill', '0x17'), ('l3d-refill', '0x2a')]]
    a.output.mkdir(parents=True, exist_ok=False)
    manifest = dict(events=events, probes=[],
        uncore_devices=[name for name in catalog['event_sources']
                        if any(x in name.lower() for x in ('uncore', 'cmn', 'dmc', 'df', 'amd_l3'))],
        scope='Isolated availability/sensitivity diagnostics; nonzero counts do not establish serving level.')
    for name, kind, code in events:
        target = a.output / name
        command = [str(a.binary.resolve()), '--suite', 'mlp', '--quick', '--cpu', str(a.cpu),
            '--mib', '128', '--reps', '1', '--ms', '2', '--pmu', 'basic',
            '--pmu-event', f'{name}:{kind}:{code}', '--pmu-output', str(target)]
        result = subprocess.run(command, text=True, capture_output=True, timeout=180)
        (a.output / (name + '.csv')).write_text(result.stdout)
        (a.output / (name + '.stderr')).write_text(result.stderr)
        manifest['probes'].append(dict(name=name, command=command, returncode=result.returncode))
        (a.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(f'Cache diagnostic failed, distinct from an unavailable event: {name}')
        subprocess.run([sys.executable,
            str(ROOT / 'workbench/spikes/memory-characterisation/pmu_sensitivity.py'), str(target),
            '--output', str(a.output / (name + '-analysis.json'))], check=True, stdout=subprocess.DEVNULL)


if __name__ == '__main__':
    main()
