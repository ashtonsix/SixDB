#!/usr/bin/env python3
"""Fetch the six immutable worker archives and regenerate measurement summaries."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import artifacts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError('choose a new recovery output directory')
    args.output.mkdir(parents=True)
    sources = []
    refs = json.loads((args.evidence / 'workers.json').read_text())
    for entry in refs:
        ref = args.output / (entry['job'] + '.artifact.json')
        ref.write_text(json.dumps(entry['artifact'], indent=2) + '\n')
        folder = args.output / entry['job']
        artifacts.fetch(ref, folder)
        sources.append(str(folder))
    for script in ['summarize.py', 'diagnostics.py', 'consensus.py']:
        subprocess.run([sys.executable, str(Path(__file__).parent / script), *sources, '--output', str(args.output / 'summary')], check=True)

if __name__ == '__main__':
    main()
