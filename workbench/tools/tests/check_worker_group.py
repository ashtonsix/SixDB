#!/usr/bin/env python3
"""Exercise group capture, uncertain submission, observer recovery and independent cleanup offline."""
import json
import io
from contextlib import redirect_stdout
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
        self.objects = {}
        self.aws.get_json.side_effect = lambda bucket, key: self.objects.get(key)
        self.aws.upload.side_effect = lambda path, uri, **kw: self.objects.update({uri.removeprefix('s3://fixture/'): json.loads(Path(path).read_text())}) if str(path).endswith('.json') else None
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

    def test_remote_manifest_precedes_dispatch_and_recovery_never_resubmits(self):
        def dispatch(job, directory, aws):
            manifest = self.objects['sixdb/worker-groups/fixture/manifest.json']
            self.assertEqual(set(manifest['members']), {'server', 'client'})
            self.objects[f'sixdb/workers/{job["id"]}/job.json'] = job | {'config': job['config'] | {
                'env': {'SIXDB_GROUP_URI': self.group.uri(), 'SIXDB_GROUP_MEMBER': job['id'].removeprefix('job-')}}}
        worker.dispatch.side_effect = dispatch
        self.group.launch()
        restored = groups.recover('fixture', self.root / 'restored', config=self.config)
        self.assertTrue(restored.read()['launch_complete'])
        self.assertTrue(restored.read()['recovered'])
        self.assertTrue(all(e['phase'] == 'submitting' for e in restored.read()['members'].values()))
        with self.assertRaises(ValueError):
            restored.launch()
        self.assertEqual(worker.dispatch.call_count, 2)
        self.group.cancel()
        control = self.objects['sixdb/worker-groups/fixture/control.json']
        self.assertTrue(control['aborted'])
        self.assertTrue(control['launch_complete'])

    def test_partial_collection_leaves_active_peers_alone_and_preserves_archive(self):
        self.group.launch()
        def status(job, aws):
            return {'state': 'failed', 'artifact': {'manifest': 's3://fixture/archive'}} if job['id'] == 'job-server' else {'state': 'running'}
        with patch.object(worker, 'status', side_effect=status), patch.object(worker, 'fetch') as fetch:
            self.assertEqual(self.group.fetch(), 0)
            self.assertEqual([c.args[0]['id'] for c in fetch.call_args_list], ['job-server'])
        archive = self.group.read()['members']['server']['collection']['artifact']
        with patch.object(worker, 'status', return_value=None), patch.object(worker, 'fetch'):
            self.assertEqual(self.group.fetch(['server'], partial=True), 0)
        self.assertEqual(self.group.read()['members']['server']['collection']['artifact'], archive)
        worker.cancel.assert_not_called()
        worker.wait.assert_not_called()

    def test_status_joins_named_progress_and_root_cause_but_rejects_stale_progress(self):
        self.group.launch()
        self.objects['sixdb/worker-groups/fixture/members/server/progress.json'] = {
            'job': 'job-server', 'member': 'server', 'phase': 'measuring', 'case': 'batch-8', 'updated_at': 900}
        self.objects['sixdb/worker-groups/fixture/members/client/progress.json'] = {
            'job': 'old-client', 'member': 'client', 'phase': 'ready', 'updated_at': 1000}
        with patch.object(worker, 'status', return_value={'state': 'failed', 'failure_phase': 'setting-up', 'error': 'apt failed'}):
            state = self.group.status()
        self.assertNotIn('progress', state['members']['client'])
        output = io.StringIO()
        with redirect_stdout(output):
            self.group.show(state)
        self.assertIn('server: failed | last progress: measuring; reported 100s ago', output.getvalue())
        self.assertIn('apt failed', output.getvalue())
        self.assertIn('--file setup.log', output.getvalue())

    def test_resource_termination_is_not_erased_when_ec2_ages_out_old_instances(self):
        self.group.launch()
        self.group.update('server', instances=[{'id': 'i-old', 'state': 'terminated'}], resource_observed_at=900)
        with patch.object(worker, 'status', return_value={'state': 'complete'}), patch.object(worker, 'instances', return_value=[]):
            state = self.group.references()
        server = state['members']['server']
        self.assertEqual(server['instances'], [{'id': 'i-old', 'state': 'terminated'}])
        self.assertEqual(server['resource_observed_at'], 900)
        self.assertIn('last observation', server['resource_note'])

    def test_scoped_wait_retains_hosts_and_cancel_does_not_remove_shared_network(self):
        self.group.launch()
        self.group.update(network={'scope': 'study', 'name': 'scope', 'vpc_id': 'vpc-fixture'})
        with patch.object(self.group, 'remove_network') as remove:
            self.assertEqual(self.group.wait(), 0)
            worker.cancel.assert_not_called()
            remove.assert_not_called()
            self.assertEqual(self.group.cancel(), 0)
            self.assertEqual(worker.cancel.call_count, 2)
            remove.assert_not_called()

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
        self.group.update(network=self.group.read()['network'] | {'udp_ports': [[43002, 43003]]})
        self.group.launch()
        rules = next(c.kwargs['IpPermissions'] for c in self.aws.call.call_args_list
                     if c.args[1] == 'authorize-security-group-ingress')
        self.assertEqual(rules, [
            {'IpProtocol': 'tcp', 'FromPort': 43000, 'ToPort': 43001, 'UserIdGroupPairs': [{'GroupId': 'sg-fixture'}]},
            {'IpProtocol': 'udp', 'FromPort': 43002, 'ToPort': 43003, 'UserIdGroupPairs': [{'GroupId': 'sg-fixture'}]},
            {'IpProtocol': 'icmp', 'FromPort': -1, 'ToPort': -1, 'UserIdGroupPairs': [{'GroupId': 'sg-fixture'}]}])
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
            spec['network']['scope'] = 'repeat-study'
            scoped = groups.create(spec, self.root / 'scoped-group', source=source)
            self.assertTrue(all(not e['config']['fresh'] and e['config']['idle_seconds'] == 300
                                for e in scoped.read()['members'].values()))
            with self.assertRaisesRegex(ValueError, 'existing group'):
                groups.create(spec, self.root / 'new-group', source=source)

    def test_invalid_udp_ranges_fail_before_capture_or_cloud_calls(self):
        for ports in ([[0, 3]], [[3, 2]], [[1, 65536]], [[True, 2]], [['1', 2]], [3]):
            with self.subTest(ports=ports), patch.object(groups.capture, 'capture') as capture:
                with self.assertRaisesRegex(ValueError, 'TCP/UDP'):
                    groups.create({'script': 'probe.sh', 'members': {'a': {}},
                        'network': {'vpc_id': 'vpc-fixture', 'udp_ports': ports}}, self.root / 'bad-group')
                capture.assert_not_called()
                self.aws.call.assert_not_called()

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
