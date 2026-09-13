#!/usr/bin/env python3
"""Retain the selected fleet comparisons through the existing artifact helper."""
import concurrent.futures
import json
from pathlib import Path
import subprocess
import sys

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[2]


def retain(machine):
    for kind in ['screen', 'repeat']:
        parts = machine[kind].split('/')
        source = ROOT / 'build/workers' / parts[0] / 'results' / (parts[1] if len(parts) > 1 else 'study')
        target = STUDY / 'evidence' / machine['name'] / kind
        files = ['hardware.json', 'samples.csv', 'initialisation.json', 'isolated.stderr']
        if kind == 'screen':
            files += [p for p in ['system.json', 'thp.stderr', 'thp-tlb.stderr', 'instance-store/system.json'] if (source / p).exists()]
            files += [p.name for p in source.glob('memory-node*.stderr')]
        command = [sys.executable, str(ROOT / 'workbench/tools/artifacts.py'), 'retain', str(source), str(target)]
        for name in files:
            command += ['--file', name]
        completed = subprocess.run(command, capture_output=True, text=True)
        if completed.returncode:
            raise RuntimeError(machine['name'] + ': ' + completed.stdout + completed.stderr)
    return machine['name']


if __name__ == '__main__':
    campaign = json.loads((STUDY / 'campaign.json').read_text())
    selected = [m for m in campaign['machines'] if not sys.argv[1:] or m['name'] in sys.argv[1:]]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for name in pool.map(retain, selected):
            print('Retained', name, flush=True)
