#!/usr/bin/env python3
"""Compose experimental archive consumption and a fixed-capacity workspace.

Local immutable archive fixtures only. No live tooling or build migration.
The public run command always enters workspace.py's single guarded scope.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys


def inside(argv):
    """Internal continuation; workspace's pool lock/PID 1 cover this lifetime."""
    from acquire import MAX_REFERENCE, acquire, manifest_members, read_json, reference_identity
    from store import Store

    parser = argparse.ArgumentParser(description='Internal workspace continuation')
    parser.add_argument('--cache-budget', required=True, type=int)
    parser.add_argument('--output', required=True)
    parser.add_argument('reference', type=Path)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--manifest', type=Path)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--file', dest='selected', action='append')
    selection.add_argument('--full', action='store_true')
    separator = argv.index('--')
    args = parser.parse_args(argv[:separator])
    command = argv[separator + 1:]
    output = Path(args.output)
    if (not output.parts or output.is_absolute() or
            any(part in ('.', '..') for part in args.output.split('/'))):
        raise ValueError('output must be a relative directory inside the workspace')
    reference = reference_identity(read_json(args.reference, MAX_REFERENCE))
    expected = read_json(args.manifest, 8 * 1024 * 1024) if args.manifest else None
    if (expected is not None and manifest_members(expected) is not expected and 'artifact' in expected and
            reference_identity(expected['artifact']) != reference):
        raise ValueError('manifest artifact identity differs')
    # Cache and mutable output share the same enforcing filesystem. A cache
    # namespace limit alone never accounts for logs, output or open-unlinked data.
    store = Store(Path('cache').absolute(), args.cache_budget)
    try:
        result = acquire(store, reference, args.archive, selected=args.selected,
                         full=args.full, expected=expected, consumer=True)
        output = Path('output') / output
        output.mkdir(parents=True, exist_ok=True)
        os.chdir(output)
        # This is the sole intentionally un-nested consumer: the outer fixed-FS
        # wrapper retains its global pool lock until PID 1 reaps all descendants.
        # A second unshare/mount-proc guardian is incompatible with confinement.
        result['consumer'] = store.run_consumer(result.pop('paths'), command,
                                                outer_guard=True)
        print(json.dumps(result, sort_keys=True), flush=True)
        code = result['consumer']['returncode']
        return code if code >= 0 else 128 - code
    finally:
        store.close()


def main():
    argv = sys.argv[1:]
    if argv[:1] == ['--_inside']:
        return inside(argv[1:])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pool', type=Path, required=True)
    parser.add_argument('--budget', type=int, required=True)
    parser.add_argument('--size', type=int, required=True)
    parser.add_argument('--cache-budget', type=int)
    parser.add_argument('operation', choices=['run', 'exec'])
    parser.add_argument('name')
    parser.add_argument('reference', type=Path, nargs='?')
    parser.add_argument('archive', type=Path, nargs='?')
    parser.add_argument('--output', help='persistent relative output name')
    parser.add_argument('--manifest', type=Path)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--file', dest='selected', action='append')
    selection.add_argument('--full', action='store_true')
    if '--' not in argv:
        parser.parse_args(argv)  # Handles normal --help before reporting separator.
        parser.error('run requires -- COMMAND')
    separator = argv.index('--')
    args = parser.parse_args(argv[:separator])
    command = argv[separator + 1:]
    if not command:
        parser.error('a command is required')
    here = Path(__file__).resolve().parent
    if args.operation == 'exec':
        if any((args.reference, args.archive, args.output, args.manifest, args.selected,
                args.full, args.cache_budget is not None)):
            parser.error('exec does not take archive/cache/output arguments')
        # Even a completely full inode table must not require a new catalog or
        # acquired input before explicit inspection/cleanup can run. This has
        # exactly the same physical/lifetime boundary as normal consumption.
        os.execv(sys.executable, [sys.executable, str(here / 'workspace.py'),
                 '--pool', str(args.pool.absolute()), '--budget', str(args.budget),
                 'run', args.name, '--size', str(args.size), '--', *command])
    if (not all((args.reference, args.archive, args.output)) or
            args.cache_budget is None or not 0 < args.cache_budget < args.size):
        parser.error('run needs reference, archive, output and a cache budget smaller than the image')
    continuation = [sys.executable, str(here / 'flow.py'), '--_inside',
                    '--cache-budget', str(args.cache_budget), '--output', args.output,
                    str(args.reference.absolute()), str(args.archive.absolute())]
    if args.manifest:
        continuation += ['--manifest', str(args.manifest.absolute())]
    for name in args.selected or []:
        continuation += ['--file', name]
    if args.full:
        continuation += ['--full']
    continuation += ['--', *command]
    os.execv(sys.executable, [sys.executable, str(here / 'workspace.py'),
             '--pool', str(args.pool.absolute()), '--budget', str(args.budget),
             'run', args.name, '--size', str(args.size), '--', *continuation])


if __name__ == '__main__':
    from store import Refused
    try:
        raise SystemExit(main())
    except (Refused, OSError, ValueError, KeyError) as exc:
        print(f'flow refused: {type(exc).__name__}: {exc}', file=sys.stderr)
        raise SystemExit(125)
