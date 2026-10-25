#!/usr/bin/env python3
"""Report the native quota; optionally reclaim freshly verified cached inputs."""
import argparse
from contextlib import closing
import os
from pathlib import Path

import storage


def maintain(root, *, prune=True):
    managed = storage.managed_status()
    if managed is None:
        room = storage.headroom(Path(root) / 'build')
        print('Native storage is unconfigured; filesystem headroom is diagnostic and '
              'no SixDB capacity limit is enforced.')
        for name, value in room.items():
            print(f'{name}: {value["free"] / storage.GIB:.2f} GiB free ({value["path"]})')
        return {'scope': 'unconfigured', 'headroom': room, 'errors': [], 'removed_bytes': 0}

    errors, removed = [], 0
    if prune and (Path(managed['root']) / 'immutable').exists():
        from sixdb_storage.store import MARGIN, Refused
        try:
            with closing(storage.immutable_store()) as store:
                before = store.used()
                reserve = int(float(os.environ.get('SIXDB_MIN_FREE_GIB', '5')) * storage.GIB)
                if reserve < 0:
                    raise ValueError('storage reserve must be nonnegative')
                try:
                    store.admit(reserve + MARGIN, external=True)
                finally:
                    removed = max(0, before - store.used())
        except (OSError, Refused) as error:
            errors.append(f'verified cache reclamation refused: {error}')
        managed = storage.managed_status()

    report = {'scope': 'native-subvolume-quota', 'bytes': managed['used_bytes'],
              'budget_bytes': managed['capacity_bytes'], 'available_bytes': managed['available_bytes'],
              'managed_volume': managed, 'errors': errors, 'removed_bytes': removed,
              'over_budget': managed['used_bytes'] > managed['capacity_bytes']}
    print(f'Native storage: {managed["used_bytes"] / storage.GIB:.2f} GiB used, '
          f'{managed["available_bytes"] / storage.GIB:.2f} GiB available in the '
          f'{managed["capacity_bytes"] / storage.GIB:g} GiB Btrfs quota '
          f'({managed["qgroup"]}, {managed["scope"]}). '
          'All data in this subvolume, including builds, Git worktrees, temporary and '
          'open-unlinked files, shares the limit.')
    if removed:
        print(f'Reclaimed {removed / storage.GIB:.2f} GiB of freshly verified recoverable cache.')
    for error in errors:
        print(error)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prune', action='store_true',
                        help='reclaim verified recoverable input cache under capacity pressure')
    args = parser.parse_args()
    try:
        maintain(storage.canonical_path(Path(__file__).resolve().parents[2]), prune=args.prune)
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error))
