#!/usr/bin/env python3
"""Run repository scripts on temporary EC2 workers and recover their results."""
from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile
import time
from typing import Any
import uuid

from experiment import source_files
import artifacts
import worker_pool as pool

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
JOBS = ROOT / 'build/workers'
CAPACITY_ERRORS = {'InsufficientInstanceCapacity', 'InsufficientFreeAddressesInSubnet',
                   'UnfulfillableCapacity', 'Unsupported', 'SpotMaxPriceTooLow'}
FINAL = {'complete', 'failed', 'timeout', 'interrupted', 'upload-failed'}
RESERVED = {'SIXDB_RESULTS', 'SIXDB_SOURCE', 'SIXDB_JOB', 'SIXDB_RESULTS_S3',
            'SIXDB_SOURCE_COMMIT', 'SIXDB_WORKER_ID', 'SIXDB_WORKER_REUSED', 'AWS_REGION', 'AWS_DEFAULT_REGION'}


class AwsError(RuntimeError):
    def __init__(self, message):
        super().__init__(message)
        match = re.search(r'\(([^)]+)\) when calling', message)
        self.code = match[1] if match else 'TransportError'


class Aws:
    def __init__(self, region):
        self.region = region

    def call(self, service, operation, *, timeout=None, **parameters):
        # Structured JSON keeps script arguments and environment out of shell parsing.
        with tempfile.NamedTemporaryFile('w', suffix='.json') as request:
            json.dump(parameters, request)
            request.flush()
            result = subprocess.run(['aws', service, operation, '--cli-input-json',
                'file://' + request.name, '--region', self.region, '--output', 'json',
                '--no-cli-pager', '--cli-connect-timeout', '10', '--cli-read-timeout', '30'],
                text=True, capture_output=True, timeout=timeout)
        if result.returncode:
            raise AwsError(result.stderr.strip())
        return json.loads(result.stdout or '{}')

    def upload(self, path, uri):
        subprocess.run(['aws', 's3', 'cp', '--only-show-errors', str(path), uri,
                        '--region', self.region, '--no-cli-pager'], check=True)

    def get_object(self, bucket, key, *, timeout=None):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / 'object'
            # get-object's output filename is positional in the CLI.
            result = subprocess.run(['aws', 's3api', 'get-object', '--bucket', bucket,
                '--key', key, str(target), '--region', self.region, '--no-cli-pager', '--output', 'json'],
                text=True, capture_output=True, timeout=timeout)
            if result.returncode:
                error = AwsError(result.stderr.strip())
                if error.code in {'NoSuchKey', '404', 'NotFound'}:
                    return None, {}
                raise error
            return target.read_bytes(), json.loads(result.stdout)

    def get_bytes(self, bucket, key):
        return self.get_object(bucket, key)[0]

    def get_json(self, bucket, key):
        data = self.get_bytes(bucket, key)
        return json.loads(data) if data is not None else None


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    temporary.replace(path)


def environment(values):
    output = {}
    for value in values:
        key, sep, content = value.partition('=')
        if not sep or not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', key) or key in RESERVED:
            raise ValueError(f'invalid or reserved environment assignment: {key}')
        if '\0' in content:
            raise ValueError('environment values cannot contain NUL')
        output[key] = content
    return output


def configuration(args, defaults):
    config = dict(defaults)
    for key in ('machine', 'capacity', 'deadline_seconds', 'disk_gb', 'setup', 'sync_seconds', 'idle_seconds', 'max_age_seconds'):
        if getattr(args, key, None) is not None:
            config[key] = getattr(args, key)
    config.update(config['machines'][config['machine']])
    del config['machines']
    for key in ('instance_type', 'ami'):
        if getattr(args, key, None):
            config[key] = getattr(args, key)
    config['fresh'] = getattr(args, 'fresh', False) or config.get('fresh', False)
    config.setdefault('idle_seconds', 300)
    config.setdefault('max_age_seconds', 14400)
    config['env'] = dict(config.get('env', {})) | environment(getattr(args, 'env', []))
    environment([f'{k}={v}' for k, v in config['env'].items()])
    config['env'] = {k: str(v) for k, v in config['env'].items()}
    if config['capacity'] not in {'spot', 'on-demand', 'spot-or-on-demand'}:
        raise ValueError('unknown capacity choice')
    if config['setup'] not in {'toolchain', 'minimal'}:
        raise ValueError('setup must be toolchain or minimal')
    if not 120 <= config['deadline_seconds'] <= 86400:
        raise ValueError('deadline must be 120..86400 seconds, including setup and upload')
    if config['disk_gb'] < 8 or config['sync_seconds'] < 0:
        raise ValueError('disk must be at least 8 GiB and sync interval nonnegative')
    if not 0 <= config['idle_seconds'] <= 3600:
        raise ValueError('idle window must be 0..3600 seconds')
    if not config['deadline_seconds'] <= config['max_age_seconds'] <= 172800:
        raise ValueError('maximum worker age must cover the job deadline and be at most 48 hours')
    return config


def resolve(config, aws):
    config = dict(config)
    hardware = aws.call('ec2', 'describe-instance-types', InstanceTypes=[config['instance_type']])['InstanceTypes'][0]
    image = aws.call('ec2', 'describe-images', ImageIds=[config['ami']])['Images'][0]
    if image['State'] != 'available' or image['Architecture'] not in hardware['ProcessorInfo']['SupportedArchitectures']:
        raise ValueError('AMI is unavailable or incompatible with instance architecture')
    config['hardware'] = {k: hardware[k] for k in ('VCpuInfo', 'MemoryInfo', 'ProcessorInfo')}
    config['architecture'] = image['Architecture']
    config['root_device'] = image['RootDeviceName']
    config['image_name'] = image['Name']
    if config.get('threads_per_core') not in (None, hardware['VCpuInfo']['DefaultThreadsPerCore']):
        if config['threads_per_core'] not in hardware['VCpuInfo'].get('ValidThreadsPerCore', []):
            raise ValueError('threads_per_core is unsupported for this instance type')
    if not config.get('vpc_id'):
        vpcs = aws.call('ec2', 'describe-vpcs', Filters=[{'Name': 'is-default', 'Values': ['true']}])['Vpcs']
        if len(vpcs) != 1:
            raise ValueError('set vpc_id and subnets in your worker configuration')
        config['vpc_id'] = vpcs[0]['VpcId']
    subnets = aws.call('ec2', 'describe-subnets', Filters=[{'Name': 'vpc-id', 'Values': [config['vpc_id']]}])['Subnets']
    offers = aws.call('ec2', 'describe-instance-type-offerings', LocationType='availability-zone',
        Filters=[{'Name': 'instance-type', 'Values': [config['instance_type']]}])['InstanceTypeOfferings']
    zones = {offer['Location'] for offer in offers}
    selected = config.get('subnets')
    config['subnets'] = sorted([{'id': s['SubnetId'], 'zone': s['AvailabilityZone']} for s in subnets
        if s['AvailabilityZone'] in zones and s['AvailableIpAddressCount'] > 0
        and (s['SubnetId'] in selected if selected else s['MapPublicIpOnLaunch'])], key=lambda s: s['zone'])
    if not config['subnets']:
        raise ValueError('no selected subnet offers this instance type')
    return config


def ensure_infrastructure(config, aws):
    """Provision only SixDB-owned, idle-cost-free plumbing; explicit resources bypass this."""
    if not config.get('security_group_id'):
        groups = aws.call('ec2', 'describe-security-groups', Filters=[
            {'Name': 'vpc-id', 'Values': [config['vpc_id']]},
            {'Name': 'group-name', 'Values': ['sixdb-worker']}])['SecurityGroups']
        if groups:
            config['security_group_id'] = groups[0]['GroupId']
        else:
            try:
                config['security_group_id'] = aws.call('ec2', 'create-security-group',
                    GroupName='sixdb-worker', Description='SixDB disposable workers; no inbound services',
                    VpcId=config['vpc_id'], TagSpecifications=[{'ResourceType': 'security-group',
                        'Tags': [{'Key': 'Project', 'Value': 'SixDB'}]}])['GroupId']
            except AwsError as error:
                if error.code != 'InvalidGroup.Duplicate':
                    raise
                return ensure_infrastructure(config, aws)
    if config.get('instance_profile'):
        return
    bucket = config['bucket']
    # A settings overlay for another result bucket must not revoke access from
    # an already-running job using the default profile.
    role = 'sixdb-worker' if bucket == artifacts.BUCKET else 'sixdb-worker-' + hashlib.sha256(bucket.encode()).hexdigest()[:8]
    trust = {'Version': '2012-10-17', 'Statement': [{'Effect': 'Allow',
        'Principal': {'Service': 'ec2.amazonaws.com'}, 'Action': 'sts:AssumeRole'}]}
    try:
        aws.call('iam', 'create-role', RoleName=role, AssumeRolePolicyDocument=json.dumps(trust),
                 Tags=[{'Key': 'Project', 'Value': 'SixDB'}])
    except AwsError as error:
        if error.code != 'EntityAlreadyExists':
            raise
    buckets = sorted({bucket, artifacts.BUCKET})
    policy = {'Version': '2012-10-17', 'Statement': [
        {'Effect': 'Allow', 'Action': ['s3:ListBucket', 's3:GetBucketLocation'],
         'Resource': [f'arn:aws:s3:::{b}' for b in buckets]},
        {'Effect': 'Allow', 'Action': 's3:GetObject', 'Resource': [f'arn:aws:s3:::{b}/*' for b in buckets]},
        {'Effect': 'Allow', 'Action': ['s3:PutObject', 's3:AbortMultipartUpload'],
         'Resource': [f'arn:aws:s3:::{b}/sixdb/*' for b in buckets]}]}
    aws.call('iam', 'put-role-policy', RoleName=role, PolicyName='sixdb-worker-storage', PolicyDocument=json.dumps(policy))
    try:
        aws.call('iam', 'create-instance-profile', InstanceProfileName=role,
                 Tags=[{'Key': 'Project', 'Value': 'SixDB'}])
    except AwsError as error:
        if error.code != 'EntityAlreadyExists':
            raise
    profile = aws.call('iam', 'get-instance-profile', InstanceProfileName=role)['InstanceProfile']
    if not profile['Roles']:
        aws.call('iam', 'add-role-to-instance-profile', InstanceProfileName=role, RoleName=role)
    elif [r['RoleName'] for r in profile['Roles']] != [role]:
        raise ValueError('sixdb-worker profile has an unexpected role')
    config['instance_profile'] = role


def snapshot(directory):
    sources = source_files(ROOT)
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in sources.items()}
    path = directory / 'source.tar.gz'
    with path.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', mtime=0, filename='') as zipped:
        with tarfile.open(fileobj=zipped, mode='w|') as archive:
            for name, data in sources.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o755 if (ROOT / name).stat().st_mode & 0o111 else 0o644
                archive.addfile(info, io.BytesIO(data))
    runtime = sources['workbench/tools/worker_runtime.py']
    (directory / 'runtime.py').write_bytes(runtime)
    (directory / 'worker_pool.py').write_bytes(sources['workbench/tools/worker_pool.py'])
    return {'sha256': artifacts.sha256(path), 'files': hashes,
            'pool_sha256': hashes['workbench/tools/worker_pool.py'],
            'setup_sha256': hashes['workbench/tools/worker-setup.sh'],
            'digest': hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest(),
            'runtime_sha256': hashlib.sha256(runtime).hexdigest()}


def boot_script(job):
    # The OS shutdown backstop exists before package installation or source download.
    config = job['config']
    encoded = base64.b64encode(json.dumps(job).encode()).decode()
    region = shlex.quote(config['region'])
    uri = shlex.quote(job['uri'] + '/runtime.py')
    pool_uri = shlex.quote(job['uri'] + '/worker_pool.py')
    max_age = config.get('max_age_seconds', config['deadline_seconds'])
    script = f'''#!/bin/bash
set -euo pipefail
mkdir -p /opt/sixdb
exec > >(tee -a /opt/sixdb/bootstrap.log) 2>&1
systemd-run --unit=sixdb-deadline --on-active={max_age}s /sbin/shutdown -h now
systemd-run --unit=sixdb-boot-deadline --on-active={config['deadline_seconds']}s /sbin/shutdown -h now
trap '/sbin/shutdown -h now' EXIT
echo {shlex.quote(encoded)} | base64 -d > /opt/sixdb/job.json
export DEBIAN_FRONTEND=noninteractive AWS_DEFAULT_REGION={region} AWS_REGION={region}
apt-get update -qq
apt-get install -y -qq curl unzip ca-certificates python3 git
if ! command -v aws >/dev/null; then
  curl --fail --retry 3 -sS "https://awscli.amazonaws.com/awscli-exe-linux-$(uname -m).zip" -o /opt/sixdb/aws.zip
  unzip -q /opt/sixdb/aws.zip -d /opt/sixdb/aws-install
  /opt/sixdb/aws-install/aws/install
  rm -rf /opt/sixdb/aws.zip /opt/sixdb/aws-install
fi
aws s3 cp --only-show-errors {uri} /opt/sixdb/runtime.py
echo {shlex.quote(job['source']['runtime_sha256'] + '  /opt/sixdb/runtime.py')} | sha256sum -c -
aws s3 cp --only-show-errors {pool_uri} /opt/sixdb/worker_pool.py
echo {shlex.quote(job['source'].get('pool_sha256', '') + '  /opt/sixdb/worker_pool.py')} | sha256sum -c -
python3 /opt/sixdb/runtime.py /opt/sixdb/job.json
'''
    if len(script.encode()) > 16384:
        raise ValueError('job parameters exceed EC2 user-data size; keep large inputs in files')
    return base64.b64encode(script.encode()).decode()


def launch_request(job, subnet, capacity):
    config = job['config']
    tags = [{'Key': 'Project', 'Value': 'SixDB'}, {'Key': 'SixDBWorkerJob', 'Value': job['id']},
            {'Key': 'Name', 'Value': 'sixdb-' + job['id']}]
    if job.get('format', 1) >= 2:
        tags += [{'Key': 'SixDBWorkerSession', 'Value': job['id']}, {'Key': 'SixDBWorkerProfile', 'Value': job['profile']}]
    request: dict[str, Any] = dict(ImageId=config['ami'], InstanceType=config['instance_type'], MinCount=1, MaxCount=1,
        ClientToken=f"{job['id']}-{subnet['zone']}-{capacity}",
        IamInstanceProfile={'Name': config['instance_profile']},
        NetworkInterfaces=[{'DeviceIndex': 0, 'SubnetId': subnet['id'],
            'Groups': [config['security_group_id']], 'AssociatePublicIpAddress': config.get('public_ip', True)}],
        BlockDeviceMappings=[{'DeviceName': config['root_device'], 'Ebs': {'VolumeSize': config['disk_gb'],
            'VolumeType': 'gp3', 'Encrypted': True, 'DeleteOnTermination': True}}],
        MetadataOptions={'HttpTokens': 'required', 'HttpEndpoint': 'enabled'},
        InstanceInitiatedShutdownBehavior='terminate',
        UserData=boot_script(job | {'config': config | {'actual_capacity': capacity},
                                   'worker': {'id': job['id'], 'reused': False, 'subnet_id': subnet['id']}}),
        TagSpecifications=[{'ResourceType': kind, 'Tags': tags} for kind in ('instance', 'volume')])
    if config.get('threads_per_core'):
        request['CpuOptions'] = {'CoreCount': config['hardware']['VCpuInfo']['DefaultCores'],
                                 'ThreadsPerCore': config['threads_per_core']}
    if capacity == 'spot':
        request['InstanceMarketOptions'] = {'MarketType': 'spot',
            'SpotOptions': {'SpotInstanceType': 'one-time', 'InstanceInterruptionBehavior': 'terminate'}}
    return request


def launch(job, directory, aws):
    capacities = ['spot', 'on-demand'] if job['config']['capacity'] == 'spot-or-on-demand' else [job['config']['capacity']]
    for capacity in capacities:
        for subnet in job['config']['subnets']:
            request = launch_request(job, subnet, capacity)
            save(directory / 'launch-request.json', request)
            print(f"Launch {capacity} {job['config']['instance_type']} in {subnet['zone']} …", flush=True)
            for attempt in range(4):
                try:
                    response = aws.call('ec2', 'run-instances', **request)
                    instance = response['Instances'][0]
                    state = {'instance_id': instance['InstanceId'], 'capacity': capacity,
                             'zone': subnet['zone'], 'launched_at': time.time()}
                    save(directory / 'launch.json', state)
                    aws.upload(directory / 'launch.json', job['uri'] + '/launch.json')
                    return state
                except AwsError as error:
                    if error.code in CAPACITY_ERRORS:
                        print(f'{error.code}; trying another offered zone', flush=True)
                        break
                    # IAM propagation and uncertain transport retry this exact client token.
                    retryable = error.code in {'TransportError', 'RequestLimitExceeded', 'ServiceUnavailable'} or (
                        error.code == 'InvalidParameterValue' and 'instance profile' in str(error).lower())
                    if retryable and attempt < 3:
                        time.sleep(5)
                        continue
                    raise
    if job['config']['capacity'] == 'spot':
        raise RuntimeError('no Spot capacity for the selected type in the offered zones; try later, '
                           'or use --capacity spot-or-on-demand to allow launch fallback')
    raise RuntimeError('no capacity for the selected type; change capacity/type explicitly or try later')


def locate(value, config):
    directory = JOBS / value
    if Path(value).is_dir():
        directory = Path(value).resolve()
    if not (directory / 'job.json').exists():
        if not re.fullmatch(r'[a-zA-Z0-9_-]+', value):
            raise ValueError('expected a worker job ID or existing local job directory')
        job = Aws(config['region']).get_json(config['bucket'], f'sixdb/workers/{value}/job.json')
        if job is None:
            raise ValueError('worker job not found')
        save(directory / 'job.json', job)
    return directory, json.loads((directory / 'job.json').read_text())


def instances(job, aws):
    assignment = aws.get_json(job['config']['bucket'], job['prefix'] + '/assignment.json') if job.get('format', 1) >= 2 else None
    filters = [{'Name': 'tag:SixDBWorkerSession', 'Values': [assignment['worker_id']]}] if assignment else [
        {'Name': 'tag:SixDBWorkerJob', 'Values': [job['id']]}]
    reservations = aws.call('ec2', 'describe-instances', Filters=filters + [
        {'Name': 'tag:Project', 'Values': ['SixDB']}])['Reservations']
    return [i for r in reservations for i in r['Instances']]


def status(job, aws):
    return aws.get_json(job['config']['bucket'], job['prefix'] + '/status.json')


def cancel(job, aws):
    active = [i for i in instances(job, aws) if i['State']['Name'] not in {'terminated', 'shutting-down'}]
    store = pool.Store(job['config']['bucket'], job['config']['region'])
    workers = []
    for instance in active:
        tags = {tag['Key']: tag['Value'] for tag in instance.get('Tags', [])}
        session = tags.get('SixDBWorkerSession')
        if session:
            key = pool.state_key(session)
            state, etag = store.read(key)
            if state is None:
                # Close the boot-time race before the worker creates its first state.
                state = {'state': 'starting', 'job_id': session}
            if state.get('job_id') != job['id']:
                continue  # A completed job cannot cancel a newer tenant.
            if not store.replace(key, state | {'state': 'retiring'}, etag):
                raise RuntimeError('Worker ownership changed during cancellation; inspect status and retry')
        workers.append(instance['InstanceId'])
    if workers:
        aws.call('ec2', 'terminate-instances', InstanceIds=workers)
    print('Termination requested: ' + (', '.join(workers) or 'no worker owned by this job'))


def reuse(job, directory, aws):
    if job['config']['fresh']:
        return False
    store = pool.Store(job['config']['bucket'], job['config']['region'])
    response = aws.call('ec2', 'describe-instances', Filters=[
        {'Name': 'tag:Project', 'Values': ['SixDB']},
        {'Name': 'tag:SixDBWorkerProfile', 'Values': [job['profile']]},
        {'Name': 'instance-state-name', 'Values': ['running']}])
    for reservation in response['Reservations']:
        for instance in reservation['Instances']:
            tags = {t['Key']: t['Value'] for t in instance.get('Tags', [])}
            worker_id = tags.get('SixDBWorkerSession')
            if not worker_id:
                continue
            key = pool.state_key(worker_id)
            state, etag = store.read(key)
            if not state or not pool.can_claim(state, job):
                continue
            assignment = {'worker_id': worker_id, 'instance_id': instance['InstanceId'], 'reused': True}
            # Durable routing exists before the claim, so wait/cancel can recover
            # even if this controller disappears immediately after the write.
            save(directory / 'assignment.json', assignment)
            aws.upload(directory / 'assignment.json', job['uri'] + '/assignment.json')
            claim = state | {'state': 'assigned', 'job_id': job['id'], 'job_uri': job['uri'],
                             'job_sha256': artifacts.sha256(directory / 'job.json')}
            if store.replace(key, claim, etag):
                print(f"Reuse {worker_id} ({instance['InstanceId']}, {state['capacity']}); prepared data and builds retained", flush=True)
                return True
    return False


def idle_hint(job, aws):
    """Best-effort advice after launch, while the new worker is already booting."""
    config = job['config']
    if config['fresh'] or not config['idle_seconds']:
        return
    # No history scan on successful reuse. Bound optional reads so advice cannot
    # hold up a detached submission indefinitely when AWS is unavailable.
    end = time.monotonic() + 8
    def remaining():
        return max(0.01, end - time.monotonic())
    try:
        response = aws.call('ec2', 'describe-instances', timeout=remaining(), Filters=[
            {'Name': 'tag:Project', 'Values': ['SixDB']},
            {'Name': 'tag:SixDBWorkerProfile', 'Values': [job['profile']]},
            {'Name': 'instance-state-name', 'Values': ['shutting-down', 'terminated']}])
        candidates = [i for r in response['Reservations'] for i in r['Instances']
                      if i['State']['Name'] in {'shutting-down', 'terminated'}]
        for instance in sorted(candidates, key=lambda i: i['LaunchTime'], reverse=True)[:3]:
            if time.monotonic() >= end:
                return
            tags = {t['Key']: t['Value'] for t in instance.get('Tags', [])}
            session = tags.get('SixDBWorkerSession')
            if not session or session == job['id']:
                continue
            data, metadata = aws.get_object(config['bucket'], pool.state_key(session), timeout=remaining())
            if data is None:
                continue
            state = json.loads(data)
            now = time.time()
            age = now - state.get('idle_until', 0)
            if (state.get('state') != 'retiring' or state.get('error') or not 0 <= age <= 300
                    or state.get('instance_id') != instance['InstanceId']
                    or state['idle_until'] > job['created_at']):
                continue
            modified = datetime.fromisoformat(metadata['LastModified'].replace('Z', '+00:00')).timestamp()
            # An early cancel/failure or maximum-age shutdown is not an idle miss.
            if not 0 <= modified - state['idle_until'] <= 30 or state['idle_until'] >= state['max_end']:
                continue
            if not pool.can_claim(state | {'state': 'idle', 'idle_until': now + 1}, job, now):
                continue
            previous, _ = aws.get_object(config['bucket'], f"sixdb/workers/{state['job_id']}/job.json",
                                         timeout=remaining())
            if previous is None:
                continue
            idle_seconds = json.loads(previous)['config']['idle_seconds']
            needed = idle_seconds + age + 1
            suggested = math.ceil(needed / 300) * 300
            if not idle_seconds or config['idle_seconds'] >= needed or suggested > 3600:
                continue
            ago = f'{age:.0f}s ago' if age < 90 else f'about {age / 60:.0f} min ago'
            print(f"BTW: a compatible worker's {idle_seconds / 60:g}-minute idle window ended {ago}. "
                  f"Try --idle-seconds {suggested} for longer edit/review loops.", flush=True)
            return
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError):
        pass  # Optional advice must not turn a successful launch into a failed job.


def fetch(job, directory, state, aws):
    # A final reference describes an immutable bundle; partial syncs are kept separate.
    if state and state.get('artifact'):
        save(directory / 'artifact.json', state['artifact'])
        destination = directory / 'results'
        if not destination.exists():
            artifacts.restore_bundle(state['artifact'], destination)
        for receipt in sorted(destination.rglob('run.json')):
            # Failed/aborted scripts can leave unfinished study receipts. Recover
            # their raw evidence too; their own readers still check validity.
            if (receipt.parent / 'input-artifacts.json').exists():
                artifacts.restore_inputs(receipt.parent)
        print(f'Results: {destination}', flush=True)
    else:
        destination = directory / 'partial'
        subprocess.run(['aws', 's3', 'sync', '--only-show-errors', job['uri'] + '/live/',
                        str(destination), '--region', aws.region], check=True)
        print(f'Partial output only: {destination}', flush=True)


def wait(job, directory, aws, interval=10):
    previous = None
    stopped_since = None
    while True:
        state = status(job, aws)
        if state != previous and state:
            print(f"{job['id']}: {state['state']}" + (f" — {state['error']}" if state.get('error') else ''), flush=True)
            previous = state
        workers = instances(job, aws)
        active = [i for i in workers if i['State']['Name'] in {'pending', 'running'}]
        if state and state['state'] in FINAL:
            save(directory / 'status.json', state)
            fetch(job, directory, state, aws)
            # Old workers were job-scoped. Sessions own their idle/lifetime cleanup;
            # a completed job must never terminate a worker serving a newer job.
            if job.get('format', 1) < 2:
                cancel(job, aws)
            return 0 if state['state'] == 'complete' else 1
        if not active:
            stopped_since = stopped_since or time.monotonic()
            if time.monotonic() - stopped_since > 30:
                fetch(job, directory, state, aws)
                print('Worker ended without a completed upload; inspect logs/partial output.', flush=True)
                return 1
        else:
            stopped_since = None
        if time.time() > job['created_at'] + job['config']['deadline_seconds'] + 180:
            cancel(job, aws)
            fetch(job, directory, state, aws)
            print('Worker deadline elapsed without completion.', flush=True)
            return 1
        time.sleep(interval)


def logs(job, aws, *, console=False):
    if console:
        found = False
        for item in instances(job, aws):
            output = aws.call('ec2', 'get-console-output', InstanceId=item['InstanceId'], Latest=True)
            if output.get('Output'):
                print(f"EC2 console: {item['InstanceId']} (instance-wide boot diagnostics)")
                # AWS CLI already decodes GetConsoleOutput; decoding again corrupts
                # plain text and fails on Unicode in systemd/cloud-init output.
                print(output['Output'], end='' if output['Output'].endswith('\n') else '\n')
                found = True
        if not found:
            print('No EC2 console output is available yet.')
        return

    data = aws.get_bytes(job['config']['bucket'], job['prefix'] + '/live/script.log')
    if data is not None:
        print(data.decode('utf-8', errors='replace'), end='')
        return
    state = status(job, aws) or {}
    print(f"No script log has been uploaded (job state: {state.get('state', 'not yet reported')}).")
    if state.get('state') in FINAL:
        print('The script may not have started, or collection may be incomplete. '
              f"Inspect collected output with: python3 workbench/tools/worker.py fetch {job['id']}")
    elif not job['config'].get('sync_seconds', 0):
        print('Live sync is disabled; script logs are uploaded during collection. '
              f"Resume collection with: python3 workbench/tools/worker.py wait {job['id']}")
    else:
        print(f"Live sync is enabled every {job['config']['sync_seconds']} seconds; "
              'try again after the script starts and its first upload completes.')
    print(f"For instance boot diagnostics: python3 workbench/tools/worker.py logs {job['id']} --console")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=HERE / 'worker.json', help='JSON settings overlay')
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('run', 'plan'):
        cmd = commands.add_parser(name, help='run a script' if name == 'run' else 'resolve settings without creating anything')
        if name == 'run':
            cmd.add_argument('script', type=Path)
            cmd.add_argument('--arg', action='append', default=[], help='script argument; repeat, use --arg=--flag for flags')
            cmd.add_argument('--detach', action='store_true')
        cmd.add_argument('--machine', choices=['zen5', 'granite-rapids', 'neoverse-v2'])
        cmd.add_argument('--instance-type')
        cmd.add_argument('--ami')
        cmd.add_argument('--capacity', choices=['spot', 'on-demand', 'spot-or-on-demand'],
                         help='default: spot; spot-or-on-demand explicitly allows launch fallback')
        cmd.add_argument('--deadline', type=int, dest='deadline_seconds', help='job deadline including setup and collection')
        cmd.add_argument('--idle-seconds', type=int, help='keep worker ready after collection; default 300, zero shuts down')
        cmd.add_argument('--max-age', type=int, dest='max_age_seconds', help='maximum instance lifetime; default 14400 seconds')
        cmd.add_argument('--fresh', action='store_true', help='launch a fresh instance instead of reusing a compatible idle worker')
        cmd.add_argument('--disk-gb', type=int)
        cmd.add_argument('--setup', choices=['minimal', 'toolchain'])
        cmd.add_argument('--sync-seconds', type=int, help='optional live output sync; zero keeps uploads outside measurement')
        cmd.add_argument('--env', action='append', default=[])
    for name in ('status', 'wait', 'fetch', 'cancel', 'logs'):
        cmd = commands.add_parser(name)
        cmd.add_argument('job')
        if name == 'logs':
            cmd.add_argument('--console', action='store_true', help='show instance boot diagnostics instead of script output')
    commands.add_parser('list', help='list SixDB worker instances')
    argv = sys.argv[1:]
    arguments = []
    if '--' in argv:
        split = argv.index('--')
        argv, arguments = argv[:split], argv[split + 1:]
    args = parser.parse_args(argv)
    if arguments and args.command != 'run':
        parser.error('arguments after -- are for run scripts')
    base = json.loads((HERE / 'worker.json').read_text())
    if args.config != HERE / 'worker.json':
        overlay = json.loads(args.config.read_text())
        machines = {name: settings | overlay.get('machines', {}).get(name, {})
                    for name, settings in base['machines'].items()}
        base = base | overlay | {'machines': machines}
    if args.command in {'run', 'plan'}:
        config = configuration(args, base)
        aws = Aws(config['region'])
        config = resolve(config, aws)
        if args.command == 'plan':
            print(json.dumps(config, indent=2))
            return 0
        script = args.script.resolve()
        if not script.is_relative_to(ROOT) or not script.is_file():
            parser.error('script must be a file in this repository')
        name = str(script.relative_to(ROOT))
        directory = JOBS / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8])
        directory.mkdir(parents=True)
        source = snapshot(directory)
        if name not in source['files']:
            raise ValueError('script is ignored/excluded from source capture; use a non-ignored repository file')
        ensure_infrastructure(config, aws)
        prefix = 'sixdb/workers/' + directory.name
        # Per-file hashes live separately: EC2 user-data has a small fixed size limit.
        save(directory / 'source-manifest.json', source.pop('files'))
        job = {'format': 2, 'profile': pool.profile(config, source), 'id': directory.name, 'created_at': time.time(),
            'source': source, 'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'script': name, 'args': args.arg + arguments, 'config': config, 'prefix': prefix,
            'uri': f"s3://{config['bucket']}/{prefix}"}
        save(directory / 'job.json', job)
        boot_script(job)  # Validate before uploading or allocating compute.
        for filename in ('source.tar.gz', 'source-manifest.json', 'runtime.py', 'worker_pool.py', 'job.json'):
            aws.upload(directory / filename, job['uri'] + '/' + filename)
        cpu = config['hardware']['VCpuInfo']
        vcpus = cpu['DefaultCores'] * config.get('threads_per_core', cpu['DefaultThreadsPerCore'])
        memory = config['hardware']['MemoryInfo']['SizeInMiB'] / 1024
        print(f"Job: {job['id']}\nWorker: {config['instance_type']}, {vcpus} vCPU, {memory:g} GiB; "
              f"capacity={config['capacity']}; {config['deadline_seconds']}s job deadline, {config['idle_seconds']}s idle window\nS3: {job['uri']}\n"
              f"Resume: python3 workbench/tools/worker.py wait {job['id']}", flush=True)
        try:
            if not reuse(job, directory, aws):
                # Replace a rejected reuse candidate's routing with this new session.
                save(directory / 'assignment.json', {'worker_id': job['id'], 'reused': False})
                aws.upload(directory / 'assignment.json', job['uri'] + '/assignment.json')
                launch(job, directory, aws)
                idle_hint(job, aws)
            if not args.detach:
                return wait(job, directory, aws)
        except (KeyboardInterrupt, Exception) as error:
            print(f"Controller stopped: {error}\nJob is retained. Use wait/status/cancel {job['id']}; do not resubmit to resume.", file=sys.stderr)
            return 1
        return 0
    aws = Aws(base['region'])
    if args.command == 'list':
        response = aws.call('ec2', 'describe-instances', Filters=[{'Name': 'tag:Project', 'Values': ['SixDB']},
            {'Name': 'tag-key', 'Values': ['SixDBWorkerJob']},
            {'Name': 'instance-state-name', 'Values': ['pending', 'running', 'stopping', 'stopped', 'shutting-down']}])
        for reservation in response['Reservations']:
            for item in reservation['Instances']:
                tags = {t['Key']: t['Value'] for t in item['Tags']}
                session = tags.get('SixDBWorkerSession')
                state, _ = pool.Store(base['bucket'], aws.region).read(pool.state_key(session)) if session else (None, None)
                print(session or tags['SixDBWorkerJob'], item['InstanceId'], item['InstanceType'],
                      (state or {}).get('state', item['State']['Name']),
                      'job=' + (state or {}).get('job_id', tags['SixDBWorkerJob']))
        return 0
    directory, job = locate(args.job, base)
    aws = Aws(job['config']['region'])
    if args.command == 'cancel':
        cancel(job, aws)
    elif args.command == 'wait':
        return wait(job, directory, aws)
    elif args.command == 'fetch':
        fetch(job, directory, status(job, aws), aws)
    elif args.command == 'status':
        assignment = aws.get_json(job['config']['bucket'], job['prefix'] + '/assignment.json')
        session, _ = pool.Store(job['config']['bucket'], aws.region).read(pool.state_key(assignment['worker_id'])) if assignment else (None, None)
        print(json.dumps({'worker': status(job, aws), 'session': session, 'instances': [
            {'id': i['InstanceId'], 'state': i['State']['Name']} for i in instances(job, aws)]}, indent=2))
    else:
        logs(job, aws, console=args.console)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
