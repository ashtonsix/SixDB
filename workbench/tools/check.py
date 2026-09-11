#!/usr/bin/env python3
"""Run one helper's checks; use --list to discover them. No cloud jobs are launched."""
import argparse
import ast
from pathlib import Path
import runpy
import sys


def main():
    here = Path(__file__).resolve().parent
    checks = {p.stem.removeprefix('check_'): p for p in sorted((here / 'tests').glob('check_*.py'))}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true', help='show checks and their purpose')
    parser.add_argument('check', choices=checks, nargs='?')
    parser.add_argument('args', nargs=argparse.REMAINDER, help='arguments for that check')
    args = parser.parse_args()
    if args.list or args.check is None:
        for name, path in checks.items():
            print(f'{name}: {ast.get_docstring(ast.parse(path.read_text()))}')
        return
    path = checks[args.check]
    sys.path[:0] = [str(here), str(path.parent)]
    sys.argv = [str(path), *args.args]
    runpy.run_path(str(path), run_name='__main__')


if __name__ == '__main__':
    main()
