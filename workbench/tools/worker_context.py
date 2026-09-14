"""Optional startup rendezvous and progress for worker scripts; no background polling."""
import json
import os
from pathlib import Path
import re
import sys
import subprocess
import tempfile
import time

import worker


def label(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_.-]+', value) or value in {'.', '..'}:
        raise ValueError('rendezvous labels use letters, digits, dot, underscore or hyphen')
    return value


def put(aws, uri, value):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / 'value.json'
        worker.save(path, value)
        aws.upload(path, uri, timeout=20)


def read_json(aws, bucket, key, *, timeout=20):
    end = time.monotonic() + timeout
    for attempt in range(3):
        try:
            data, _ = aws.get_object(bucket, key,
                timeout=max(.01, min(8, end - time.monotonic())))
            return json.loads(data) if data is not None else None
        except (worker.AwsError, subprocess.TimeoutExpired) as error:
            if isinstance(error, worker.AwsError) and error.code not in {
                    'TransportError', 'SlowDown', 'RequestTimeout', 'InternalError', 'ServiceUnavailable'}:
                raise  # Permission errors are not an absent peer.
            remaining = end - time.monotonic()
            if attempt == 2 or remaining <= 0:
                raise
            time.sleep(min(.25 * 2**attempt, remaining))


class GroupContext:
    def __init__(self, job, results, manifest, aws=None):
        self.job, self.results, self.manifest = job, Path(results), manifest
        self.config = job['config']
        self.member = self.config['env']['SIXDB_GROUP_MEMBER']
        self.uri = self.config['env']['SIXDB_GROUP_URI']
        self.prefix = self.uri.removeprefix('s3://' + self.config['bucket'] + '/')
        if (manifest['uri'] != self.uri or self.uri != f's3://{self.config["bucket"]}/sixdb/worker-groups/{manifest["id"]}' or
                manifest['members'][self.member]['job'] != job['id']):
            raise ValueError('group/job identity differs from the rendezvous manifest')
        self.aws = aws or worker.Aws(self.config['region'])

    @classmethod
    def from_env(cls):
        results = Path(os.environ['SIXDB_RESULTS'])
        job = json.loads((results / 'job.json').read_text())
        config = job['config']
        uri = config['env']['SIXDB_GROUP_URI']
        prefix = uri.removeprefix('s3://' + config['bucket'] + '/')
        aws = worker.Aws(config['region'])
        manifest = read_json(aws, config['bucket'], prefix + '/manifest.json')
        if manifest is None:
            raise ValueError('group manifest is unavailable; launch with current worker-group tools')
        context = cls(job, results, manifest, aws)
        worker.save(results / 'group-manifest.json', manifest)
        return context

    def read(self, suffix, *, timeout=20):
        return read_json(self.aws, self.config['bucket'], self.prefix + '/' + suffix, timeout=timeout)

    def progress(self, phase, **detail):
        """Publish between measured phases. Values are descriptions, not numerical results."""
        record = detail | {'phase': phase, 'job': self.job['id'], 'member': self.member, 'updated_at': time.time()}
        path = self.results / 'progress.json'
        try:
            worker.save(path, record)
            self.aws.upload(path, self.uri + '/members/' + self.member + '/progress.json', timeout=5)
        except Exception as error:
            record['publication_error'] = str(error)
            try:
                worker.save(path, record)
            except OSError:
                pass
            print(f'Progress publication failed ({phase}): {error}', file=sys.stderr, flush=True)
        return record

    def publish(self, name, value):
        record = {'job': self.job['id'], 'member': self.member, 'value': value, 'updated_at': time.time()}
        put(self.aws, self.uri + '/values/' + label(name) + '/' + self.member + '.json', record)

    def ready(self, value=None):
        """The caller decides what ready means, e.g. a checked listening endpoint."""
        self.publish('ready', value)
        self.progress('ready')

    def wait_ready(self, members=None, *, timeout=300):
        return self.wait_values('ready', members, timeout=timeout, require_live=True)

    def wait_values(self, name, members=None, *, timeout=300, require_live=False):
        """Wait for selected peers; a known failed prerequisite ends the wait promptly."""
        label(name)
        members = list(self.manifest['members'] if members is None else members)
        if not members or len(set(members)) != len(members) or any(
                m not in self.manifest['members'] and m != 'controller' for m in members):
            raise ValueError('choose distinct members from this group (or controller)')
        if timeout <= 0:
            raise ValueError('readiness timeout must be positive')
        end = min(time.monotonic() + timeout, time.monotonic() + max(0,
            self.job['created_at'] + self.config['deadline_seconds'] - time.time()))
        missing, last_missing = members, None
        values = {}
        try:
            while True:
                remaining = end - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f'{name}: waiting for {", ".join(missing)}')
                control = self.read('control.json', timeout=min(15, remaining)) or {}
                if control.get('aborted') or control.get('launch_error'):
                    raise RuntimeError('group stopped: ' + (control.get('launch_error') or 'cancelled by controller'))
                for member in members if require_live else missing:
                    remaining = end - time.monotonic()
                    if remaining <= 0:
                        break
                    value = self.read(f'values/{name}/{member}.json', timeout=min(15, remaining))
                    expected = self.manifest['members'][member]['job'] if member != 'controller' else self.manifest['id']
                    if value is not None:
                        if value.get('job') != expected or value.get('member') != member:
                            raise ValueError(f'{name}: stale or mismatched value from {member}')
                        values[member] = value['value']
                    if member != 'controller' and (value is None or require_live):
                        status = read_json(self.aws, self.config['bucket'], f'sixdb/workers/{expected}/status.json',
                            timeout=min(15, max(.01, end-time.monotonic()))) or {}
                        if status.get('state') in worker.FINAL | {'uploading'}:
                            phase = status.get('failure_phase', status['state'])
                            raise RuntimeError(f'required peer {member} ({expected}) ended during {phase} while waiting for {name}: '
                                               + status.get('error', status['state'])
                                               + f'; inspect worker.py logs {expected}'
                                               + (' --file setup.log' if phase == 'setting-up' else ''))
                missing = [m for m in members if m not in values]
                if time.monotonic() >= end:
                    raise TimeoutError(f'{name}: deadline elapsed checking {", ".join(missing or members)}')
                if not missing:
                    self.progress('ready', rendezvous=name, waiting_for=[])
                    return values
                if missing != last_missing:
                    self.progress('waiting', rendezvous=name, waiting_for=missing)
                    last_missing = missing
                time.sleep(min(1, max(0, end-time.monotonic())))
        except Exception as error:
            # Keep the original cause if publication itself is unavailable.
            try:
                self.progress('failed', rendezvous=name, waiting_for=missing, error=str(error))
            except Exception:
                pass
            raise
