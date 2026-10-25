"""Verified local worker collection, recovery and cache reclamation."""
import errno
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time

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


def normally_required(name, entry):
    # The transport source archive is unique evidence in S3, not rebuildable
    # compiler output. Keep its identity locally and fetch its bytes on request.
    return Path(name).name not in {'source.tar.gz', 'source.tar.zst'} and not entry.get('rebuildable', False)


def verified(directory, reference, *, full=False, selected=None):
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
    if selected is not None and set(selected) - members.keys():
        return False
    evicted = set(receipt.get('evicted', [])) if not full else set()
    for name, expected in members.items():
        path = directory / 'results' / name
        required = full or (name in selected if selected is not None else normally_required(name, expected))
        if name in evicted and not required and not path.exists() and not path.is_symlink():
            continue
        if not matches(directory / 'results', name, expected):
            return False
    return True


def list_files(directory, reference):
    receipt = read(directory / 'collection.json')
    members = receipt.get('members', {}) if receipt.get('artifact') == reference else {}
    if len(members) != reference['files']:
        members = artifacts.remote_manifest(reference)
    for name, entry in sorted(members.items()):
        location = 'present' if (directory / 'results' / name).is_file() else 'S3'
        print(f"{entry['bytes']:>12,} bytes  {location:7} {name}")


def record_collection(directory, reference, members):
    previous = read(directory / 'collection.json')
    receipt = {'format': 1, 'artifact': reference, 'members': members,
               'evicted': sorted(name for name in members if not (directory / 'results' / name).exists()),
               'accessed_at': time.time()}
    if previous.get('artifact') == reference and isinstance(previous.get('root_evicted'), dict):
        receipt['root_evicted'] = previous['root_evicted']
    storage.write_json(directory / 'collection.json', receipt)


def pinned(directory):
    directory = Path(directory)
    return any((parent / '.keep-local').exists() for parent in (directory, *directory.parents))


def release_launch_source(directory, reference, members):
    """After a fresh archive verification, release only its identical launch copy."""
    expected = members.get('source.tar.gz')
    if (not expected or pinned(directory) or
            not matches(directory, 'source.tar.gz', expected)):
        return
    receipt = read(directory / 'collection.json')
    root_evicted = receipt.get('root_evicted', {})
    storage.write_json(directory / 'collection.json', receipt | {'root_evicted': root_evicted | {
        'source.tar.gz': {'archive_member': 'source.tar.gz', **expected}}})
    if not pinned(directory) and matches(directory, 'source.tar.gz', expected):
        (directory / 'source.tar.gz').unlink()
        storage.sync_directory(directory)


def release_uploaded_source(directory, job):
    """Finish launch-source ownership when its independent S3 bytes verify.

    Source is needed for upload and remote execution, not as another permanent
    local package for every dispatched job. Recovery remains at the job URI.
    """
    import hashlib
    from sixdb_storage.acquire import S3ArchiveSource
    path = directory / 'source.tar.gz'
    receipt_path = directory / 'launch-source.json'
    previous = read(receipt_path)
    expected = job['source']['sha256']
    if path.is_symlink():
        raise ValueError('launch source was replaced by a link; preserved')
    if path.exists():
        size = path.stat().st_size
        if artifacts.sha256(path) != expected:
            raise ValueError('launch source differs from the prepared job; preserved')
    elif (previous.get('sha256') == expected and previous.get('uri') == job['uri'] + '/source.tar.gz'):
        size = previous['bytes']
    else:
        raise ValueError('launch source is missing without a recovery receipt')
    reference = {'format': 1, 'bytes': size, 'sha256': expected, 'files': 1,
                 'bucket': job['config']['bucket'], 'key': job['prefix'] + '/source.tar.gz',
                 'region': job['config']['region']}
    digest, count = hashlib.sha256(), 0
    with S3ArchiveSource(reference).open(reference) as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            count += len(chunk)
            if count > size:
                raise ValueError('uploaded launch source exceeds expected size')
            digest.update(chunk)
    if count != size or digest.hexdigest() != expected:
        raise ValueError('uploaded launch source failed independent verification; local copy preserved')
    storage.write_json(receipt_path, {'format': 1, 'kind': 'worker-launch-source',
        'uri': job['uri'] + '/source.tar.gz', 'bucket': reference['bucket'],
        'key': reference['key'], 'region': reference['region'], 'bytes': size,
        'sha256': expected, 'verified_at': time.time()})
    if path.exists() and not pinned(directory):
        if path.is_symlink() or artifacts.sha256(path) != expected:
            raise ValueError('local launch source changed during verification; preserved')
        path.unlink()
        storage.sync_directory(directory)


def collect(directory, reference, *, full=False, selected=None):
    """Publish checked members individually, preserving differing local work."""
    import stat
    directory = storage.canonical_path(directory)
    storage.manage_directory(directory.parent, root=Path(__file__).resolve().parents[2])
    selected = artifacts.selection(selected)
    if full and selected is not None:
        raise ValueError('choose full collection or selected files')

    def perform(temporary):
        if verified(directory, reference, full=full, selected=selected):
            receipt = read(directory / 'collection.json')
            receipt['accessed_at'] = time.time()
            storage.write_json(directory / 'collection.json', receipt)
            return
        print(f'Verifying/restoring local collection: {directory.name}', flush=True)
        destination = directory / 'results'
        def identity(info):
            return (info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
        before = {}
        if destination.is_dir() and not destination.is_symlink():
            for root, directories, names in os.walk(destination, followlinks=False):
                for name in names + [name for name in directories if (Path(root) / name).is_symlink()]:
                    path = Path(root) / name
                    before[path.relative_to(destination)] = identity(path.lstat())
        available = verified(directory, reference, selected=set())
        wanted = selected
        if available:
            current = read(directory / 'collection.json')['members']
            wanted = {name for name, entry in current.items()
                      if full or (name in selected if selected is not None else normally_required(name, entry))}
            if selected is not None and selected - current.keys():
                raise ValueError('selected files missing from bundle: ' + ', '.join(sorted(selected - current.keys())))
            wanted = {name for name in wanted if not (destination / name).exists()}
        staging = temporary / 'results'
        members = artifacts.restore_bundle(reference, staging, selected=wanted,
            omit_compiler=not full and wanted is None, omit_source_archive=not full and wanted is None)
        if destination.is_symlink() or destination.exists() and not destination.is_dir():
            raise ValueError('local results root was replaced; preserved')
        destination.mkdir(exist_ok=True)
        staged = artifacts.files(staging)
        for path in staged:
            name = path.relative_to(staging)
            target = destination / name
            try:
                current = identity(target.lstat())
            except FileNotFoundError:
                current = None
            if name not in before and current is not None:
                raise ValueError(f'local output appeared during fetch; preserved: {target}')
            if name in before and current != before[name]:
                raise ValueError(f'local output changed during fetch; preserved: {target}')
        # One deterministic conflict tree holds only differing authored bytes.
        # Unchanged files, extra notes and prepared-input links keep their paths.
        archive_id = hashlib.sha256(json.dumps(reference, sort_keys=True).encode()).hexdigest()[:16]
        conflicts = directory / ('replaced-results-' + archive_id)
        for path in staged:
            name = path.relative_to(staging)
            target = destination / name
            if target.resolve() != destination.resolve() / name:
                raise ValueError(f'local output path was replaced by a symlink: {target}')
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists() or target.is_symlink():
                if matches(destination, name.as_posix(), members[name.as_posix()]):
                    continue
                info = target.lstat()
                if not stat.S_ISREG(info.st_mode):
                    raise ValueError(f'nonregular local output preserved: {target}')
                checksum = artifacts.sha256(target)
                saved = conflicts / name
                if (saved.is_file() and not saved.is_symlink() and
                        artifacts.sha256(saved) != checksum):
                    saved = conflicts / '.versions' / checksum / name
                if saved.resolve() != saved.absolute():
                    raise ValueError(f'local conflict path was replaced by a symlink: {saved}')
                saved.parent.mkdir(parents=True, exist_ok=True)
                if saved.exists() or saved.is_symlink():
                    if (saved.is_symlink() or not saved.is_file() or saved.stat().st_size != info.st_size or
                            artifacts.sha256(saved) != checksum or artifacts.sha256(target) != checksum):
                        raise ValueError(f'local conflict differs; preserved: {saved}')
                    target.unlink()
                else:
                    target.rename(saved)
                    storage.sync_directory(saved.parent)
                storage.sync_directory(target.parent)
                print(f'Preserved differing local output: {saved}', flush=True)
            # Exclusive linking is an atomic no-overwrite publication of this
            # private inode; unlinking its stage leaves an editable private file.
            os.link(path, target)
            path.unlink()
            storage.sync_directory(target.parent)
        if storage.managed_root() is None and not available and conflicts.exists():
            # Legacy repair placed extra notes alongside the first conflicts.
            # Keep that discoverable layout without recopying an entire tree.
            for name in before.keys() - {Path(name) for name in members}:
                target, saved = destination / name, conflicts / name
                if name.name == '.keep-local' or not target.is_file() or target.is_symlink():
                    continue
                if identity(target.lstat()) != before[name]:
                    continue
                if saved.resolve() != saved.absolute() or saved.exists() or saved.is_symlink():
                    raise ValueError(f'local conflict differs; preserved: {saved}')
                saved.parent.mkdir(parents=True, exist_ok=True)
                target.rename(saved)
                storage.sync_directory(saved.parent)
                storage.sync_directory(target.parent)
        record_collection(directory, reference, members)
        release_launch_source(directory, reference, members)

    with storage.lock(directory / '.collection.lock'):
        if storage.managed_root() is not None:
            from sixdb_storage import preparation
            from sixdb_storage.transfers import publication
            with publication(directory) as temporary:
                preparation.run(lambda: perform(temporary))
        else:
            with tempfile.TemporaryDirectory(dir=directory, prefix='.collection-') as name:
                perform(Path(name))


def reclaim(directory, reference, *, include_results=False):
    """Remove byte-identical archived files after a fresh full S3 read.

    Explicit eviction can include closed results. Fetch restores them from the
    same receipt; local edits and pins remain protected.
    """
    with storage.lock(directory / '.collection.lock'):
        if pinned(directory) or (directory / 'results').is_symlink():
            return 0
        if include_results and read(directory / 'status.json').get('state') not in {'complete', 'failed', 'timeout', 'interrupted'}:
            return 0
        print(f'Checking archived {"results" if include_results else "compiler output"}: {directory.name}', flush=True)
        members = artifacts.remote_manifest(reference)
        destination = directory / 'results'
        candidates = {name: expected for name, expected in members.items()
                      if (include_results or expected['rebuildable']) and matches(destination, name, expected)}
        root_candidates = {name: members[name] for name in ['source.tar.gz']
                           if include_results and name in members and matches(directory, name, members[name])}
        if not candidates and not root_candidates:
            return 0
        receipt = read(directory / 'collection.json')
        evicted = set(receipt.get('evicted', [])) if receipt.get('artifact') == reference else set()
        root_evicted = receipt.get('root_evicted', {}) if receipt.get('artifact') == reference else {}
        if not isinstance(root_evicted, dict):
            root_evicted = {}
        # The recovery receipt is durable before any removal. If interrupted,
        # remaining files are still checked, and already removed ones are explicit.
        storage.write_json(directory / 'collection.json', {
            'format': 1, 'artifact': reference, 'members': members,
            'evicted': sorted(evicted | candidates.keys()),
            'root_evicted': root_evicted | {
                name: {'archive_member': name, **expected} for name, expected in root_candidates.items()},
            'accessed_at': receipt.get('accessed_at', 0), 'reclaimed_at': time.time()})
        removed = 0
        for name, expected in candidates.items():
            if pinned(directory):
                break
            if matches(destination, name, expected):
                path = destination / name
                path.unlink()
                storage.sync_directory(path.parent)
                removed += expected['bytes']
        for name, expected in root_candidates.items():
            if pinned(directory):
                break
            if matches(directory, name, expected):
                (directory / name).unlink()
                storage.sync_directory(directory)
                removed += expected['bytes']
        retained = 'recovery reference retained' if include_results else 'measurements retained'
        print(f'Reclaimed {removed / storage.GIB:.2f} GiB; {retained}: {directory.name}', flush=True)
        return removed


def evict(directory, state):
    """Explicitly evict a finished collection; never infer completion from age."""
    if not state or state.get('state') not in {'complete', 'failed', 'timeout', 'interrupted'} or not state.get('artifact'):
        raise ValueError('eviction needs a finished worker with an archived result bundle')
    if pinned(directory):
        raise ValueError('collection is pinned by .keep-local; remove the pin to allow eviction')
    storage.write_json(directory / 'artifact.json', state['artifact'])
    storage.write_json(directory / 'status.json', state)
    removed = reclaim(directory, state['artifact'], include_results=True)
    print(f'Recover: worker.py fetch {directory} --file PATH (or --full for everything)', flush=True)
    return removed


def maintain(root, *, prune=True):
    import local_cache
    return local_cache.maintain(root, prune=prune)


def preflight(root):
    storage.initialize(root)
    # Healthy launches/fetches do no online cache verification. Under pressure,
    # only verified recoverable inputs may leave; worker results stay explicit.
    try:
        storage.require_space(root / 'build')
    except OSError as error:
        if error.errno != errno.ENOSPC:
            raise
        maintain(root)
        storage.require_space(root / 'build')
