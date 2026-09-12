"""Verified local worker collection and reclamation of archived compiler output."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import time
import uuid

import artifacts
import storage


def read(path):
    try:
        value = json.loads(path.read_text())
        return value if isinstance(value, dict) else {}
    except (FileNotFoundError, ValueError):
        return {}


def matches(root, name, expected):
    path = root / name
    # Do not follow a replaced directory or symlink outside the collected tree.
    try:
        return (not root.is_symlink() and path.resolve() == root.resolve() / name and path.is_file() and
                path.stat().st_size == expected['bytes'] and artifacts.sha256(path) == expected['sha256'])
    except FileNotFoundError:
        return False  # Another collector may have moved a previously observed file.


def verified(directory, reference, *, full=False):
    receipt = read(directory / 'collection.json')
    members = receipt.get('members', {})
    if (receipt.get('artifact') != reference or not isinstance(members, dict) or len(members) != reference['files'] or
            not (directory / 'results').is_dir()):
        return False
    if any(not isinstance(value, dict) or not {'bytes', 'sha256'} <= value.keys()
           for value in members.values()):
        return False
    if not isinstance(receipt.get('evicted', []), list):
        return False
    evicted = set(receipt.get('evicted', [])) if not full else set()
    for name, expected in members.items():
        path = directory / 'results' / name
        if name in evicted and not path.exists() and not path.is_symlink():
            continue
        if not matches(directory / 'results', name, expected):
            return False
    return True


def collect(directory, reference, *, full=False):
    """A successful call means retained members match verified remote bytes."""
    with storage.lock(directory / '.collection.lock'):
        if verified(directory, reference, full=full):
            receipt = read(directory / 'collection.json')
            receipt['accessed_at'] = time.time()
            storage.write_json(directory / 'collection.json', receipt)
            return
        print(f'Verifying/restoring local collection: {directory.name}', flush=True)
        destination = directory / 'results'
        with tempfile.TemporaryDirectory(dir=directory, prefix='.collection-') as temp:
            staging = Path(temp) / 'results'
            members = artifacts.restore_bundle(reference, staging)
            # Preserve existing output if publication is interrupted. Its differing
            # files remain available as conflicts after the new tree is durable.
            previous = directory / ('replaced-results-' + uuid.uuid4().hex[:8])
            if destination.exists() or destination.is_symlink():
                destination.rename(previous)
            staging.rename(destination)
            storage.sync_directory(directory)
            storage.write_json(directory / 'collection.json', {
                'format': 1, 'artifact': reference, 'members': members,
                'evicted': [], 'accessed_at': time.time()})
        # Remove only byte-identical duplicates. Changed and user-added files stay
        # in the old tree, including interrupted-write witnesses such as empty files.
        if previous.is_dir() and not previous.is_symlink():
            for name, expected in members.items():
                if matches(previous, name, expected):
                    (previous / name).unlink()
            for path, _, _ in os.walk(previous, topdown=False):
                if not any(Path(path).iterdir()):
                    Path(path).rmdir()
        if previous.exists() or previous.is_symlink():
            print(f'Preserved differing local output: {previous}', flush=True)


def roots(root):
    """This repository's shared checkout, worktrees and conventional workspaces."""
    result = subprocess.run(['git', '-C', str(root), 'worktree', 'list', '--porcelain'],
                            capture_output=True, text=True)
    checkouts = {root.resolve()}
    if result.returncode == 0:
        checkouts.update(Path(line.removeprefix('worktree ')).resolve()
                         for line in result.stdout.splitlines() if line.startswith('worktree '))
    found = set()
    for checkout in checkouts:
        found.add(checkout / 'build/workers')
        found.update(checkout.glob('build/workspaces/*/build/workers'))
    unique = {}
    # OrbStack's Mac and Linux aliases can name the same directory.
    for path in sorted(found, key=lambda path: len(str(path))):
        if path.is_dir() and not path.is_symlink():
            stat = path.stat()
            unique.setdefault((stat.st_dev, stat.st_ino), path)
    return sorted(unique.values())


def inventory(root, older_hours=24):
    entries = []
    for workers in roots(root):
        for directory in sorted(workers.iterdir()):
            if not directory.is_dir() or directory.is_symlink():
                continue
            reference = read(directory / 'artifact.json')
            receipt = read(directory / 'collection.json')
            status = read(directory / 'status.json')
            size = reclaimable = 0
            results = directory / 'results'
            if results.is_symlink():
                continue
            for path in results.rglob('*'):
                try:
                    if path.is_file() and not path.is_symlink():
                        length = path.stat().st_size
                        size += length
                        if length >= 1024 ** 2:
                            with path.open('rb') as handle:
                                if artifacts.rebuildable(path.name, length, handle.read(512)):
                                    reclaimable += length
                except FileNotFoundError:
                    continue
            try:
                created = datetime.strptime(directory.name[:16], '%Y%m%dT%H%M%SZ').replace(tzinfo=timezone.utc).timestamp()
            except ValueError:
                created = directory.stat().st_mtime
            accessed = max(created, receipt.get('accessed_at', 0))
            has_reference = ({'bucket', 'region', 'key', 'bytes', 'sha256', 'files'} <= reference.keys())
            eligible = (has_reference and status.get('state') in {'complete', 'failed', 'timeout', 'interrupted'}
                        and time.time() - accessed >= older_hours * 3600
                        and not (directory / '.keep-local').exists())
            entries.append({'directory': directory, 'bytes': size, 'eligible': eligible,
                            'accessed': accessed, 'reference': reference, 'reclaimable': reclaimable})
    return sorted(entries, key=lambda entry: entry['accessed'])


def reclaim(directory, reference, *, older_hours=0):
    """Only remove unchanged large compiler output after a fresh full S3 read."""
    with storage.lock(directory / '.collection.lock'):
        if (directory / '.keep-local').exists() or (directory / 'results').is_symlink():
            return 0
        receipt = read(directory / 'collection.json')
        if time.time() - receipt.get('accessed_at', 0) < older_hours * 3600:
            return 0  # A fetch after inventory took priority while we waited for the lock.
        print(f'Checking archived compiler output: {directory.name}', flush=True)
        members = artifacts.remote_manifest(reference)
        destination = directory / 'results'
        candidates = {name: expected for name, expected in members.items()
                      if expected['rebuildable'] and matches(destination, name, expected)}
        if not candidates:
            return 0
        receipt = read(directory / 'collection.json')
        evicted = set(receipt.get('evicted', [])) if receipt.get('artifact') == reference else set()
        # The recovery receipt is durable before any removal. If interrupted,
        # remaining files are still checked, and already removed ones are explicit.
        storage.write_json(directory / 'collection.json', {
            'format': 1, 'artifact': reference, 'members': members,
            'evicted': sorted(evicted | candidates.keys()),
            'accessed_at': receipt.get('accessed_at', 0), 'reclaimed_at': time.time()})
        removed = 0
        for name, expected in candidates.items():
            if (directory / '.keep-local').exists():
                break
            if matches(destination, name, expected):
                path = destination / name
                path.unlink()
                storage.sync_directory(path.parent)
                removed += expected['bytes']
        print(f'Reclaimed {removed / storage.GIB:.2f} GiB; measurements retained: {directory.name}', flush=True)
        return removed


def maintain(root, *, prune=True, older_hours=24, protect=None):
    entries = inventory(root, older_hours)
    total = sum(entry['bytes'] for entry in entries)
    budget = int(float(os.environ.get('SIXDB_WORKER_CACHE_GIB', '10')) * storage.GIB)
    if budget < 0:
        raise ValueError('SIXDB_WORKER_CACHE_GIB must be nonnegative')
    free = min(info['free'] for info in storage.headroom(root).values())
    if not prune:
        largest = sorted(entries, key=lambda entry: entry['bytes'], reverse=True)[:20]
        for entry in largest:
            label = 'archived/old' if entry['eligible'] else 'recent/pinned/unarchived'
            print(f"{entry['bytes'] / storage.GIB:6.2f} GiB  {label:25} {entry['directory']}")
        for name, info in storage.headroom(root).items():
            print(f"{name}: {info['free'] / storage.GIB:.2f} GiB free ({info['path']})")
        print(f'Worker results: {total / storage.GIB:.2f} GiB; soft budget {budget / storage.GIB:g} GiB')
        print(f'Largest {len(largest)} of {len(entries)} collections shown; '
              f"{sum(e['reclaimable'] for e in entries if e['eligible']) / storage.GIB:.2f} GiB old compiler output to verify")
        return
    # Reclaim substantial space with fewer S3 reads; age still protects live work.
    for entry in sorted(entries, key=lambda entry: -entry['reclaimable']):
        if total <= budget and free >= 10 * storage.GIB:
            break
        if entry['eligible'] and entry['reclaimable']:
            if protect is not None and protect.exists() and entry['directory'].samefile(protect):
                continue
            try:
                removed = reclaim(entry['directory'], entry['reference'], older_hours=older_hours)
                total -= removed
                free = min(info['free'] for info in storage.headroom(root).values())
            except RuntimeError as error:
                print(f'Cache reclamation paused: {error}; local output preserved.', flush=True)
                break  # An unavailable S3 connection should not delay every job in the inventory.
            except (OSError, ValueError, tarfile.TarError) as error:
                print(f"Kept {entry['directory'].name}: archive check failed: {error}", flush=True)
    if total > budget:
        print(f'Worker results use {total / storage.GIB:.2f} GiB (soft budget {budget / storage.GIB:g}); '
              'recent/pinned output and measurements are preserved. Inspect: worker.py cache', flush=True)


def preflight(root, *, protect=None):
    maintain(root, protect=protect)
    storage.require_space(root)
