"""Small S3 compare-and-swap mailbox shared by worker and controller.

Only idle workers accept assignments. Busy workers never poll this mailbox.
See https://docs.aws.amazon.com/AmazonS3/latest/userguide/conditional-writes.html.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import uuid


class Store:
    def __init__(self, bucket, region):
        self.bucket, self.region = bucket, region

    def command(self, *args):
        return subprocess.run(['aws', 's3api', *args, '--region', self.region, '--no-cli-pager',
                               '--cli-connect-timeout', '5', '--cli-read-timeout', '10'],
                              text=True, capture_output=True, timeout=20,
                              env=os.environ | {'AWS_MAX_ATTEMPTS': '1'})

    def read(self, key):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / 'state.json'
            result = self.command('get-object', '--bucket', self.bucket, '--key', key, str(target))
            if result.returncode:
                if any(code in result.stderr for code in ('(NoSuchKey)', '(404)', '(NotFound)')):
                    return None, None
                raise RuntimeError(result.stderr.strip())
            return json.loads(target.read_text()), json.loads(result.stdout)['ETag']

    def replace(self, key, value, etag):
        """False means lost a race. An uncertain write never permits another dispatch."""
        value = value | {'revision': uuid.uuid4().hex}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'state.json'
            path.write_text(json.dumps(value, sort_keys=True) + '\n')
            condition = ['--if-match', etag] if etag else ['--if-none-match', '*']
            try:
                result = self.command('put-object', '--bucket', self.bucket, '--key', key,
                                      '--body', str(path), *condition)
            except subprocess.TimeoutExpired:
                result = None
            if result is not None and result.returncode == 0:
                return True
            # Resolve a lost response. SDK retries are disabled so a 412 can only
            # mean this attempt lost the race, not a retry of an accepted write.
            observed, _ = self.read(key)
            if observed and observed.get('revision') == value['revision']:
                return True
            if result is not None and any(code in result.stderr for code in ('(PreconditionFailed)', '(412)', '(ConditionalRequestConflict)', '(409)')):
                return False
            raise RuntimeError('Uncertain worker assignment; inspect this job before resubmitting: ' +
                               (result.stderr.strip() if result else 'S3 request timed out'))


def state_key(worker_id):
    return f'sixdb/worker-sessions/{worker_id}/state.json'


def profile(config, source):
    keys = ('region', 'bucket', 'instance_type', 'ami', 'architecture', 'disk_gb',
            'threads_per_core', 'setup', 'vpc_id', 'security_group_id', 'instance_profile', 'public_ip')
    settings = {key: config.get(key) for key in keys}
    settings['code'] = {key: source[key] for key in ('runtime_sha256', 'pool_sha256', 'setup_sha256')}
    return hashlib.sha256(json.dumps(settings, sort_keys=True).encode()).hexdigest()


def can_claim(state, job, now=None):
    now = time.time() if now is None else now
    config = job['config']
    capacities = {'spot', 'on-demand'} if config['capacity'] == 'spot-or-on-demand' else {config['capacity']}
    return (state.get('state') == 'idle' and state['profile'] == job['profile']
            and state['capacity'] in capacities
            and state['subnet_id'] in {s['id'] for s in config['subnets']}
            and now < state['idle_until']
            and now + config['deadline_seconds'] < state['max_end'])
