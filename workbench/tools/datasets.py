#!/usr/bin/env python3
"""Resolve reusable inputs and cache prepared variants; publish only when asked."""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import urllib.parse
import urllib.request
import storage
from sixdb_storage import preparation

ROOT = Path(__file__).resolve().parents[2]


def cache_root():
    storage.initialize(ROOT)
    managed = storage.managed_root()
    default = managed / 'datasets' if managed is not None else ROOT / 'build/datasets'
    path = Path(os.environ.get('SIXDB_DATA_CACHE', default)).expanduser()
    return storage.manage_directory(path).resolve()


def digest(path):
    h = hashlib.sha256()
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as f:
        if not stat.S_ISREG(os.fstat(f.fileno()).st_mode):
            raise ValueError(f'Input must be a regular file: {path}')
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


@contextmanager
def locked(key, *, blocking=True, _root=None):
    directory = (_root if _root is not None else cache_root()) / '.locks'
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / (key + '.lock')).open('a') as f:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | (0 if blocking else fcntl.LOCK_NB))
        except BlockingIOError:
            yield False
            return
        yield True


def _cache_directory(name):
    root = cache_root()
    directory = root / name
    directory.mkdir(parents=True, exist_ok=True)
    for kind in ('downloads', 'prepared'):
        pending = root / kind
        if pending.exists():
            preparation.reconcile(pending, lambda key, **kw: locked(key, _root=root, **kw))
    return directory


def file_hashes(directory):
    if not stat.S_ISDIR(directory.lstat().st_mode):
        raise ValueError(f'Prepared input must be a directory: {directory}')
    result = {}
    for p in sorted(directory.rglob('*')):
        mode = p.lstat().st_mode
        if not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
            raise ValueError(f'Prepared inputs must be regular files: {p}')
        if p.is_file() and p != directory / 'prepared.json':
            result[str(p.relative_to(directory))] = digest(p)
    return result


def verify(directory):
    # Recovered runs link their input root to one shared prepared variant.
    # Members remain regular files; only this root alias is resolved.
    directory = Path(directory).resolve(strict=True)
    meta = json.loads((directory / 'prepared.json').read_text())
    if meta.get('format') != 2:
        raise ValueError('Resolve this input again with the current preparation format')
    if identity(meta['request']) != meta['key'] or file_hashes(directory) != meta['files_sha256']:
        raise ValueError(f'Prepared input changed: {directory}')
    if identity({'request': meta['request'], 'files': meta['files_sha256'], 'details': meta['details']}) != meta['id']:
        raise ValueError(f'Prepared identity changed: {directory}')
    return meta


def source(spec):
    """Reuse a verified old download or existing S3/HTTP object by its hash."""
    checksum = spec['sha256']
    if (not isinstance(checksum, str) or not preparation.KEY.fullmatch(checksum) or
            type(spec['bytes']) is not int or spec['bytes'] < 0):
        raise ValueError('Source needs a pinned SHA-256 and nonnegative byte count')
    directory = _cache_directory('downloads')
    path = directory / checksum
    with locked(checksum):
        if os.path.lexists(path):
            if digest(path) != checksum or path.stat().st_size != spec['bytes']:
                raise ValueError(f'Cached download changed: {path}')
            return path
        with preparation.stage(directory, checksum) as temp:
            partial = temp / 'download'
            def download():
                legacy = ROOT / spec.get('legacy_cache', 'build/datasets/no-legacy-input')
                if legacy.is_file() and digest(legacy) == checksum:
                    with legacy.open('rb') as src:
                        _copy_source(src, partial, spec)
                elif spec['url'].startswith('s3://'):
                    from sixdb_storage.acquire import S3ArchiveSource
                    url = urllib.parse.urlsplit(spec['url'])
                    reference = {'format': 1, 'sha256': checksum, 'bytes': spec['bytes'], 'files': 1,
                                 'bucket': url.netloc, 'key': url.path.lstrip('/'),
                                 'region': spec.get('region', os.environ.get('AWS_REGION', 'us-east-1'))}
                    with S3ArchiveSource(reference).open(reference) as src:
                        _copy_source(src, partial, spec)
                else:
                    print(f'Download: {spec["url"]}', file=sys.stderr)
                    with urllib.request.urlopen(spec['url'], timeout=120) as src:
                        _copy_source(src, partial, spec)
            preparation.run(download)
            partial.rename(path)
            storage.sync_directory(directory)
    return path


def _copy_source(src, path, spec):
    """Enforce pinned bytes while writing, including an oversized HTTP/S3 body."""
    remaining, checksum = spec['bytes'], hashlib.sha256()
    with path.open('xb') as dst:
        while True:
            chunk = src.read(min(1024 * 1024, remaining + 1))
            if not chunk:
                break
            if len(chunk) > remaining:
                raise ValueError(f'Download exceeds pinned bytes: {spec["url"]}')
            dst.write(chunk)
            checksum.update(chunk)
            remaining -= len(chunk)
        if remaining or checksum.hexdigest() != spec['sha256']:
            raise ValueError(f'Download does not match pinned bytes: {spec["url"]}')
        dst.flush()
        os.fsync(dst.fileno())


def cached(name, recipe, inputs, parameters, build, reference=None):
    """Cache a plain Python preparation function. Recipes need no registration."""
    request = {'format': 2, 'name': name, 'recipe_sha256': digest(recipe), 'inputs': inputs, 'parameters': parameters}
    key = identity(request)
    directory = _cache_directory('prepared')
    target = directory / key
    if not os.path.lexists(target) and reference and reference.exists():
        ref = json.loads(reference.read_text())
        if ref['key'] != key:
            raise ValueError('Prepared reference names a different recipe')
        try:
            return restore(ref)
        except (RuntimeError, FileNotFoundError) as exc:
            # Public preparation remains usable without AWS credentials or CLI.
            # Integrity errors are ValueError and deliberately remain visible.
            print(f'Prepared mirror unavailable; preparing from pinned sources: {exc}', file=sys.stderr)
    with locked(key):
        if os.path.lexists(target):
            if verify(target)['key'] != key:
                raise ValueError('Cached variant belongs to a different request')
            return target
        with preparation.stage(directory, key) as temp:
            staging = temp / 'prepared'
            staging.mkdir()
            details = preparation.run(lambda: build(staging))
            hashes = file_hashes(staging)
            meta = {'format': 2, 'request': request, 'key': key, 'details': details,
                    'files_sha256': hashes, 'id': identity({'request': request, 'files': hashes, 'details': details})}
            (staging / 'prepared.json').write_text(json.dumps(meta, indent=2, sort_keys=True) + '\n')
            storage.sync_tree(staging)
            staging.rename(target)
            storage.sync_directory(directory)
    return target


def get(name, **parameters):
    home = ROOT / 'workbench/datasets' / name
    if home.parent != ROOT / 'workbench/datasets':
        raise ValueError('Use a dataset name from the catalog')
    spec = json.loads((home / 'dataset.json').read_text())
    recipe = home / 'prepare.py'
    module_spec = importlib.util.spec_from_file_location('dataset_recipe', recipe)
    if module_spec is None or module_spec.loader is None:
        raise ValueError(f'Cannot load dataset recipe: {recipe}')
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    unknown = parameters.keys() - module.DEFAULTS.keys()
    if unknown:
        raise ValueError(f'Unknown preparation parameters: {sorted(unknown)}')
    parameters = module.DEFAULTS | parameters
    inputs = {k: v['sha256'] for k, v in spec['sources'].items()}
    request = {'format': 2, 'name': name, 'recipe_sha256': digest(recipe), 'inputs': inputs, 'parameters': parameters}
    def build(output):
        paths = {k: source(v) for k, v in spec['sources'].items()}
        print(f'Prepare: {name} {parameters}', file=sys.stderr)
        return module.prepare(paths, output, **parameters)
    return cached(name, recipe, inputs, parameters, build, home / 'prepared' / (identity(request) + '.json'))


def publish(directory):
    """Retain a prepared variant once. This is also used when retaining a run."""
    import artifacts
    directory = Path(directory)
    meta = verify(directory)
    references = cache_root() / 'references'
    references.mkdir(parents=True, exist_ok=True)
    path = references / (meta['id'] + '.json')
    with locked(meta['id']):
        if path.exists():
            ref = json.loads(path.read_text())
            if ref['id'] != meta['id'] or ref['key'] != meta['key']:
                raise ValueError('Published reference belongs to a different input')
            return ref
        if storage.managed_root() is not None:
            # The configured publisher owns one restartable staging scope.
            artifact = artifacts.publish(directory, references)
        else:
            with tempfile.TemporaryDirectory(dir=references) as temp:
                artifact = artifacts.publish(directory, Path(temp))
        ref = {'format': 1, 'id': meta['id'], 'key': meta['key'], 'artifact': artifact}
        temporary = path.with_suffix('.tmp')
        temporary.write_text(json.dumps(ref, indent=2, sort_keys=True) + '\n')
        temporary.replace(path)
    return ref


def restore(ref):
    import artifacts
    # Validate keys before using them as cache paths.
    if any(len(ref[k]) != 64 or any(c not in '0123456789abcdef' for c in ref[k]) for k in ('id', 'key')):
        raise ValueError('Invalid prepared-input identity')
    directory = _cache_directory('prepared')
    target = directory / ref['key']
    with locked(ref['key']):
        if not os.path.lexists(target):
            with preparation.stage(directory, ref['key']) as temp:
                staging = temp / 'prepared'
                preparation.run(lambda: (artifacts.restore_bundle(ref['artifact'], staging), None)[1])
                meta = verify(staging)
                if meta['id'] != ref['id'] or meta['key'] != ref['key']:
                    raise ValueError('Restored input has a different identity')
                storage.sync_tree(staging)
                staging.rename(target)
                storage.sync_directory(directory)
        if verify(target)['id'] != ref['id']:
            raise ValueError('Cached input has a different identity')
    return target


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=('list', 'get', 'publish', 'fetch'))
    p.add_argument('name', nargs='?')
    p.add_argument('--param', action='append', default=[], metavar='NAME=JSON')
    args = p.parse_args()
    if args.command == 'list':
        for path in sorted((ROOT / 'workbench/datasets').glob('*/dataset.json')):
            print(path.parent.name)
        return
    if not args.name:
        p.error('A dataset name is required')
    if args.command == 'fetch':
        print(restore(json.loads(Path(args.name).read_text())))
        return
    parameters = {k: json.loads(v) for k, v in (item.split('=', 1) for item in args.param)}
    directory = get(args.name, **parameters)
    if args.command == 'publish':
        ref = publish(directory)
        destination = ROOT / 'workbench/datasets' / args.name / 'prepared' / (ref['key'] + '.json')
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(ref, indent=2, sort_keys=True) + '\n')
        print(destination)
    else:
        print(directory)


if __name__ == '__main__':
    main()
