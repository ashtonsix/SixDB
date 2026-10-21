#!/usr/bin/env python3
"""Recover the pre-API-cleanup Ikea control without maintaining a second library."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--revision', default='7a57deae')
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', args.revision,
    'ikea/include', 'ikea/src/boundlist', 'ikea/src/streampack'], cwd=root, text=True).splitlines()
if args.output.exists():
    raise SystemExit('Choose a new output directory; frozen controls are never overwritten.')
for name in paths:
    target = args.output / Path(name).relative_to('ikea')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(subprocess.check_output(['git', 'show', f'{args.revision}:{name}'], cwd=root))
print(f'{len(paths)} control files recovered from {args.revision} into {args.output}')
