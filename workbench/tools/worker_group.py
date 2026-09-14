#!/usr/bin/env python3
"""Launch named workers from one capture; resume collection or cancel a recorded group."""
import argparse
import concurrent.futures
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys
import threading
import time
import uuid

import capture
import storage
import worker


def identifier():
    return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]


class Group:
    def __init__(self, receipt):
        self.receipt = Path(receipt).resolve()
        self.directory = self.receipt.parent

    def read(self):
        return json.loads(self.receipt.read_text())

    def update(self, member=None, **fields):
        # Wait and cancel may be separate controllers. Never replace their
        # independently recorded outcomes with an earlier in-memory copy.
        with storage.lock(self.directory / '.receipt.lock'):
            state = self.read()
            target = state if member is None else state['members'][member]
            target.update(fields)
            storage.write_json(self.receipt, state)

    def job(self, member):
        entry = self.read()['members'][member]
        if entry['phase'] not in {'submitting', 'submitted'}:
            return None  # No compute request precedes the durable submitting record.
        return worker.locate(entry['job'], entry['config'])

    def launch(self):
        with storage.lock(self.directory / '.dispatch.lock'):
            state = self.read()
            if state.get('launch_started') or state.get('aborted'):
                raise ValueError('group launch already attempted; use wait/status/cancel')
            self.update(launch_started=True)
            self._launch()

    def _launch(self):
        state = self.read()
        try:
            if state.get('network'):
                network = state['network']
                aws = worker.Aws(state['region'])
                group_id = aws.call('ec2', 'create-security-group', GroupName=network['name'],
                    Description='Temporary SixDB worker group', VpcId=network['vpc_id'],
                    TagSpecifications=[{'ResourceType': 'security-group', 'Tags': [
                        {'Key': 'Project', 'Value': 'SixDB'}, {'Key': 'SixDBWorkerGroup', 'Value': state['id']}]}])['GroupId']
                network['id'] = group_id
                self.update(network=network)
                rules = [{'IpProtocol': protocol, 'FromPort': a, 'ToPort': b,
                          'UserIdGroupPairs': [{'GroupId': group_id}]}
                         for protocol in ('tcp', 'udp') for a, b in network.get(protocol + '_ports', [])]
                if network.get('icmp'):
                    rules.append({'IpProtocol': 'icmp', 'FromPort': -1, 'ToPort': -1,
                                  'UserIdGroupPairs': [{'GroupId': group_id}]})
                if rules:
                    aws.call('ec2', 'authorize-security-group-ingress', GroupId=group_id, IpPermissions=rules)
            for name, entry in state['members'].items():
                if self.read().get('aborted'):
                    raise RuntimeError('group was cancelled during launch')
                config = dict(entry['config'])
                if state.get('network'):
                    config['security_group_id'] = group_id
                directory = worker.JOBS / entry['job']
                directory.mkdir(parents=True)
                self.update(name, config=config, phase='preparing')
                aws = worker.Aws(config['region'])
                job = worker.prepare(config, Path(state['source']), entry['script'], entry['args'], directory, aws)
                # Preallocated IDs and this record survive an interrupted or
                # uncertain dispatch; recovery never launches a replacement.
                if self.read().get('aborted'):
                    raise RuntimeError('group was cancelled during preparation')
                self.update(name, phase='submitting', job_created_at=job['created_at'])
                worker.dispatch(job, directory, aws)
                self.update(name, phase='submitted')
        except BaseException as error:
            self.update(launch_error=str(error) or type(error).__name__)
            print(f'Partial group recorded: {self.receipt}\nUse worker_group.py wait or cancel; do not rerun the launch.', file=sys.stderr)
            raise
        self.update(launch_complete=True)

    def observe(self, name, stop=None):
        entry = self.read()['members'][name]
        while True:
            if stop is not None and stop.is_set():
                return {'detached': True}
            try:
                located = self.job(name)
                if located is None:
                    result = {'error': 'member was not submitted'}
                else:
                    directory, job = located
                    result = {'exit_code': worker.wait(job, directory, worker.Aws(job['config']['region']), stop=stop)}
                break
            except Exception as error:
                if stop is not None and stop.is_set():
                    return {'detached': True}
                self.update(name, observation_error=str(error))
                end = entry['job_created_at'] + entry['config']['deadline_seconds'] + 300
                if time.time() >= end:
                    result = {'error': str(error), 'deadline_elapsed': True}
                    break
                print(f'{name}: observation failed; retrying the same job: {error}', flush=True)
                if stop is None:
                    time.sleep(10)
                else:
                    stop.wait(10)
        self.update(name, collection=result | {'utc': time.time()})
        return result

    def cancel_member(self, name):
        for attempt in range(3):
            try:
                located = self.job(name)
                if located:
                    _, job = located
                    worker.cancel(job, worker.Aws(job['config']['region']))
                self.update(name, cancellation={'requested': True, 'utc': time.time()})
                return True
            except Exception as error:
                self.update(name, cancellation={'error': str(error), 'utc': time.time()})
                if attempt < 2:
                    time.sleep(3)
        return False

    def remove_network(self):
        state = self.read()
        network = state.get('network')
        if not network or state.get('network_removed'):
            return True
        aws = worker.Aws(state['region'])
        try:
            # The name is recorded before create. Discover an accepted create
            # whose response was lost, without creating a second resource.
            groups = aws.call('ec2', 'describe-security-groups', Filters=[
                {'Name': 'group-name', 'Values': [network['name']]},
                {'Name': 'vpc-id', 'Values': [network['vpc_id']]}])['SecurityGroups']
            for group in groups:
                tags = {t['Key']: t['Value'] for t in group.get('Tags', [])}
                if tags.get('SixDBWorkerGroup') != state['id']:
                    raise ValueError('security group ownership differs; preserved')
                group_id = group['GroupId']
                for attempt in range(60):
                    reservations = aws.call('ec2', 'describe-instances', Filters=[
                        {'Name': 'instance.group-id', 'Values': [group_id]}])['Reservations']
                    if not any(i['State']['Name'] != 'terminated' for r in reservations for i in r['Instances']):
                        break
                    time.sleep(5)
                else:
                    raise RuntimeError('instances still reference the security group; retry cleanup later')
                if group.get('IpPermissions'):
                    aws.call('ec2', 'revoke-security-group-ingress', GroupId=group_id, IpPermissions=group['IpPermissions'])
                for attempt in range(12):
                    try:
                        aws.call('ec2', 'delete-security-group', GroupId=group_id)
                        break
                    except worker.AwsError as error:
                        if error.code == 'InvalidGroup.NotFound':
                            break
                        if error.code != 'DependencyViolation' or attempt == 11:
                            raise
                        time.sleep(5)
            self.update(network_removed=True, network_error=None)
            return True
        except Exception as error:
            self.update(network_error=str(error))
            return False

    def wait(self):
        with storage.lock(self.directory / '.dispatch.lock'):
            state = self.read()  # Wait for an active submitter; a crashed one releases the lock.
        if not state.get('launch_started'):
            raise ValueError('group launch has not started; no jobs to observe')
        stop = threading.Event()
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, len(state['members']))) as pool:
            try:
                list(pool.map(lambda name: self.observe(name, stop), state['members']))
            except BaseException:
                stop.set()
                raise
        state = self.read()
        clean = True
        for name, entry in state['members'].items():
            result = entry['collection']
            # Private-group hosts are dedicated. An observation error before
            # the deadline grants no authority to cancel an active measurement.
            if result.get('deadline_elapsed') or (state.get('network') and 'exit_code' in result):
                clean = self.cancel_member(name) and clean
        if state.get('network'):
            clean = self.remove_network() and clean
        state = self.read()
        complete = state.get('launch_complete', False) and not state.get('aborted') and all(
            e['collection'].get('exit_code') == 0 for e in state['members'].values())
        return 0 if complete and clean else 1

    def cancel(self):
        self.update(aborted=True)
        with storage.lock(self.directory / '.dispatch.lock'):
            clean = True
            for name in self.read()['members']:
                clean = self.cancel_member(name) and clean
            return 0 if self.remove_network() and clean else 1

    def status(self):
        state = self.read()
        for name, entry in state['members'].items():
            try:
                located = self.job(name)
                if located:
                    _, job = located
                    entry['worker_status'] = worker.status(job, worker.Aws(job['config']['region']))
            except Exception as error:
                entry['observation_error'] = str(error)
        return state


def create(spec, directory=None, *, source=worker.ROOT):
    """Resolve participants and freeze source once; no compute is launched here."""
    group_id = identifier()
    directory = Path(directory or worker.ROOT / 'build/worker-groups' / group_id).resolve()
    if directory.exists():
        raise ValueError('choose a new group directory; use wait/status/cancel to recover an existing group')
    if not isinstance(spec.get('members'), dict) or not spec['members']:
        raise ValueError('provide named group members')
    network = spec.get('network')
    if network:
        if not network.get('vpc_id') or any(not (isinstance(p, list) and len(p) == 2
                and all(type(n) is int for n in p) and 0 < p[0] <= p[1] <= 65535)
                for protocol in ('tcp', 'udp') for p in network.get(protocol + '_ports', [])):
            raise ValueError('network needs a VPC ID and TCP/UDP port ranges within 1..65535')
        network = network | {'name': 'sixdb-group-' + group_id}
    members = {}
    for name, entry in spec['members'].items():
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
            raise ValueError('member names use letters, digits, underscore or hyphen')
        common = spec.get('config', {})
        overlay = common | entry.get('config', {})
        overlay['env'] = common.get('env', {}) | entry.get('config', {}).get('env', {})
        if network:
            overlay |= {'vpc_id': network['vpc_id'], 'fresh': True, 'idle_seconds': 0}
        config = worker.configuration(argparse.Namespace(instance_type=overlay.get('instance_type'),
                                                         ami=overlay.get('ami')), worker.settings(overlay))
        if 'threads_per_core' in overlay:
            config['threads_per_core'] = overlay['threads_per_core']
        config = worker.resolve(config, worker.Aws(config['region']))
        script = entry.get('script', spec.get('script'))
        arguments = entry.get('args', spec.get('args', []))
        if not isinstance(script, str) or not isinstance(arguments, list) or not all(isinstance(a, str) for a in arguments):
            raise ValueError('each member needs a script and a list of string arguments')
        script_path = (Path(source) / script).resolve()
        if not script_path.is_relative_to(Path(source).resolve()) or not script_path.is_file():
            raise ValueError(f'missing repository script: {script}')
        members[name] = {'job': identifier(), 'phase': 'planned', 'config': config, 'script': script, 'args': arguments}
    locations = {(e['config']['region'], e['config']['bucket']) for e in members.values()}
    if len(locations) != 1:
        raise ValueError('a group uses one region and result bucket')
    region, bucket = locations.pop()
    uri = f's3://{bucket}/sixdb/worker-groups/{group_id}'
    for name, entry in members.items():
        entry['config']['env'] = {k: v.replace('{group_uri}', uri) for k, v in entry['config']['env'].items()}
        entry['config']['env'] |= {'SIXDB_GROUP_URI': uri, 'SIXDB_GROUP_MEMBER': name}
    directory.mkdir(parents=True)
    group = Group(directory / 'group.json')
    storage.write_json(group.receipt, {'format': 1, 'id': group_id, 'region': region, 'bucket': bucket,
        'uri': uri, 'source': str(directory / 'source'), 'members': members, 'network': network})
    print(f'Group: {group.receipt}', flush=True)
    worker.worker_cache.preflight(worker.ROOT)
    capture.capture(Path(source), directory / 'source')
    return group


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    run = commands.add_parser('run', help='launch a new group from a JSON spec')
    run.add_argument('spec', type=Path)
    run.add_argument('--output', type=Path, help='new receipt/source directory; default build/worker-groups/ID')
    run.add_argument('--source', type=Path, default=worker.ROOT)
    run.add_argument('--detach', action='store_true')
    for name in ('wait', 'status', 'cancel'):
        sub = commands.add_parser(name)
        sub.add_argument('receipt', type=Path, help='existing group.json')
    args = parser.parse_args()
    if args.command == 'run':
        group = create(json.loads(args.spec.read_text()), args.output, source=args.source)
        group.launch()
        return 0 if args.detach else group.wait()
    group = Group(args.receipt)
    if args.command == 'status':
        state = group.status()
        for name, entry in state['members'].items():
            live = (entry.get('worker_status') or {}).get('state', entry['phase'])
            print(f"{name}: {entry['job']} {live}; collection={entry.get('collection', 'pending')}")
            if entry.get('observation_error'):
                print(f"  Last observation error: {entry['observation_error']}")
        if state.get('network'):
            print('Private network: ' + ('removed' if state.get('network_removed') else 'retained'))
        for key in ('launch_error', 'network_error'):
            if state.get(key):
                print(f'{key}: {state[key]}')
        return 0
    return getattr(group, args.command)()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        raise SystemExit(str(error))
