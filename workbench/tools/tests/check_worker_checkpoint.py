#!/usr/bin/env python3
"""Checkpoint failures, data integrity, deduplication and reactive worker ordering (offline)."""
import copy
import json
import os
from pathlib import Path
import signal
import sys
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

import worker_checkpoint as checkpoint
import worker_runtime as runtime
import worker
from check_worker import job


class MemoryStore:
    def __init__(self):
        self.data = {}
        self.fail_at = None
        self.writes = []

    def get(self, uri):
        return self.data[uri]

    def put(self, uri, data, *, immutable=True):
        self.writes.append(uri)
        if len(self.writes) == self.fail_at:
            raise OSError('injected upload loss')
        if immutable and uri in self.data and self.data[uri] != data:
            raise ValueError('object changed')
        self.data[uri] = data


def checkpoint_job():
    value = job()
    value['config'] |= {'checkpoint_script': 'save.sh', 'checkpoint_seconds': 0, 'checkpoint_timeout': 3}
    return value


class StorageTests(unittest.TestCase):
    def test_interrupted_publication_at_each_write_keeps_previous_usable_generation(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(checkpoint, 'CHUNK_BYTES', 4):
            root = Path(temp)
            source = root / 'snapshot'; source.mkdir()
            (source / 'state').write_bytes(b'abcdabcd1234')
            store = MemoryStore(); value = checkpoint_job()
            first = checkpoint.publish(store, source, value, 'periodic')
            self.assertEqual(first['uploaded_bytes'], 8)  # repeated chunk stored once
            self.assertEqual(first['bytes'], 12)
            original = copy.deepcopy(store.data)
            (source / 'state').write_bytes(b'abcd5678')
            for fail_at in (1, 2, 3):  # new chunk, manifest, commit pointer
                failed = MemoryStore(); failed.data = copy.deepcopy(original); failed.fail_at = fail_at
                with self.assertRaises(OSError):
                    checkpoint.publish(failed, source, value, 'spot-interruption', first)
                latest = json.loads(failed.get(value['uri'] + '/checkpoints/latest.json'))
                recovered = root / f'old-{fail_at}'
                checkpoint.restore(failed, latest, recovered, value)
                self.assertEqual((recovered / 'state').read_bytes(), b'abcdabcd1234')
            second = checkpoint.publish(store, source, value, 'spot-interruption', first)
            self.assertEqual(second['uploaded_bytes'], 4)
            checkpoint.restore(store, second, root / 'new', value)
            self.assertEqual((root / 'new/state').read_bytes(), b'abcd5678')
            self.assertNotEqual(first['uri'], second['uri'])

    def test_corruption_wrong_invocation_and_unsafe_files_never_install_restore(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root / 'snapshot'; source.mkdir()
            (source / 'state').write_bytes(b'progress')
            store = MemoryStore(); value = checkpoint_job()
            reference = checkpoint.publish(store, source, value, 'periodic')
            other = copy.deepcopy(value); other['args'] = ['different model']
            with self.assertRaisesRegex(ValueError, 'mismatch'):
                checkpoint.restore(store, reference, root / 'wrong', other)
            self.assertFalse((root / 'wrong').exists())
            manifest = checkpoint.load(store, reference)
            chunk = manifest['files'][0]['chunks'][0]
            store.data[chunk['uri']] = b'corrupt!'
            with self.assertRaisesRegex(ValueError, 'checksum'):
                checkpoint.restore(store, reference, root / 'corrupt', value)
            self.assertFalse((root / 'corrupt').exists())
            manifest['files'][0]['path'] = '../escape'
            data = json.dumps(manifest).encode(); store.data[reference['uri']] = data
            bad = reference | {'sha256': checkpoint.sha(data)}
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                checkpoint.restore(store, bad, root / 'bad', value)
            self.assertFalse((root / 'escape').exists())
            (source / 'link').symlink_to(source / 'state')
            with self.assertRaisesRegex(ValueError, 'ordinary files'):
                checkpoint.publish(store, source, value, 'periodic')

    def test_producer_hook_receipt_and_restore_preserve_the_given_snapshot(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); value = checkpoint_job(); store = MemoryStore()
            hook = root / 'save.sh'
            hook.write_text('printf "%s\\n" "$SIXDB_CHECKPOINT_REASON" > "$1/reason"\n'
                            'mkdir "$1/empty"\nprintf "saved progress" > "$1/progress"\n')
            value['config']['checkpoint_script'] = str(hook)
            job_file = root / 'job.json'; job_file.write_text(json.dumps(value))
            receipt = root / 'receipt.json'
            with patch.object(sys, 'argv', ['helper', 'save', str(job_file), str(root),
                                          'spot-interruption', str(receipt)]), \
                 patch.object(checkpoint, 'Store', return_value=store):
                checkpoint.main()
            checkpoint.restore(store, json.loads(receipt.read_text()), root / 'restored', value)
            self.assertEqual((root / 'restored/progress').read_text(), 'saved progress')
            self.assertEqual((root / 'restored/reason').read_text(), 'spot-interruption\n')
            self.assertTrue((root / 'restored/empty').is_dir())

    def test_resume_uses_original_source_and_records_lineage_without_recapturing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); snapshot = root / 'snapshot'; snapshot.mkdir()
            (snapshot / 'progress').write_text('already explored')
            value = checkpoint_job()
            original = b'exact captured source bytes'
            value['source'] |= {'sha256': checkpoint.sha(original), 'setup_sha256': 'old-setup'}
            store = MemoryStore(); reference = checkpoint.publish(store, snapshot, value, 'periodic')
            out = root / 'resumed'; out.mkdir()
            def download(command, **kwargs):
                self.assertEqual(command[:3], ['aws', 's3', 'cp'])
                Path(command[5]).write_bytes(original if command[4].endswith('source.tar.gz') else b'{}')
            with patch.object(worker, 'status', return_value={'state': 'interrupted'}), \
                 patch.object(worker, 'checkpoint_status', return_value=reference), \
                 patch.object(checkpoint, 'Store', return_value=store), \
                 patch.object(worker.subprocess, 'run', side_effect=download), \
                 patch.object(worker, 'ensure_infrastructure'), \
                 patch.object(worker, 'snapshot', side_effect=AssertionError('must not capture edited checkout')):
                resumed = worker.prepare_resume(value, value['config'], out, Mock())
            self.assertEqual((out / 'source.tar.gz').read_bytes(), original)
            self.assertEqual(resumed['source']['digest'], value['source']['digest'])
            self.assertEqual(resumed['source_commit'], 'original-commit')
            self.assertEqual(resumed['args'], value['args'])
            self.assertEqual(resumed['resume'], {'job': value['id'], 'checkpoint': reference})
            self.assertEqual(resumed['id'], 'resumed')
            self.assertEqual(resumed['source']['checkpoint_sha256'],
                             checkpoint.sha((out / 'worker_checkpoint.py').read_bytes()))
            # A second interruption before any new save still has a recovery point.
            aws = Mock(); aws.get_json.return_value = None
            self.assertEqual(worker.checkpoint_status(resumed, aws), reference)


class RuntimeTests(unittest.TestCase):
    def test_notice_checkpoints_before_termination_and_keeps_interrupted_status(self):
        with tempfile.TemporaryDirectory() as temp:
            value = checkpoint_job(); runner = runtime.Worker(value, Path(temp))
            env = os.environ | {'SIXDB_CHECKPOINT_STATE': str(Path(temp) / 'state')}
            recorded = []
            def save(reason, supplied):
                os.kill(runner.process.pid, 0)  # workload must still be alive
                recorded.append(reason)
            timer = threading.Timer(0.15, runner.interrupted.set)
            timer.start()
            with patch.object(runner, 'checkpoint', side_effect=save):
                with self.assertRaises(InterruptedError):
                    runner.execute(['sleep', '30'], runner.results / 'log', env)
            timer.join()
            self.assertEqual(recorded, ['spot-interruption'])
            self.assertIsNone(runner.process)

    def test_notice_shortens_active_checkpoint_kills_hook_and_unpauses_workload(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); runner = runtime.Worker(checkpoint_job(), root)
            runner.config['checkpoint_timeout'] = 30
            def notice():
                runner.interruption_end = time.time() + 0.1
                runner.interrupted.set()
            timer = threading.Timer(0.1, notice)
            timer.start()
            runner.process = subprocess.Popen(['sleep', '30'], start_new_session=True)
            os.kill(runner.process.pid, signal.SIGSTOP)
            # Exercise real process lifetime; only replace the checkpoint program.
            real = subprocess.Popen
            def launch(argv, **kwargs):
                return real(['sleep', '30'], **kwargs)
            try:
                with patch.object(runtime.subprocess, 'Popen', side_effect=launch):
                    start = time.monotonic(); runner.checkpoint('periodic', os.environ)
                self.assertLess(time.monotonic() - start, 2)
                event = json.loads((runner.results / 'checkpoint-events.jsonl').read_text())
                self.assertEqual(event['state'], 'failed')
                status = Path(f'/proc/{runner.process.pid}/status').read_text()
                self.assertNotIn('\nState:\tT', status)
            finally:
                runtime.terminate_group(runner.process)
                timer.join()

    def test_resume_rejects_running_original_and_missing_committed_checkpoint(self):
        from unittest.mock import Mock
        value = checkpoint_job(); aws = Mock()
        with tempfile.TemporaryDirectory() as temp:
            with patch.object(worker, 'status', return_value={'state': 'running'}), \
                 patch.object(worker, 'instances', return_value=[{'State': {'Name': 'running'}}]):
                with self.assertRaisesRegex(ValueError, 'still be running'):
                    worker.prepare_resume(value, value['config'], Path(temp), aws)
            with patch.object(worker, 'status', return_value={'state': 'interrupted'}), \
                 patch.object(worker, 'checkpoint_status', return_value=None):
                with self.assertRaisesRegex(ValueError, 'no committed'):
                    worker.prepare_resume(value, value['config'], Path(temp), aws)


if __name__ == '__main__':
    unittest.main()
