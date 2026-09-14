#!/usr/bin/env python3
"""Startup failure propagation, rendezvous identity, bounded reads and observational progress."""
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

import worker
from worker_context import GroupContext


class ContextCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.aws = Mock(spec=worker.Aws)
        self.objects = {}
        self.aws.get_object.side_effect = lambda bucket, key, **kw: (json.dumps(self.objects[key]).encode(), {}) if key in self.objects else (None, {})
        self.aws.upload.side_effect = lambda path, uri, **kw: self.objects.update({uri.removeprefix('s3://fixture/'): json.loads(Path(path).read_text())})
        self.job = {'id': 'job-a', 'created_at': time.time(), 'config': {'region': 'us-east-1', 'bucket': 'fixture',
            'deadline_seconds': 300, 'env': {'SIXDB_GROUP_MEMBER': 'a', 'SIXDB_GROUP_URI': 's3://fixture/sixdb/worker-groups/group'}}}
        self.manifest = {'id': 'group', 'uri': self.job['config']['env']['SIXDB_GROUP_URI'],
                         'members': {'a': {'job': 'job-a'}, 'b': {'job': 'job-b'}}}
        self.context = GroupContext(self.job, self.root, self.manifest, self.aws)

    def value(self, name='ready', *, job='job-b', member='b', value=42):
        self.objects[f'sixdb/worker-groups/group/values/{name}/{member}.json'] = {'job': job, 'member': member, 'value': value}

    def test_setup_failure_before_any_script_has_root_cause_and_log(self):
        self.objects['sixdb/workers/job-b/status.json'] = {'state': 'failed', 'failure_phase': 'setting-up', 'error': 'package index unavailable'}
        with self.assertRaisesRegex(RuntimeError, 'peer b.*setting-up.*package index unavailable.*--file setup.log'):
            self.context.wait_ready(['b'])
        self.assertEqual(json.loads((self.root / 'progress.json').read_text())['phase'], 'failed')

    def test_peer_that_exited_is_not_live_while_its_archive_uploads(self):
        self.value()
        self.objects['sixdb/workers/job-b/status.json'] = {'state': 'uploading', 'script_returncode': 0}
        with self.assertRaisesRegex(RuntimeError, 'peer b.*uploading'):
            self.context.wait_ready(['b'])

    def test_durable_value_from_completed_producer_is_not_live_readiness(self):
        self.value()
        self.objects['sixdb/workers/job-b/status.json'] = {'state': 'complete'}
        self.assertEqual(self.context.wait_values('ready', ['b']), {'b': 42})
        with self.assertRaisesRegex(RuntimeError, 'peer b.*complete'):
            self.context.wait_ready(['b'])

    def test_completed_without_required_value_and_cancelled_launch_fail_promptly(self):
        self.objects['sixdb/workers/job-b/status.json'] = {'state': 'complete'}
        with self.assertRaisesRegex(RuntimeError, 'peer b'):
            self.context.wait_values('output', ['b'])
        for control in ({'aborted': True}, {'launch_error': 'capacity exhausted'}):
            self.objects['sixdb/worker-groups/group/control.json'] = control
            with self.assertRaisesRegex(RuntimeError, 'group stopped'):
                self.context.wait_ready(['b'])

    def test_missing_peer_can_arrive_during_wait(self):
        with patch('worker_context.time.sleep', side_effect=lambda _: self.value()):
            self.assertEqual(self.context.wait_ready(['b']), {'b': 42})

    def test_old_job_values_cannot_satisfy_new_group(self):
        self.value(job='old-job')
        with self.assertRaisesRegex(ValueError, 'stale or mismatched'):
            self.context.wait_ready(['b'])
        with self.assertRaisesRegex(ValueError, 'identity differs'):
            GroupContext(self.job | {'id': 'old-job'}, self.root, self.manifest, self.aws)

    def test_controller_gate_and_null_ready_payload(self):
        self.value(name='network-configured', job='group', member='controller', value={'mtu': 1500})
        self.assertEqual(self.context.wait_values('network-configured', ['controller']), {'controller': {'mtu': 1500}})
        self.context.ready()
        self.assertEqual(self.context.wait_ready(['a']), {'a': None})

    def test_observational_upload_failure_preserves_local_progress_and_previous_remote_age(self):
        self.context.progress('measuring', case=3)
        previous = dict(self.objects['sixdb/worker-groups/group/members/a/progress.json'])
        self.aws.upload.side_effect = OSError('upload unavailable')
        result = self.context.progress('summarizing', completed=4)
        self.assertEqual(result['publication_error'], 'upload unavailable')
        self.assertEqual(json.loads((self.root / 'progress.json').read_text())['phase'], 'summarizing')
        self.assertEqual(self.objects['sixdb/worker-groups/group/members/a/progress.json'], previous)
        with self.assertRaises(OSError):
            self.context.ready()  # Required exchange has different failure semantics.

    def test_read_retries_transient_errors_but_not_access_denied(self):
        self.aws.get_object.side_effect = [worker.AwsError('connection lost'), (b'{}', {})]
        with patch('worker_context.time.sleep'):
            self.assertEqual(self.context.read('control.json'), {})
        self.assertEqual(self.aws.get_object.call_count, 2)
        self.aws.get_object.reset_mock()
        self.aws.get_object.side_effect = worker.AwsError('(AccessDenied) when calling GetObject')
        with self.assertRaises(worker.AwsError):
            self.context.wait_ready(['b'])
        self.assertEqual(self.aws.get_object.call_count, 1)

    def test_timeout_names_missing_peers_and_retries_are_bounded(self):
        with self.assertRaisesRegex(TimeoutError, 'waiting for b'):
            self.context.wait_ready(['b'], timeout=.01)
        self.aws.get_object.reset_mock()
        self.aws.get_object.side_effect = worker.AwsError('temporarily unreadable')
        with patch('worker_context.time.sleep'), self.assertRaises(worker.AwsError):
            self.context.read('control.json')
        self.assertEqual(self.aws.get_object.call_count, 3)


if __name__ == '__main__':
    unittest.main()
