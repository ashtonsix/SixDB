#!/usr/bin/env python3
"""Exercise group capture, uncertain submission, observer recovery and independent cleanup offline."""
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

import storage
import worker
import worker_group as groups

PREPARE, DISPATCH = worker.prepare, worker.dispatch


class GroupCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.jobs = self.root / 'jobs'
        self.group = groups.Group(self.root / 'group/group.json')
        self.config = {'region': 'us-east-1', 'bucket': 'fixture', 'deadline_seconds': 120}
        self.members = {name: {'job': 'job-' + name, 'phase': 'planned', 'config': self.config,
                               'script': 'probe.sh', 'args': []} for name in ('server', 'client')}
        storage.write_json(self.group.receipt, {'id': 'fixture', 'region': 'us-east-1', 'bucket': 'fixture',
            'source': str(self.root), 'members': self.members})
        self.aws = Mock(spec=worker.Aws)
        self.now = 1000
        def advance(seconds):
            self.now += seconds
        self.patches = [patch.object(worker, 'JOBS', self.jobs), patch.object(worker, 'Aws', return_value=self.aws),
                        patch.object(groups.time, 'sleep', side_effect=advance),
                        patch.object(groups.time, 'time', side_effect=lambda: self.now), patch.object(worker, 'prepare', side_effect=self.prepare),
                        patch.object(worker, 'dispatch'), patch.object(worker, 'cancel'), patch.object(worker, 'wait', return_value=0)]
        self.jobs.mkdir()
        for p in self.patches:
            p.start()
            self.addCleanup(p.stop)

    def prepare(self, config, source, script, args, directory, aws):
        job = {'id': directory.name, 'created_at': time.time(), 'config': config}
        storage.write_json(directory / 'job.json', job)
        return job

    def network(self):
        value = {'name': 'sixdb-group-fixture', 'vpc_id': 'vpc-fixture', 'tcp_ports': [[43000, 43001]], 'icmp': True}
        self.group.update(network=value)
        self.present = True
        self.busy = False
        self.owned = True
        self.delete_failures = 0
        def call(service, operation, **kw):
            if operation == 'create-security-group':
                return {'GroupId': 'sg-fixture'}
            if operation == 'describe-security-groups':
                return {'SecurityGroups': [{'GroupId': 'sg-fixture', 'Tags': [{'Key': 'SixDBWorkerGroup',
                    'Value': 'fixture' if self.owned else 'another'}], 'IpPermissions': []}] if self.present else []}
            if operation == 'describe-instances':
                return {'Reservations': [{'Instances': [{'State': {'Name': 'running' if self.busy else 'terminated'}}]}]}
            if operation == 'delete-security-group':
                if self.delete_failures:
                    self.delete_failures -= 1
                    raise worker.AwsError('(DependencyViolation) when calling DeleteSecurityGroup')
                self.present = False
            return {}
        self.aws.call.side_effect = call

    def test_ids_precede_dispatch_and_wait_preserves_ordinary_reuse(self):
        def dispatch(job, directory, aws):
            entry = next(e for e in self.group.read()['members'].values() if e['job'] == job['id'])
            self.assertEqual(entry['phase'], 'submitting')
            self.assertTrue((directory / 'job.json').exists())
        worker.dispatch.side_effect = dispatch
        self.group.launch()
        self.assertEqual(self.group.wait(), 0)
        worker.cancel.assert_not_called()
        before = worker.dispatch.call_count
        with self.assertRaisesRegex(ValueError, 'already attempted'):
            self.group.launch()
        self.assertEqual(worker.dispatch.call_count, before)

    def test_lost_submission_response_and_partial_cancel_do_not_resubmit(self):
        worker.dispatch.side_effect = RuntimeError('accepted, response lost')
        with self.assertRaisesRegex(RuntimeError, 'response lost'):
            self.group.launch()
        state = self.group.read()
        self.assertEqual(state['members']['client']['phase'], 'submitting')
        self.assertEqual(state['members']['server']['phase'], 'planned')
        self.assertEqual(self.group.cancel(), 0)
        self.assertEqual([c.args[0]['id'] for c in worker.cancel.call_args_list], ['job-client'])
        self.assertEqual(worker.dispatch.call_count, 1)

    def test_cancel_during_preparation_prevents_dispatch(self):
        def preparing(*args):
            result = self.prepare(*args)
            self.group.update(aborted=True)
            return result
        worker.prepare.side_effect = preparing
        with self.assertRaisesRegex(RuntimeError, 'cancelled during preparation'):
            self.group.launch()
        worker.dispatch.assert_not_called()
        self.assertEqual(self.group.cancel(), 0)

    def test_transient_observer_and_job_lookup_errors_retry_same_jobs(self):
        self.group.launch()
        calls = []
        def wait(job, *args, **kwargs):
            calls.append(job['id'])
            if len(calls) == 1:
                raise worker.AwsError('temporary status failure')
            return 0
        worker.wait.side_effect = wait
        original = self.group.job
        with patch.object(self.group, 'job', side_effect=[RuntimeError('temporary lookup failure'), original('server'), original('server')]):
            self.assertEqual(self.group.observe('server')['exit_code'], 0)
        self.assertEqual(calls, ['job-server', 'job-server'])
        worker.cancel.assert_not_called()
        self.assertEqual(worker.dispatch.call_count, 2)

    def test_detaching_observation_does_not_cancel_or_record_a_failed_run(self):
        self.group.launch()
        stop = threading.Event()
        stop.set()
        self.assertEqual(self.group.observe('server', stop), {'detached': True})
        self.assertNotIn('collection', self.group.read()['members']['server'])
        worker.wait.assert_not_called()
        worker.cancel.assert_not_called()

    def test_interrupt_releases_parallel_observers_without_cancelling_jobs(self):
        self.group.launch()
        observing = threading.Event()
        def wait(job, directory, aws, *, stop):
            if job['id'] == 'job-client':
                if not observing.wait(3):
                    raise AssertionError('second observer did not start')
                raise KeyboardInterrupt()
            observing.set()
            if not stop.wait(3):
                raise AssertionError('observer was not detached')
            raise InterruptedError('observer detached')
        worker.wait.side_effect = wait
        with self.assertRaises(KeyboardInterrupt):
            self.group.wait()
        worker.cancel.assert_not_called()
        self.assertTrue(all('collection' not in e for e in self.group.read()['members'].values()))

    def test_observer_deadline_is_recorded_before_cancellation(self):
        self.group.launch()
        for name in self.members:
            self.group.update(name, job_created_at=0)
        worker.wait.side_effect = worker.AwsError('observation unavailable')
        def cancel(job, aws):
            entry = next(e for e in self.group.read()['members'].values() if e['job'] == job['id'])
            self.assertTrue(entry['collection']['deadline_elapsed'])
        worker.cancel.side_effect = cancel
        self.assertEqual(self.group.wait(), 1)
        self.assertEqual(worker.cancel.call_count, 2)

    def test_failed_measurement_is_collected_without_replacement(self):
        self.group.launch()
        worker.wait.return_value = 1
        self.assertEqual(self.group.wait(), 1)
        self.assertEqual(worker.dispatch.call_count, 2)
        self.assertTrue(all(e['collection']['exit_code'] == 1 for e in self.group.read()['members'].values()))

    def test_cancel_failure_does_not_skip_other_members_or_hide_results(self):
        self.network()
        self.group.launch()
        self.busy = True
        def cancel(job, aws):
            if job['id'] == 'job-server':
                raise worker.AwsError('temporary cancellation failure')
        worker.cancel.side_effect = cancel
        self.assertEqual(self.group.wait(), 1)
        state = self.group.read()
        self.assertEqual(len(worker.cancel.call_args_list), 4)
        self.assertIn('error', state['members']['server']['cancellation'])
        self.assertTrue(state['members']['client']['cancellation']['requested'])
        self.assertEqual(state['members']['server']['collection']['exit_code'], 0)
        self.assertIn('network_error', state)
        self.assertTrue(self.present)
        worker.cancel.side_effect = None
        self.busy = False
        self.assertEqual(self.group.wait(), 0)
        self.assertFalse(self.present)

    def test_network_cleanup_retries_dependencies_and_accepts_absence(self):
        self.network()
        self.group.launch()
        self.delete_failures = 1
        self.assertEqual(self.group.wait(), 0)
        deletes = [c for c in self.aws.call.call_args_list if c.args[1] == 'delete-security-group']
        self.assertEqual(len(deletes), 2)
        self.group.update(network_removed=False)  # Simulate loss of the final local acknowledgment.
        self.assertEqual(self.group.wait(), 0)
        self.assertTrue(self.group.read()['network_removed'])

    def test_unknown_network_create_is_discovered_by_owned_name(self):
        self.network()
        original = self.aws.call.side_effect
        def call(service, operation, **kw):
            if operation == 'create-security-group':
                raise worker.AwsError('create accepted, response lost')
            return original(service, operation, **kw)
        self.aws.call.side_effect = call
        with self.assertRaises(worker.AwsError):
            self.group.launch()
        self.assertNotIn('id', self.group.read()['network'])
        self.assertEqual(self.group.cancel(), 0)
        self.assertFalse(self.present)
        worker.dispatch.assert_not_called()

    def test_cleanup_preserves_unowned_network(self):
        self.network()
        self.owned = False
        self.assertEqual(self.group.cancel(), 1)
        self.assertTrue(self.present)
        self.assertIn('ownership', self.group.read()['network_error'])

    def test_wait_before_launch_cannot_mark_future_network_cleaned(self):
        self.network()
        with self.assertRaisesRegex(ValueError, 'launch has not started'):
            self.group.wait()
        self.assertNotIn('network_removed', self.group.read())
        self.aws.call.assert_not_called()

    def test_updates_from_separate_controllers_preserve_other_outcomes(self):
        other = groups.Group(self.group.receipt)
        self.group.update('server', collection={'exit_code': 0})
        other.update(aborted=True)
        self.group.update('client', collection={'error': 'failed observation'})
        state = self.group.read()
        self.assertTrue(state['aborted'])
        self.assertEqual(state['members']['server']['collection']['exit_code'], 0)

    def test_create_captures_once_and_resolves_named_configs(self):
        source = self.root / 'source'
        source.mkdir()
        (source / 'probe.sh').write_text('#!/bin/sh\ntrue\n')
        subprocess.run(['git', 'init', '-q', str(source)], check=True)
        subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(source), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                        'commit', '-qm', 'fixture'], check=True)
        spec = {'script': 'probe.sh', 'config': {'env': {'SHARED': 'x', 'PEERS': '{group_uri}'}}, 'members': {
            'zen': {'config': {'machine': 'zen5'}},
            'arm': {'config': {'machine': 'neoverse-v2', 'env': {'CASE': 'one'}}}}}
        with patch.object(worker, 'resolve', side_effect=lambda config, aws: config), \
                patch.object(worker.worker_cache, 'preflight'), \
                patch.object(groups.capture, 'capture', wraps=groups.capture.capture) as capture:
            created = groups.create(spec, self.root / 'new-group', source=source)
            self.assertEqual(capture.call_count, 1)
            state = created.read()
            self.assertEqual(state['members']['arm']['config']['instance_type'], 'c8g.medium')
            for name, entry in state['members'].items():
                self.assertEqual(entry['config']['capacity'], 'spot')
                self.assertFalse(entry['config']['fresh'])
                self.assertEqual(entry['config']['idle_seconds'], 300)
                self.assertEqual(entry['config']['env']['PEERS'], state['uri'])
                self.assertEqual(entry['config']['env']['SIXDB_GROUP_MEMBER'], name)
            (source / 'probe.sh').write_text('changed after capture\n')
            self.assertEqual((Path(state['source']) / 'probe.sh').read_text(), '#!/bin/sh\ntrue\n')
            spec['network'] = {'vpc_id': 'vpc-fixture', 'tcp_ports': [[1234, 1234]]}
            private = groups.create(spec, self.root / 'private-group', source=source)
            self.assertTrue(all(e['config']['fresh'] and e['config']['idle_seconds'] == 0
                                for e in private.read()['members'].values()))
            with self.assertRaisesRegex(ValueError, 'existing group'):
                groups.create(spec, self.root / 'new-group', source=source)

    def test_real_preparation_and_dispatch_use_identical_capture_and_literal_arguments(self):
        source = self.root / 'source'
        (source / 'workbench/tools').mkdir(parents=True)
        (source / 'workbench/tools/worker-setup.sh').write_text('#!/bin/sh\ntrue\n')
        (source / 'probe.sh').write_text('#!/bin/sh\ntrue\n')
        subprocess.run(['git', 'init', '-q', str(source)], check=True)
        subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(source), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                        'commit', '-qm', 'fixture'], check=True)
        def resolve(config, aws):
            return config | {'architecture': 'x86_64', 'root_device': '/dev/sda1',
                'subnets': [{'id': 'subnet-a', 'zone': 'us-east-1a'}],
                'hardware': {'VCpuInfo': {'DefaultCores': 1, 'DefaultThreadsPerCore': 1},
                             'MemoryInfo': {'SizeInMiB': 2048}}}
        requests = []
        def call(service, operation, **request):
            self.assertEqual(operation, 'run-instances')
            requests.append(request)
            return {'Instances': [{'InstanceId': 'i-' + str(len(requests))}]}
        self.aws.call.side_effect = call
        worker.prepare.side_effect = PREPARE
        worker.dispatch.side_effect = DISPATCH
        spec = {'script': 'probe.sh', 'args': ['a b', '$(literal)'],
                'config': {'fresh': True, 'security_group_id': 'sg-existing', 'instance_profile': 'existing'},
                'members': {'a': {}, 'b': {}}}
        with patch.object(worker, 'resolve', side_effect=resolve), patch.object(worker.worker_cache, 'preflight'):
            group = groups.create(spec, self.root / 'real-group', source=source)
            (source / 'probe.sh').write_text('changed\n')
            group.launch()
        entries = group.read()['members'].values()
        jobs = [json.loads((self.jobs / e['job'] / 'job.json').read_text()) for e in entries]
        self.assertEqual(len(requests), 2)
        self.assertTrue(all(r['InstanceMarketOptions']['MarketType'] == 'spot' for r in requests))
        self.assertEqual(jobs[0]['source']['sha256'], jobs[1]['source']['sha256'])
        self.assertEqual(jobs[0]['source']['digest'], jobs[1]['source']['digest'])
        self.assertEqual(jobs[0]['args'], ['a b', '$(literal)'])
        self.assertEqual({j['config']['env']['SIXDB_GROUP_MEMBER'] for j in jobs}, {'a', 'b'})


if __name__ == '__main__':
    unittest.main()
