#!/usr/bin/env python3
"""Offline lifecycle checks: no EC2 allocations, real scripts and local archives."""
import base64
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

import artifacts
import datasets
import worker
import worker_runtime as runtime


def job():
    return {'format': 1, 'id': 'test-worker', 'created_at': time.time(),
        'uri': 's3://test-bucket/sixdb/workers/test-worker', 'prefix': 'sixdb/workers/test-worker',
        'script': 'probe.sh', 'args': ['a b', '$(literal)'], 'source_commit': 'original-commit',
        'source': {'sha256': '', 'runtime_sha256': '0' * 64, 'digest': 'captured'},
        'config': {'bucket': 'test-bucket', 'region': 'us-east-1', 'instance_type': 'c8a.medium',
            'ami': 'ami-test', 'disk_gb': 24, 'root_device': '/dev/sda1', 'deadline_seconds': 300,
            'instance_profile': 'sixdb-worker', 'security_group_id': 'sg-test', 'capacity': 'on-demand',
            'subnets': [{'id': 'subnet-a', 'zone': 'us-east-1a'}, {'id': 'subnet-b', 'zone': 'us-east-1b'}],
            'setup': 'minimal', 'sync_seconds': 0, 'env': {'VALUE': 'with spaces; $(not executed)'}}}


class LaunchTests(unittest.TestCase):
    def test_request_shutdown_volumes_architecture_and_exact_arguments(self):
        value = job()
        request = worker.launch_request(value, value['config']['subnets'][0], 'spot')
        self.assertEqual(request['InstanceType'], 'c8a.medium')
        self.assertEqual(request['InstanceMarketOptions']['SpotOptions']['SpotInstanceType'], 'one-time')
        self.assertEqual(request['InstanceInitiatedShutdownBehavior'], 'terminate')
        self.assertTrue(request['BlockDeviceMappings'][0]['Ebs']['DeleteOnTermination'])
        self.assertNotIn('KeyName', request)
        script = base64.b64decode(request['UserData']).decode()
        self.assertLess(script.index('sixdb-deadline'), script.index('apt-get'))
        subprocess.run(['bash', '-n'], input=script, text=True, check=True)
        payload = script.split('echo ', 1)[1].split(' | base64', 1)[0]
        recovered = json.loads(base64.b64decode(payload))
        self.assertEqual(recovered['args'], ['a b', '$(literal)'])
        self.assertEqual(recovered['config']['env'], value['config']['env'])
        with self.assertRaises(ValueError):
            worker.environment(['SIXDB_RESULTS=elsewhere'])

    def test_launch_capacity_fallback_and_transport_reuse(self):
        class Fake:
            def __init__(self):
                self.requests = []
            def call(self, service, operation, **request):
                self.requests.append(request)
                if len(self.requests) == 1:
                    raise worker.AwsError('Connection timed out')
                if 'InstanceMarketOptions' in request:
                    raise worker.AwsError('An error occurred (InsufficientInstanceCapacity) when calling RunInstances')
                return {'Instances': [{'InstanceId': 'i-test'}]}
            def upload(self, *args):
                pass
        with tempfile.TemporaryDirectory() as temp, patch.object(worker.time, 'sleep'):
            value = job()
            value['config']['capacity'] = 'spot-or-on-demand'
            aws = Fake()
            result = worker.launch(value, Path(temp), aws)
            self.assertEqual(result['capacity'], 'on-demand')
            self.assertEqual(aws.requests[0], aws.requests[1])
            self.assertEqual({r['InstanceType'] for r in aws.requests}, {'c8a.medium'})
            self.assertEqual(len(aws.requests), 4)

    def test_spot_exhaustion_suggests_explicit_fallback_without_using_it(self):
        value = job()
        value['config']['capacity'] = 'spot'
        aws = Mock(spec=worker.Aws)
        aws.call.side_effect = worker.AwsError(
            'An error occurred (InsufficientInstanceCapacity) when calling RunInstances')
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, '--capacity spot-or-on-demand'):
                worker.launch(value, Path(temp), aws)
        self.assertEqual(aws.call.call_count, len(value['config']['subnets']))
        self.assertTrue(all('InstanceMarketOptions' in call.kwargs for call in aws.call.call_args_list))

    def test_uncertain_launch_and_permission_errors_do_not_try_new_capacity(self):
        class Fake:
            def __init__(self, message):
                self.message, self.requests = message, []
            def call(self, service, operation, **request):
                self.requests.append(request)
                raise worker.AwsError(self.message)
        with tempfile.TemporaryDirectory() as temp, patch.object(worker.time, 'sleep'):
            for message, count in [('network timeout', 4),
                ('An error occurred (UnauthorizedOperation) when calling RunInstances', 1)]:
                aws = Fake(message)
                with self.assertRaises(worker.AwsError):
                    worker.launch(job(), Path(temp), aws)
                self.assertEqual(len(aws.requests), count)
                self.assertTrue(all(r == aws.requests[0] for r in aws.requests))

    def test_cancel_scopes_to_job_and_project(self):
        class Fake:
            def call(self, service, operation, **request):
                if operation == 'describe-instances':
                    self.filters = request['Filters']
                    return {'Reservations': [{'Instances': [
                        {'InstanceId': 'i-active', 'State': {'Name': 'running'}},
                        {'InstanceId': 'i-done', 'State': {'Name': 'terminated'}}]}]}
                self.terminated = request['InstanceIds']
        aws = Fake()
        worker.cancel(job(), aws)
        self.assertEqual(aws.terminated, ['i-active'])
        self.assertIn({'Name': 'tag:SixDBWorkerJob', 'Values': ['test-worker']}, aws.filters)
        self.assertIn({'Name': 'tag:Project', 'Values': ['SixDB']}, aws.filters)


class LogTests(unittest.TestCase):
    def test_missing_log_explains_sync_and_completion_without_console_fallback(self):
        value = job()
        aws = Mock(spec=worker.Aws)
        aws.get_bytes.return_value = None
        for state, sync, expected in [('running', 0, 'Live sync is disabled'),
                                      ('downloading', 15, 'every 15 seconds'),
                                      ('failed', 0, 'script may not have started')]:
            value['config']['sync_seconds'] = sync
            output = io.StringIO()
            with patch.object(worker, 'status', return_value={'state': state}), patch('sys.stdout', output):
                worker.logs(value, aws)
            self.assertIn(expected, output.getvalue())
            self.assertIn('logs test-worker --console', output.getvalue())
        aws.call.assert_not_called()

    def test_uploaded_log_including_empty_log_is_shown_without_state_lookup(self):
        aws = Mock(spec=worker.Aws)
        for data in [b'', 'compiler finished ✓\n'.encode()]:
            aws.get_bytes.return_value = data
            output = io.StringIO()
            with patch('sys.stdout', output), patch.object(worker, 'status') as status:
                worker.logs(job(), aws)
            self.assertEqual(output.getvalue(), data.decode())
            status.assert_not_called()
        aws.call.assert_not_called()

    def test_console_is_already_decoded_and_missing_output_is_explained(self):
        aws = Mock(spec=worker.Aws)
        for data in ['[  OK  ] Started café service\n', 'YWJj', None]:
            aws.call.return_value = {'Output': data}
            output = io.StringIO()
            with patch('sys.stdout', output), patch.object(worker, 'instances', return_value=[{'InstanceId': 'i-test'}]):
                worker.logs(job(), aws, console=True)
            self.assertIn(data or 'No EC2 console output', output.getvalue())
        aws.get_bytes.assert_not_called()

    def test_object_reads_distinguish_absence_from_permission_and_transport_errors(self):
        aws = worker.Aws('us-east-1')
        for code in ['NoSuchKey', '404', 'AccessDenied', 'TransportError']:
            result = subprocess.CompletedProcess([], 1, '', f'An error occurred ({code}) when calling GetObject')
            with patch.object(worker.subprocess, 'run', return_value=result):
                if code in {'NoSuchKey', '404'}:
                    self.assertIsNone(aws.get_bytes('bucket', 'missing'))
                else:
                    with self.assertRaises(worker.AwsError):
                        worker.logs(job(), aws)

        def download(argv, **kwargs):
            Path(argv[7]).write_bytes('{"text": "café"}'.encode())
            return subprocess.CompletedProcess(argv, 0, '{}', '')
        with patch.object(worker.subprocess, 'run', side_effect=download):
            self.assertEqual(aws.get_json('bucket', 'status'), {'text': 'café'})

    def test_console_cli_option_reaches_log_reader(self):
        with patch.object(worker.sys, 'argv', ['worker.py', 'logs', 'test-worker', '--console', '--file', 'setup.log']), \
                patch.object(worker, 'locate', return_value=(Path('unused'), job())), \
                patch.object(worker, 'logs') as logs:
            self.assertEqual(worker.main(), 0)
        self.assertTrue(logs.call_args.kwargs['console'])
        self.assertEqual(logs.call_args.kwargs['file'], 'setup.log')


class RuntimeTests(unittest.TestCase):
    def fixture(self, directory, script):
        value = job()
        store = directory / 'store'
        store.mkdir()
        source = store / 'source.tar.gz'
        with tarfile.open(source, 'w:gz') as archive:
            data = script.encode()
            info = tarfile.TarInfo('probe.sh')
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        value['source']['sha256'] = runtime.digest(source)
        (store / 'source-manifest.json').write_text('{}')
        base = directory / 'runtime'
        base.mkdir()
        running = runtime.Worker(value, base)
        events = []
        def aws(*args, **kwargs):
            if args[1] == 'cp':
                source_path, target = args[-2:]
                if source_path.startswith('s3:'):
                    shutil.copyfile(store / source_path.rsplit('/', 1)[1], target)
                else:
                    if target.endswith('/status.json'):
                        events.append(json.loads(Path(source_path).read_text()))
            elif args[1] == 'sync':
                pass
            else:
                raise AssertionError(args)
            return subprocess.CompletedProcess(args, 0, stdout=b'', stderr=b'')
        running.aws = aws
        def publish(results, temp, **kwargs):
            self.assertEqual(kwargs, {'bucket': 'test-bucket', 'region': 'us-east-1', 'validate_run': False})
            count = artifacts.pack(results, temp / 'bundle.tar.gz', validate_run=False)
            artifacts.unpack(temp / 'bundle.tar.gz', directory / 'recovered')
            return {'files': count, 'sha256': runtime.digest(temp / 'bundle.tar.gz')}
        return running, events, publish

    def test_success_and_failure_keep_complete_output_and_original_identity(self):
        for code in (0, 7):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp)
                running, events, publish = self.fixture(directory,
                    f'printf "%s\\n" "$VALUE" "$1" "$2" > "$SIXDB_RESULTS/values.txt"\nexit {code}\n')
                with patch.object(artifacts, 'publish', side_effect=publish), patch.object(runtime, 'metadata', return_value={}):
                    self.assertEqual(running.run(), 0 if code == 0 else 1)
                self.assertEqual(events[-1]['state'], 'complete' if code == 0 else 'failed')
                self.assertEqual(events[-1]['script_returncode'], code)
                if code:
                    self.assertEqual(events[-1]['failure_phase'], 'running')
                restored = directory / 'recovered'
                self.assertEqual((restored / 'values.txt').read_text(),
                    'with spaces; $(not executed)\na b\n$(literal)\n')
                self.assertEqual(json.loads((restored / 'job.json').read_text())['source_commit'], 'original-commit')
                self.assertTrue((restored / 'source.tar.gz').exists())

    def test_setup_failure_keeps_phase_and_log_in_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            running, events, publish = self.fixture(directory, 'touch should-not-run\n')
            running.config['setup'] = 'toolchain'
            def execute(command, output, *args):
                self.assertIn('worker-setup.sh', command[-1])
                output.write_text('package index failed\n')
                return 17
            with patch.object(running, 'execute', side_effect=execute), patch.object(artifacts, 'publish', side_effect=publish):
                self.assertEqual(running.run(), 1)
            self.assertEqual(events[-1]['failure_phase'], 'setting-up')
            self.assertIn('setup exited 17', events[-1]['error'])
            self.assertEqual((directory / 'recovered/setup.log').read_text(), 'package index failed\n')
            self.assertFalse((running.source / 'should-not-run').exists())

    def test_timeout_and_upload_failure_never_report_success(self):
        with tempfile.TemporaryDirectory() as temp:
            running, events, publish = self.fixture(Path(temp), 'sleep 30\n')
            running.execution_end = time.time() + 2
            with patch.object(artifacts, 'publish', side_effect=publish), patch.object(runtime, 'metadata', return_value={}):
                self.assertEqual(running.run(), 1)
            self.assertEqual(events[-1]['state'], 'timeout')
        with tempfile.TemporaryDirectory() as temp:
            running, events, _ = self.fixture(Path(temp), 'exit 0\n')
            with patch.object(artifacts, 'publish', side_effect=RuntimeError('upload unavailable')), patch.object(runtime, 'metadata', return_value={}):
                self.assertEqual(running.run(), 1)
            self.assertEqual(events[-1]['state'], 'upload-failed')
            self.assertTrue((running.results / 'worker-result.json').exists())
            self.assertNotIn('artifact', events[-1])

    def test_source_corruption_and_unsafe_archives(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            running, events, publish = self.fixture(directory, 'touch should-not-run\n')
            running.job['source']['sha256'] = 'incorrect'
            with patch.object(artifacts, 'publish', side_effect=publish):
                self.assertEqual(running.run(), 1)
            self.assertEqual(events[-1]['state'], 'failed')
            self.assertFalse((running.source / 'should-not-run').exists())
        with tempfile.TemporaryDirectory() as temp:
            archive_path = Path(temp) / 'bad.tar'
            with tarfile.open(archive_path, 'w') as archive:
                entry = tarfile.TarInfo('../escape')
                archive.addfile(entry, io.BytesIO(b''))
            with self.assertRaises(ValueError):
                runtime.extract(archive_path, Path(temp) / 'extracted')

    def test_nested_prepared_inputs_and_unfinished_receipts_remain_recoverable(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {}, clear=False):
            directory = Path(temp)
            os.environ['SIXDB_DATA_CACHE'] = str(directory / 'cache')
            recipe = directory / 'recipe.py'
            recipe.write_text('fixture recipe')
            with patch.object(datasets, 'ROOT', directory):
                prepared = datasets.cached('fixture', recipe, {}, {}, lambda p: (p / 'values').write_text('original'))
                meta = datasets.verify(prepared)
                receipt = {'status': 'running', 'source_files_sha256': {},
                    'inputs': {'inputs': {'path': str(prepared), 'id': meta['id'], 'key': meta['key']}}}
                script = 'mkdir -p "$SIXDB_RESULTS/nested"\n'
                script += "printf '%s' '" + json.dumps(receipt) + "' > \"$SIXDB_RESULTS/nested/run.json\"\nexit 7\n"
                running, events, publish = self.fixture(directory, script)
                reference = {'id': meta['id'], 'key': meta['key'], 'artifact': {'fixture': True}}
                with patch.object(artifacts, 'publish', side_effect=publish), \
                        patch.object(datasets, 'publish', return_value=reference), patch.object(runtime, 'metadata', return_value={}):
                    self.assertEqual(running.run(), 1)
                restored = directory / 'recovered/nested'
                self.assertEqual(events[-1]['state'], 'failed')
                self.assertTrue((restored / 'input-artifacts.json').exists())
                self.assertFalse((restored / 'inputs').exists())
                with patch.object(datasets, 'restore', return_value=prepared):
                    artifacts.restore_inputs(restored)
                    artifacts.restore_inputs(restored)  # repeat fetch after partial restoration
                self.assertEqual((restored / 'inputs/values').read_text(), 'original')

    def test_interruption_and_arbitrary_run_json(self):
        with tempfile.TemporaryDirectory() as temp:
            running, events, publish = self.fixture(Path(temp), 'exit 0\n')
            with patch.object(artifacts, 'publish', side_effect=publish), patch.object(runtime, 'metadata', return_value={}), \
                    patch.object(running, 'execute', side_effect=InterruptedError('test notice')):
                self.assertEqual(running.run(), 1)
            self.assertEqual(events[-1]['state'], 'interrupted')
        with tempfile.TemporaryDirectory() as temp:
            running, events, publish = self.fixture(Path(temp), 'echo arbitrary > "$SIXDB_RESULTS/run.json"\n')
            with patch.object(artifacts, 'publish', side_effect=publish), patch.object(runtime, 'metadata', return_value={}):
                self.assertEqual(running.run(), 0)
            self.assertEqual(events[-1]['state'], 'complete')


if __name__ == '__main__':
    unittest.main()
