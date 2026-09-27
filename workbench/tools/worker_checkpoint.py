#!/usr/bin/env python3
"""Committed, chunked worker checkpoints; partial uploads never replace recovery state."""
from __future__ import annotations

import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
import time
import uuid

CHUNK_BYTES = 32 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def identity(job):
    return {key: job[key] for key in ('script', 'args')} | {
        'source_digest': job['source']['digest'], 'env': job['config']['env'],
        'checkpoint_script': job['config']['checkpoint_script']}


class Store:
    def __init__(self, region):
        self.region = region

    def command(self, operation, uri, *args):
        bucket, _, key = uri.removeprefix('s3://').partition('/')
        if not uri.startswith('s3://') or not bucket or not key:
            raise ValueError('expected S3 checkpoint URI')
        return subprocess.run(['aws', 's3api', operation, '--bucket', bucket, '--key', key,
            *args, '--region', self.region, '--no-cli-pager', '--output', 'json',
            '--cli-connect-timeout', '5', '--cli-read-timeout', '20'],
            capture_output=True, text=True, check=True)

    def get(self, uri):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'data'
            self.command('get-object', uri, str(path))
            return path.read_bytes()

    def put(self, uri, data, *, immutable=True):
        checksum = base64.b64encode(hashlib.sha256(data).digest()).decode()
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'data'
            path.write_bytes(data)
            try:
                response = self.command('put-object', uri, '--body', str(path),
                    '--checksum-algorithm', 'SHA256', '--checksum-sha256', checksum,
                    *(['--if-none-match', '*'] if immutable else []))
                if json.loads(response.stdout).get('ChecksumSHA256') != checksum:
                    raise ValueError('S3 did not confirm the checkpoint checksum')
            except subprocess.CalledProcessError as error:
                if not immutable or '(PreconditionFailed)' not in error.stderr:
                    raise
                response = self.command('head-object', uri, '--checksum-mode', 'ENABLED')
                meta = json.loads(response.stdout)
                if meta.get('ChecksumSHA256') != checksum or meta['ContentLength'] != len(data):
                    raise ValueError('existing checkpoint object has different contents')


def load(store, reference):
    data = store.get(reference['uri'])
    if sha(data) != reference['sha256']:
        raise ValueError('checkpoint manifest checksum mismatch')
    value = json.loads(data)
    if value['format'] != 1:
        raise ValueError('unsupported checkpoint format')
    return value


def publish(store, directory, job, reason, previous=None):
    """The producer has finished writing directory and will not mutate it."""
    known = {}
    if previous:
        prior = load(store, previous)
        if prior['identity'] != identity(job):
            raise ValueError('checkpoint identity changed')
        known = {c['sha256']: c for f in prior['files'] for c in f['chunks']}
    prefix = job['uri'] + '/checkpoints'
    files, directories, uploaded = [], [], 0
    members = sorted(directory.rglob('*'))
    for path in members:
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f'checkpoint must contain ordinary files: {path}')
        if path.is_dir():
            directories.append(str(path.relative_to(directory)))
            continue
        before = path.stat()
        chunks = []
        with path.open('rb') as stream:
            while data := stream.read(CHUNK_BYTES):
                checksum = sha(data)
                chunk = known.get(checksum)
                if chunk is None:
                    chunk = {'sha256': checksum, 'bytes': len(data),
                             'uri': prefix + '/chunks/' + checksum}
                    store.put(chunk['uri'], data)
                    known[checksum] = chunk
                    uploaded += len(data)
                chunks.append(chunk)
        if (path.stat().st_size, path.stat().st_mtime_ns) != (before.st_size, before.st_mtime_ns):
            raise ValueError(f'checkpoint changed during collection: {path}')
        files.append({'path': str(path.relative_to(directory)), 'bytes': before.st_size,
                      'mode': before.st_mode & 0o777, 'chunks': chunks})
    if sorted(directory.rglob('*')) != members:
        raise ValueError('checkpoint members changed during collection')
    if not files:
        raise ValueError('empty checkpoint; previous generation retained')
    manifest = {'format': 1, 'job': job['id'], 'created_at': time.time(), 'reason': reason,
                'identity': identity(job), 'files': files, 'directories': directories}
    data = (json.dumps(manifest, sort_keys=True) + '\n').encode()
    reference = {'uri': prefix + '/generations/' + uuid.uuid4().hex + '.json',
                 'sha256': sha(data), 'created_at': manifest['created_at'], 'reason': reason,
                 'bytes': sum(f['bytes'] for f in files), 'uploaded_bytes': uploaded,
                 'files': len(files)}
    store.put(reference['uri'], data)
    # The only mutable object: publish after all data and its manifest succeeded.
    store.put(prefix + '/latest.json', (json.dumps(reference) + '\n').encode(), immutable=False)
    return reference


def restore(store, reference, destination, job):
    manifest = load(store, reference)
    if manifest['identity'] != identity(job):
        raise ValueError('checkpoint source/script/arguments/environment mismatch')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise ValueError('restore destination already exists')
    with tempfile.TemporaryDirectory(dir=destination.parent) as temp:
        root = Path(temp) / 'restored'
        root.mkdir()
        for directory in manifest.get('directories', []):
            name = PurePosixPath(directory)
            if not name.parts or name.is_absolute() or '..' in name.parts:
                raise ValueError('unsafe checkpoint directory path')
            root.joinpath(*name.parts).mkdir(parents=True, exist_ok=True)
        for entry in manifest['files']:
            name = PurePosixPath(entry['path'])
            if not name.parts or name.is_absolute() or '..' in name.parts:
                raise ValueError('unsafe checkpoint file path')
            path = root.joinpath(*name.parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('xb') as stream:
                for chunk in entry['chunks']:
                    data = store.get(chunk['uri'])
                    if len(data) != chunk['bytes'] or sha(data) != chunk['sha256']:
                        raise ValueError('checkpoint chunk checksum mismatch')
                    stream.write(data)
            if path.stat().st_size != entry['bytes']:
                raise ValueError('checkpoint file size mismatch')
            path.chmod(entry['mode'] & 0o777)
        root.rename(destination)
    return manifest


def main():
    # Worker launches this in an isolated process group with an overall time budget.
    operation, job_file, target = sys.argv[1:4]
    job = json.loads(Path(job_file).read_text())
    store = Store(job['config']['region'])
    target = Path(target)
    if operation == 'restore':
        restore(store, job['resume']['checkpoint'], target, job)
        return
    reason, receipt_file = sys.argv[4:6]
    previous = json.loads(Path(receipt_file).read_text()) if Path(receipt_file).exists() else job.get('resume', {}).get('checkpoint')
    with tempfile.TemporaryDirectory(dir=target) as temp:
        snapshot = Path(temp) / 'snapshot'
        snapshot.mkdir()
        env = os.environ | {'SIXDB_CHECKPOINT_REASON': reason}
        subprocess.run(['bash', job['config']['checkpoint_script'], str(snapshot)],
                       env=env, check=True)
        reference = publish(store, snapshot, job, reason, previous)
    receipt = Path(receipt_file)
    temporary = receipt.with_suffix('.tmp')
    temporary.write_text(json.dumps(reference, indent=2) + '\n')
    temporary.replace(receipt)


if __name__ == '__main__':
    main()
