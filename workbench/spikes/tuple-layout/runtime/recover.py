#!/usr/bin/env python3
"""Recover a captured TuplePack spike run and its original reportable evidence."""

import argparse
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from artifacts import fetch
from evidence import compact_run, verify_compact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path,
                        help='Retained evidence directory containing artifact.json')
    parser.add_argument('output', type=Path,
                        help='New directory under build/, containing run/ and evidence/')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error('choose a new output directory')

    # The shared fetcher checks the archive hash, safe members and original run
    # receipt. Its build/ restriction also applies to this wrapper's output.
    fetch(args.evidence, output / 'run')
    compact = output / 'evidence'
    compact.mkdir()
    # Use the captured selection, including any CSVs/controls now archived-only.
    # A raw run alone lacks the provenance expected by the offline report tools.
    compact_run(output / 'run', compact)
    shutil.copyfile(args.evidence / 'artifact.json', compact / 'artifact.json')
    verify_compact(compact)
    print(f'Reportable evidence: {compact}')


if __name__ == '__main__':
    main()
