#!/usr/bin/env python3
"""Compatibility entry point for AZ worker-group collection and cancellation."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from worker_group import Group


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--abort', action='store_true')
    args = parser.parse_args()
    state = json.loads(args.receipt.read_text())
    if 'members' not in state:
        if state.get('cleanup', {}).get('instances_terminated') and state['cleanup'].get('security_group_deleted'):
            print('Historical campaign already cleaned up; recover its evidence with recover.py.')
            return 0
        parser.error('legacy campaign: inspect its recorded job IDs with worker.py before further cleanup')
    group = Group(args.receipt)
    return group.cancel() if args.abort else group.wait()


if __name__ == '__main__':
    raise SystemExit(main())
