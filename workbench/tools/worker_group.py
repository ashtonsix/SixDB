#!/usr/bin/env python3
"""Launch named workers from one capture; resume collection or cancel a recorded group."""
import argparse
import concurrent.futures
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shlex
import sys
import threading
import time
import uuid

import capture
import storage
import worker
import worker_network as networks
from worker_context import put


def identifier():
    return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]


class Group:
    def __init__(self, receipt):
        self.receipt = Path(receipt).resolve()
        self.directory = self.receipt.parent

    def read(self):
        return json.loads(self.receipt.read_text())

    def uri(self):
        state = self.read()
        return state.get('uri', f's3://{state["bucket"]}/sixdb/worker-groups/{state["id"]}')

    def control(self, **fields):
        with storage.lock(self.directory / '.control.lock'):
            state = self.read()
            try:
                put(worker.Aws(state['region']), self.uri() + '/control.json',
                    {k: state[k] for k in ('aborted', 'launch_error', 'launch_complete',
                        'network', 'network_removed', 'network_error', 'network_checked_at') if k in state}
                    | fields | {'updated_at': time.time()})
                self.update(control_error=None)
            except Exception as error:
                self.update(control_error=str(error))
                print(f'Group control publication failed: {error}', file=sys.stderr, flush=True)

    def publish(self, name, value):
        """Release a study-owned startup condition after controller-side preparation."""
        from worker_context import label
        state = self.read()
        put(worker.Aws(state['region']), self.uri() + '/values/' + label(name) + '/controller.json',
            {'job': state['id'], 'member': 'controller', 'value': value, 'updated_at': time.time()})

    def update(self, member=None, **fields):
        # Wait and cancel may be separate controllers. Never replace their
        # independently recorded outcomes with an earlier in-memory copy.
        with storage.lock(self.directory / '.receipt.lock'):
            state = self.read()
            target = state if member is None else state['members'][member]
            if 'collection' in fields:
                # Partial checkpoints cannot replace already recovered immutable evidence.
                previous = target.get('collection', {})
                incoming = fields['collection']
                if previous.get('artifact') and not incoming.get('artifact'):
                    incoming = incoming | {k: previous[k] for k in ('artifact', 'results') if k in previous}
                if 'exit_code' not in incoming and 'exit_code' in previous:
                    incoming['exit_code'] = previous['exit_code']
                fields['collection'] = incoming
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
            with networks.lock(state):
                self._launch()

    def _launch(self):
        state = self.read()
        try:
            # Immutable membership exists even if a peer fails before running its script.
            put(worker.Aws(state['region']), self.uri() + '/manifest.json', state | {'uri': self.uri()})
            if state.get('network'):
                group_id = networks.ensure(state, worker.Aws(state['region']), self.update)
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
            self.control(launch_error=str(error) or type(error).__name__)
            print(f'Partial group recorded: {self.receipt}\nUse worker_group.py wait or cancel; do not rerun the launch.', file=sys.stderr)
            raise
        self.update(launch_complete=True)
        self.control(launch_complete=True)

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
                end = entry.get('job_created_at', 0) + entry['config']['deadline_seconds'] + 300
                if time.time() >= end:
                    result = {'error': str(error), 'deadline_elapsed': True}
                    break
                print(f'{name}: observation failed; retrying the same job: {error}', flush=True)
                if stop is None:
                    time.sleep(10)
                else:
                    stop.wait(10)
        self.update(name, collection=result | {'utc': time.time()})
        print(f"{name}: collection {'complete' if result.get('exit_code') == 0 else 'incomplete/failed'}; "
              f"{worker.JOBS / entry['job']}", flush=True)
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
        with networks.lock(state):
            removed = networks.remove(state, worker.Aws(state['region']), self.update)
            self.control()
            if state.get('network'):
                print('Private network removed' if removed else 'Private network cleanup pending: ' + self.read()['network_error'], flush=True)
            return removed

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
            if result.get('deadline_elapsed') or (state.get('network') and not state['network'].get('scope') and 'exit_code' in result):
                clean = self.cancel_member(name) and clean
        if state.get('network') and not state['network'].get('scope'):
            clean = self.remove_network() and clean
        state = self.read()
        complete = state.get('launch_complete', False) and not state.get('aborted') and all(
            e['collection'].get('exit_code') == 0 for e in state['members'].values())
        return 0 if complete and clean else 1

    def cancel(self):
        self.update(aborted=True)
        self.control(aborted=True)
        with storage.lock(self.directory / '.dispatch.lock'):
            clean = True
            for name in self.read()['members']:
                clean = self.cancel_member(name) and clean
            if not (self.read().get('network') or {}).get('scope'):
                clean = self.remove_network() and clean
            return 0 if clean else 1

    def status(self):
        state = self.read()
        def inspect(item):
            name, entry = item
            try:
                located = self.job(name)
                if located:
                    directory, job = located
                    aws = worker.Aws(job['config']['region'])
                    entry['worker_status'] = worker.status(job, aws)
                    entry['source_commit'] = job.get('source_commit')
                    entry['source_sha256'] = job.get('source', {}).get('sha256')
                    entry['results'] = str(directory / 'results')
                    entry.pop('observation_error', None)
                    try:
                        instances = [{'id': i['InstanceId'], 'state': i['State']['Name']}
                                     for i in worker.instances(job, aws)]
                        if instances or not entry.get('instances'):
                            entry['instances'] = instances
                            entry['resource_observed_at'] = time.time()
                            self.update(name, instances=instances, resource_observed_at=entry['resource_observed_at'])
                        else:
                            entry['resource_note'] = 'no longer listed by EC2; showing last observation'
                        assignment = aws.get_json(state['bucket'], job['prefix'] + '/assignment.json') if job.get('prefix') else None
                        if assignment:
                            entry['session'] = aws.get_json(state['bucket'], worker.pool.state_key(assignment['worker_id']))
                    except Exception as error:
                        entry['resource_error'] = str(error)
                    prefix = self.uri().removeprefix(f's3://{state["bucket"]}/')
                    progress = aws.get_json(state['bucket'], prefix + f'/members/{name}/progress.json')
                    if progress and progress.get('job') == entry['job'] and progress.get('member') == name:
                        entry['progress'] = progress
            except Exception as error:
                entry['observation_error'] = str(error)
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, len(state['members']))) as pool:
            list(pool.map(inspect, state['members'].items()))
        return state

    def show(self, state=None):
        state = state or self.status()
        counts = Counter((e.get('worker_status') or {}).get('state', e['phase']) for e in state['members'].values())
        print(f"Group {state['id']} — {len(state['members'])} members; " + ", ".join(f"{n} {phase}" for phase, n in counts.items()))
        for name, entry in state['members'].items():
            live = entry.get('worker_status') or {}
            progress = entry.get('progress') or {}
            lifecycle = live.get('state', entry['phase'])
            age = f"; reported {max(0, time.time()-progress['updated_at']):.0f}s ago" if progress.get('updated_at') else ''
            phase = progress.get('phase', 'no study progress reported')
            collected = entry.get('collection', {})
            evidence = ('collected' if collected.get('exit_code') == 0 else
                        'failed/partial collected' if 'exit_code' in collected else 'not collected')
            if live.get('artifact'):
                evidence += '; archive published'
            reused = live.get('worker') or {}
            session = entry.get('session') or {}
            print(f"  {name}: {lifecycle} | last progress: {phase}{age} | {evidence}")
            detail = {k: progress[k] for k in ('case', 'completed', 'total', 'waiting_for', 'last_completed', 'log') if k in progress}
            if detail:
                print('    ' + json.dumps(detail, ensure_ascii=False))
            print(f"    job={entry['job']}" + (f"; reused={reused['reused']}" if 'reused' in reused else '')
                  + (f"; lifetime remaining={max(0, reused['max_end']-time.time()):.0f}s"
                     if reused.get('max_end') and session.get('state') in {'busy', 'idle', 'assigned'} else ''))
            if 'script_seconds' in live:
                print(f"    script={live['script_seconds']:.2f}s; worker before collection={live.get('worker_seconds_before_collection', 0):.2f}s")
            if 'instances' in entry:
                resources = ', '.join(f"{i['id']} {i['state']}" for i in entry['instances']) or 'none listed by EC2'
                print('    resources: ' + resources + ('; ' + entry['resource_note'] if entry.get('resource_note') else ''))
            if session:
                print(f"    session: {session['state']}; current job={session.get('job_id')}"
                      + (f"; idle remaining={max(0, session['idle_until']-time.time()):.0f}s" if session.get('idle_until') and session.get('state') == 'idle' else ''))
            error = live.get('error') or progress.get('error') or entry.get('observation_error') or entry.get('resource_error') or entry.get('collection_error')
            if error:
                print(f"    {live.get('failure_phase', 'error')}: {error}")
                log = ' --file setup.log' if live.get('failure_phase') == 'setting-up' else ''
                print(f'    Inspect: worker_group.py logs {shlex.quote(str(self.receipt))} {name}{log}')
        for key in ('launch_error', 'network_error', 'control_error'):
            if state.get(key):
                print(f'{key}: {state[key]}')
        if state.get('network'):
            network = state['network']
            print('Private network: ' + ('removed at last cleanup' if state.get('network_removed') else 'retained')
                  + (f"; reusable scope={network['scope']}" if network.get('scope') else ''))
            if network.get('scope') and not state.get('network_removed'):
                print(f'  Cleanup when unused: worker_group.py cleanup-network {shlex.quote(str(self.receipt))}')
        print(f'Receipt: {self.receipt}')

    def fetch(self, members=None, *, partial=False):
        """Collect published outputs now, without waiting for or stopping active peers."""
        state = self.read()
        names = list(state['members'] if members is None else members)
        if not names or any(name not in state['members'] for name in names):
            raise ValueError('unknown group member')
        def collect(name):
            try:
                located = self.job(name)
                if located is None:
                    print(f'{name}: not submitted', flush=True)
                    return True
                directory, job = located
                aws = worker.Aws(job['config']['region'])
                status = worker.status(job, aws)
                if not (status and (status.get('artifact') or status.get('state') in worker.FINAL)) and not partial:
                    print(f'{name}: still active or unreported; use --partial for uploaded checkpoints', flush=True)
                    return True
                worker.fetch(job, directory, status, aws)
                outcome = {'utc': time.time(), 'results': str(directory / ('results' if status and status.get('artifact') else 'partial'))}
                if status and status.get('state') in worker.FINAL:
                    outcome['exit_code'] = 0 if status['state'] == 'complete' else 1
                outcome['artifact'] = status.get('artifact') if status else None
                self.update(name, collection=outcome, collection_error=None)
                return True
            except Exception as error:
                self.update(name, collection_error=str(error))
                print(f'{name}: collection error: {error}', file=sys.stderr, flush=True)
                return False
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, len(names))) as pool:
            return 0 if all(list(pool.map(collect, names))) else 1

    def references(self):
        state = self.status()
        members = {}
        for name, entry in state['members'].items():
            status = entry.get('worker_status') or {}
            members[name] = {k: entry[k] for k in ('job', 'phase', 'collection', 'results', 'source_commit',
                                                    'source_sha256', 'observation_error', 'instances', 'session',
                                                    'resource_observed_at', 'resource_note', 'resource_error') if k in entry}
            members[name] |= {'uri': f's3://{state["bucket"]}/sixdb/workers/{entry["job"]}',
                             'state': status.get('state'), 'artifact': status.get('artifact'),
                             'error': status.get('error'), 'worker': status.get('worker')}
        return {'format': 1, 'group': state['id'], 'uri': self.uri(), 'region': state['region'],
                'bucket': state['bucket'], 'network': state.get('network'),
                'network_removed': state.get('network_removed', False), 'members': members}


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
        network = networks.normalize(network) | {'name': 'sixdb-group-' + group_id}
    members = {}
    for name, entry in spec['members'].items():
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name) or name == 'controller':
            raise ValueError('member names use letters, digits, underscore or hyphen')
        common = spec.get('config', {})
        overlay = common | entry.get('config', {})
        overlay['env'] = common.get('env', {}) | entry.get('config', {}).get('env', {})
        if network:
            overlay['vpc_id'] = network['vpc_id']
            if not network.get('scope'):
                overlay |= {'fresh': True, 'idle_seconds': 0}
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
    if network and network.get('scope'):
        network['name'] = 'sixdb-scope-' + networks.identity({'network': network, 'region': region})
    uri = f's3://{bucket}/sixdb/worker-groups/{group_id}'
    for name, entry in members.items():
        entry['config']['env'] = {k: v.replace('{group_uri}', uri) for k, v in entry['config']['env'].items()}
        entry['config']['env'] |= {'SIXDB_GROUP_URI': uri, 'SIXDB_GROUP_MEMBER': name}
    directory.mkdir(parents=True)
    group = Group(directory / 'group.json')
    storage.write_json(group.receipt, {'format': 2, 'id': group_id, 'region': region, 'bucket': bucket,
        'uri': uri, 'source': str(directory / 'source'), 'members': members, 'network': network})
    print(f'Group: {group.receipt}', flush=True)
    worker.worker_cache.preflight(worker.ROOT)
    capture.capture(Path(source), directory / 'source')
    return group


def recover(group_id, directory=None, *, config=None):
    """Restore membership from S3; ambiguous submissions remain observations, never retries."""
    if not re.fullmatch(r'[A-Za-z0-9_-]+', group_id):
        raise ValueError('expected a group ID')
    config = config or worker.settings()
    aws = worker.Aws(config['region'])
    state = aws.get_json(config['bucket'], f'sixdb/worker-groups/{group_id}/manifest.json')
    if state is None:
        raise ValueError('group manifest not found; older groups need their original local receipt')
    if (state['id'] != group_id or state['bucket'] != config['bucket'] or state['region'] != config['region']
            or state['uri'] != f's3://{config["bucket"]}/sixdb/worker-groups/{group_id}'):
        raise ValueError('group manifest identity differs')
    directory = Path(directory or worker.ROOT / 'build/worker-groups' / group_id).resolve()
    if directory.exists():
        raise ValueError('recovery needs a new directory; existing receipts are not overwritten')
    state['launch_started'] = True
    state['recovered'] = True
    state['source_available'] = False
    for name, entry in state['members'].items():
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name) or name == 'controller' or not re.fullmatch(r'[A-Za-z0-9_-]+', entry['job']):
            raise ValueError('invalid recorded job ID')
        job = aws.get_json(config['bucket'], f'sixdb/workers/{entry["job"]}/job.json')
        if job is not None:
            if (job['id'] != entry['job'] or job['config']['env']['SIXDB_GROUP_URI'] != state['uri']
                    or job['config']['env']['SIXDB_GROUP_MEMBER'] != name):
                raise ValueError('worker belongs to a different group')
            entry.update(phase='submitting', job_created_at=job['created_at'], config=job['config'])
            worker.save(worker.JOBS / entry['job'] / 'job.json', job)
        else:
            entry['phase'] = 'planned'
    control = aws.get_json(config['bucket'], f'sixdb/worker-groups/{group_id}/control.json') or {}
    state.update({k: control[k] for k in ('aborted', 'launch_error', 'launch_complete', 'network', 'network_removed',
        'network_error', 'network_checked_at') if k in control})
    directory.mkdir(parents=True)
    storage.write_json(directory / 'group.json', state)
    print(f'Recovered: {directory / "group.json"}; submission uncertainty is preserved')
    return Group(directory / 'group.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    run = commands.add_parser('run', help='launch a new group from a JSON spec')
    run.add_argument('spec', type=Path)
    run.add_argument('--output', type=Path, help='new receipt/source directory; default build/worker-groups/ID')
    run.add_argument('--source', type=Path, default=worker.ROOT)
    run.add_argument('--detach', action='store_true')
    for name in ('wait', 'status', 'cancel', 'fetch', 'references', 'logs', 'cleanup-network'):
        sub = commands.add_parser(name)
        sub.add_argument('receipt', type=Path, help='existing group.json')
        if name == 'status':
            sub.add_argument('--json', action='store_true', help='include full lifecycle, study progress and errors')
        elif name == 'fetch':
            sub.add_argument('--member', action='append', help='select named members; default all published outputs')
            sub.add_argument('--partial', action='store_true', help='also fetch uploaded live checkpoints')
        elif name == 'references':
            sub.add_argument('--output', type=Path, help='write a compact named-member artifact manifest')
        elif name == 'logs':
            sub.add_argument('member')
            sub.add_argument('--file', choices=['script.log', 'setup.log', 'bootstrap.log'], default='script.log')
            sub.add_argument('--console', action='store_true')
    restore = commands.add_parser('recover', help='restore a lost local group receipt from S3')
    restore.add_argument('group_id')
    restore.add_argument('--output', type=Path)
    restore.add_argument('--bucket', default=worker.settings()['bucket'])
    restore.add_argument('--region', default=worker.settings()['region'])
    args = parser.parse_args()
    if args.command == 'run':
        group = create(json.loads(args.spec.read_text()), args.output, source=args.source)
        group.launch()
        return 0 if args.detach else group.wait()
    if args.command == 'recover':
        recover(args.group_id, args.output, config={'bucket': args.bucket, 'region': args.region})
        return 0
    group = Group(args.receipt)
    if args.command == 'cleanup-network':
        with storage.lock(group.directory / '.dispatch.lock'):
            return 0 if group.remove_network() else 1
    if args.command == 'status':
        state = group.status()
        print(json.dumps(state, indent=2)) if args.json else group.show(state)
        return 0
    if args.command == 'fetch':
        return group.fetch(args.member, partial=args.partial)
    if args.command == 'references':
        value = group.references()
        if args.output:
            storage.write_json(args.output, value)
            print(args.output)
        else:
            print(json.dumps(value, indent=2))
        return 0
    if args.command == 'logs':
        located = group.job(args.member)
        if not located:
            raise ValueError('member has not been submitted')
        _, job = located
        worker.logs(job, worker.Aws(job['config']['region']), console=args.console, file=args.file)
        return 0
    return getattr(group, args.command)()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        raise SystemExit(str(error))
