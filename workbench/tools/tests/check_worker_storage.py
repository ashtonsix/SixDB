#!/usr/bin/env python3
"""Collection corruption, disk-pressure failures and verified cache eviction; no cloud allocation."""
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

import artifacts
import storage
import worker
import worker_cache as cache


class StorageChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        for i in range(75):
            (self.source / f'case-{i}.csv').write_text(f'value\n{i}\n')
        (self.source / 'empty.log').touch()  # Empty files can be legitimate.
        self.bundle = self.root / 'bundle.tar.gz'
        count = artifacts.pack(self.source, self.bundle, validate_run=False)
        self.reference = {'format': 1, 'kind': 'files', 'bucket': 'fixture', 'region': 'test',
                          'key': 'bundle', 'bytes': self.bundle.stat().st_size,
                          'sha256': artifacts.sha256(self.bundle), 'files': count}
        self.job = self.root / 'build/workers/20000101T000000Z-old'
        self.job.mkdir(parents=True)
        self.env = patch.dict(os.environ, {'SIXDB_MIN_FREE_GIB': '0', 'XDG_CACHE_HOME': str(self.root / 'cache')})
        self.env.start()
        self.addCleanup(self.env.stop)
        self.aws = patch.object(artifacts, 'aws', side_effect=self.download)
        self.aws.start()
        self.addCleanup(self.aws.stop)

    def download(self, *args, **kwargs):
        shutil.copyfile(self.bundle, args[-1])
        return subprocess.CompletedProcess(args, 0, '{}', '')

    def repack(self):
        self.reference['files'] = artifacts.pack(self.source, self.bundle, validate_run=False)
        self.reference['bytes'] = self.bundle.stat().st_size
        self.reference['sha256'] = artifacts.sha256(self.bundle)

    def stream(self):
        original = subprocess.Popen
        return patch.object(artifacts.subprocess, 'Popen', side_effect=lambda command, **kwargs:
            original([sys.executable, '-c', 'import sys; sys.stdout.buffer.write(open(sys.argv[1], "rb").read())',
                      str(self.bundle)], **kwargs))

    def test_fetch_repairs_75_zero_files_and_preserves_local_additions(self):
        destination = self.job / 'results'
        destination.mkdir()
        for i in range(75):
            (destination / f'case-{i}.csv').touch()
        (destination / 'my-notes.txt').write_text('keep me')
        (self.job / 'artifact.json').touch()
        (self.job / 'status.json').touch()
        state = {'state': 'complete', 'artifact': self.reference}
        with patch.object(cache, 'preflight'), patch.object(cache, 'maintain'):
            worker.fetch({'id': self.job.name}, self.job, state, Mock())
        self.assertTrue(cache.verified(self.job, self.reference, full=True))
        self.assertEqual((destination / 'case-74.csv').read_text(), 'value\n74\n')
        conflicts = list(self.job.glob('replaced-results-*'))
        self.assertEqual((conflicts[0] / 'my-notes.txt').read_text(), 'keep me')
        self.assertEqual(len(list(conflicts[0].glob('case-*.csv'))), 75)
        with patch.object(artifacts, 'restore_bundle') as restore:
            cache.collect(self.job, self.reference)
        restore.assert_not_called()
        (destination / 'case-74.csv').write_text('value\n99\n')  # Same length.
        self.assertFalse(cache.verified(self.job, self.reference))
        cache.collect(self.job, self.reference)
        self.assertTrue(cache.verified(self.job, self.reference))

    def test_corrupt_receipt_does_not_make_existing_results_trusted(self):
        cache.collect(self.job, self.reference)
        (self.job / 'collection.json').write_text('')
        self.assertFalse(cache.verified(self.job, self.reference))
        cache.collect(self.job, self.reference)
        self.assertTrue(cache.verified(self.job, self.reference))

    def test_sync_failure_cannot_publish_results(self):
        with patch.object(storage, 'sync_tree', side_effect=OSError(28, 'injected disk full')):
            with self.assertRaises(OSError):
                cache.collect(self.job, self.reference)
        self.assertFalse((self.job / 'results').exists())
        self.assertFalse((self.job / 'collection.json').exists())

    def test_receipt_failure_is_not_success_and_retry_recovers(self):
        original = storage.write_json
        def fail(path, value):
            if path.name == 'collection.json':
                raise OSError(28, 'injected disk full')
            original(path, value)
        output = io.StringIO()
        with patch.object(cache, 'preflight'), patch.object(cache, 'maintain'), \
                patch.object(storage, 'write_json', side_effect=fail), redirect_stdout(output):
            with self.assertRaises(OSError):
                worker.fetch({'id': self.job.name}, self.job, {'artifact': self.reference}, Mock())
        self.assertNotIn('Results:', output.getvalue())
        self.assertFalse(cache.verified(self.job, self.reference))
        cache.collect(self.job, self.reference)
        self.assertTrue(cache.verified(self.job, self.reference))

    def test_bad_archive_preserves_existing_output(self):
        (self.job / 'results').mkdir()
        (self.job / 'results/notes').write_text('keep')
        self.bundle.write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            cache.collect(self.job, self.reference)
        self.assertEqual((self.job / 'results/notes').read_text(), 'keep')

    def test_low_mac_space_stops_before_download_even_with_guest_headroom(self):
        with patch.object(storage, 'headroom', return_value={
                'destination': {'free': 100 * storage.GIB}, 'Mac host': {'free': 2 * storage.GIB}}), \
                patch.dict(os.environ, {'SIXDB_MIN_FREE_GIB': '5'}), patch.object(artifacts, 'download') as download:
            with self.assertRaisesRegex(OSError, 'Mac host'):
                cache.collect(self.job, self.reference)
        download.assert_not_called()

    def test_expansion_headroom_and_actual_write_error_publish_nothing(self):
        (self.source / 'large.csv').write_bytes(b'a' * 2 ** 20)
        self.repack()
        with patch.object(storage, 'headroom', return_value={'destination': {'free': 8192}}):
            with self.assertRaises(OSError):
                cache.collect(self.job, self.reference)
        self.assertFalse((self.job / 'results').exists())
        original = Path.open
        def failed_open(path, mode='r', *args, **kwargs):
            if mode == 'xb':
                raise OSError(28, 'injected output creation failure')
            return original(path, mode, *args, **kwargs)
        with patch.object(Path, 'open', failed_open):
            with self.assertRaises(OSError):
                cache.collect(self.job, self.reference)
        self.assertFalse((self.job / 'results').exists())

    def test_metadata_sync_failure_preserves_previous_value(self):
        path = self.root / 'status.json'
        storage.write_json(path, {'old': True})
        with patch.object(storage.os, 'fsync', side_effect=OSError(5, 'injected I/O error')):
            with self.assertRaises(OSError):
                storage.write_json(path, {'new': True})
        self.assertEqual(json.loads(path.read_text()), {'old': True})

    def test_reclaim_verifies_remote_and_preserves_data_edits_and_pins(self):
        for name, prefix in [('probe', b'\x7fELF'), ('changed', b'\x7fELF'), ('full.asm', b'assembly\n'),
                             ('assembly.txt', b'probe: file format elf64-x86-64\nDisassembly of section .text:\n'),
                             ('large.csv', b'measurements\n'), ('script.log', b'log\n')]:
            (self.source / name).write_bytes(prefix + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference, full=True)
        (self.job / 'results/changed').write_bytes(b'\x7fELF' + b'b' * 2 ** 20)
        with self.stream():
            manifest = artifacts.remote_manifest(self.reference)
            self.assertEqual(manifest, cache.read(self.job / 'collection.json')['members'])
            (self.job / '.keep-local').touch()
            self.assertEqual(cache.reclaim(self.job, self.reference), 0)
            (self.job / '.keep-local').unlink()
            removed = cache.reclaim(self.job, self.reference)
        self.assertEqual(removed, sum((self.source / name).stat().st_size for name in ['probe', 'full.asm', 'assembly.txt']))
        self.assertFalse((self.job / 'results/probe').exists())
        for name in ['changed', 'large.csv', 'script.log']:
            self.assertTrue((self.job / 'results' / name).exists())
        self.assertFalse(cache.verified(self.job, self.reference))  # The local edit is still a difference.
        shutil.copyfile(self.source / 'changed', self.job / 'results/changed')
        self.assertTrue(cache.verified(self.job, self.reference))
        self.assertFalse(cache.verified(self.job, self.reference, full=True))
        cache.collect(self.job, self.reference, full=True)
        self.assertTrue(cache.verified(self.job, self.reference, full=True))

    def test_corrupt_remote_stream_cannot_remove_local_file(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference, full=True)
        reference = self.reference | {'sha256': '0' * 64}
        with self.stream(), self.assertRaisesRegex(ValueError, 'SHA-256'):
            cache.reclaim(self.job, reference)
        self.assertTrue((self.job / 'results/probe').exists())

    def test_reclamation_receipt_failure_leaves_everything(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference, full=True)
        with self.stream(), patch.object(storage, 'write_json', side_effect=OSError(28, 'full')):
            with self.assertRaises(OSError):
                cache.reclaim(self.job, self.reference)
        self.assertTrue(cache.verified(self.job, self.reference, full=True))

    def test_inventory_protects_recent_access_and_unarchived_results(self):
        storage.write_json(self.job / 'artifact.json', self.reference)
        storage.write_json(self.job / 'status.json', {'state': 'complete'})
        with patch.object(cache, 'roots', return_value=[self.job.parent]):
            self.assertTrue(cache.inventory(self.root)[0]['eligible'])
            storage.write_json(self.job / 'collection.json', {'accessed_at': time.time()})
            self.assertFalse(cache.inventory(self.root)[0]['eligible'])

            (self.job / 'collection.json').unlink()
            (self.job / '.keep-local').touch()
            self.assertFalse(cache.inventory(self.root)[0]['eligible'])
            (self.job / '.keep-local').unlink()
            (self.job / 'artifact.json').write_text('')
            self.assertFalse(cache.inventory(self.root)[0]['eligible'])

    def test_recent_fetch_protects_against_stale_inventory(self):
        cache.collect(self.job, self.reference)
        with patch.object(artifacts, 'remote_manifest') as remote:
            self.assertEqual(cache.reclaim(self.job, self.reference, older_hours=24), 0)
        remote.assert_not_called()

    def test_symlinked_collection_is_never_reclaimed(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        (self.job / 'results').symlink_to(self.source, target_is_directory=True)
        with patch.object(artifacts, 'remote_manifest') as remote:
            self.assertEqual(cache.reclaim(self.job, self.reference), 0)
        remote.assert_not_called()
        self.assertTrue((self.source / 'probe').exists())

    def test_zero_length_job_record_recovers_from_s3(self):
        (self.job / 'job.json').touch()
        remote = {'id': self.job.name, 'config': {'region': 'test'}}
        with patch.object(worker, 'Aws') as aws:
            aws.return_value.get_json.return_value = remote
            directory, job = worker.locate(str(self.job), {'region': 'test', 'bucket': 'fixture'})
        self.assertEqual(directory, self.job)
        self.assertEqual(job, remote)
        self.assertEqual(cache.read(self.job / 'job.json'), remote)

    def test_two_collectors_share_one_restore(self):
        script = '''
import pathlib,sys,json,subprocess,shutil
sys.path.insert(0,sys.argv[1])
import artifacts,worker_cache
root=pathlib.Path(sys.argv[2])
def download(*args, **kwargs):
    with (root/'downloads').open('a') as handle: handle.write('download\\n')
    shutil.copyfile(root/'bundle.tar.gz',args[-1])
    return subprocess.CompletedProcess(args,0,'{}','')
artifacts.aws=download
worker_cache.collect(pathlib.Path(sys.argv[3]),json.loads(sys.argv[4]))
'''
        args = [sys.executable, '-c', script, str(Path(worker.__file__).parent), str(self.root),
                str(self.job), json.dumps(self.reference)]
        processes = [subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(2)]
        for process in processes:
            output, error = process.communicate(timeout=30)
            self.assertEqual(process.returncode, 0, (output, error))
        self.assertEqual((self.root / 'downloads').read_text(), 'download\n')
        self.assertTrue(cache.verified(self.job, self.reference))

    def test_fresh_collection_never_expands_compiler_output(self):
        for name, prefix in [('probe', b'\x7fELF'), ('other.a', b'!<arch>\n'), ('kernel.asm', b'assembly\n')]:
            (self.source / name).write_bytes(prefix + b'a' * 2 ** 20)
        self.repack()
        created = []
        original = Path.open
        def record(path, mode='r', *args, **kwargs):
            if mode == 'xb':
                created.append(path.name)
            return original(path, mode, *args, **kwargs)
        with patch.object(Path, 'open', record):
            cache.collect(self.job, self.reference)
        self.assertFalse(set(created) & {'probe', 'other.a', 'kernel.asm'})
        self.assertTrue(cache.verified(self.job, self.reference))
        self.assertFalse(cache.verified(self.job, self.reference, full=True))
        with self.stream():
            self.assertEqual(artifacts.remote_manifest(self.reference), cache.read(self.job / 'collection.json')['members'])
        with patch.object(artifacts, 'download') as download:
            cache.collect(self.job, self.reference)
        download.assert_not_called()

    def test_selected_and_full_fetch_preserve_existing_notes_and_inputs(self):
        for name in ['probe', 'second']:
            (self.source / name).write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference)
        notes = self.job / 'results/notes'
        notes.write_text('local analysis')
        inputs = self.job / 'results/inputs'
        inputs.mkdir()
        (inputs / 'data').write_text('prepared input')
        original_inode = (inputs / 'data').stat().st_ino
        cache.collect(self.job, self.reference, selected=['probe'])
        self.assertTrue((self.job / 'results/probe').exists())
        self.assertFalse((self.job / 'results/second').exists())
        self.assertEqual(notes.read_text(), 'local analysis')
        cache.collect(self.job, self.reference, full=True)
        self.assertTrue(cache.verified(self.job, self.reference, full=True))
        self.assertEqual((inputs / 'data').stat().st_ino, original_inode)
        self.assertFalse(list(self.job.glob('replaced-results-*')))
        with self.assertRaisesRegex(ValueError, 'selected files missing'):
            cache.collect(self.job, self.reference, selected=['absent'])

    def test_selected_first_then_wait_collects_measurements(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference, selected=['probe'])
        self.assertFalse((self.job / 'results/case-0.csv').exists())
        self.assertFalse(cache.verified(self.job, self.reference))
        cache.collect(self.job, self.reference)
        self.assertTrue(cache.verified(self.job, self.reference))
        self.assertTrue((self.job / 'results/probe').exists())

    def test_compiler_expansion_can_exceed_headroom_without_blocking_measurements(self):
        (self.source / 'huge.asm').write_bytes(b'asm\n' * 2 ** 20)
        self.repack()
        with patch.object(storage, 'headroom', return_value={'destination': {'free': 100_000}}):
            cache.collect(self.job, self.reference)
            self.assertTrue(cache.verified(self.job, self.reference))
            with self.assertRaises(OSError):
                cache.collect(self.job, self.reference, full=True)
        self.assertTrue(cache.verified(self.job, self.reference))

    def test_failed_selected_publication_is_retryable(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference)
        with patch.object(storage, 'write_json', side_effect=OSError(28, 'receipt write failed')):
            with self.assertRaises(OSError):
                cache.collect(self.job, self.reference, selected=['probe'])
        self.assertTrue(cache.verified(self.job, self.reference))
        cache.collect(self.job, self.reference, selected=['probe'])
        self.assertTrue(cache.verified(self.job, self.reference, full=True))

    def test_file_added_during_download_is_preserved(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference)
        restore = artifacts.restore_bundle
        def concurrent_write(*args, **kwargs):
            members = restore(*args, **kwargs)
            (self.job / 'results/probe').write_text('concurrent local edit')
            return members
        with patch.object(artifacts, 'restore_bundle', side_effect=concurrent_write):
            with self.assertRaisesRegex(ValueError, 'appeared during fetch'):
                cache.collect(self.job, self.reference, selected=['probe'])
        self.assertEqual((self.job / 'results/probe').read_text(), 'concurrent local edit')

    def test_captured_source_uses_current_controller_and_exact_study_bytes(self):
        capture = self.root / 'capture'
        capture.mkdir()
        subprocess.run(['git', 'init', '-q', str(capture)], check=True)
        contents = {'probe.sh': b'echo captured\n', 'workbench/tools/worker_runtime.py': b'old runtime',
                    'workbench/tools/worker_pool.py': b'old pool', 'workbench/tools/worker-setup.sh': b'old setup'}
        for name, data in contents.items():
            path = capture / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        snapshot = worker.snapshot(self.job, capture)
        self.assertEqual((self.job / 'runtime.py').read_bytes(), (worker.HERE / 'worker_runtime.py').read_bytes())
        self.assertEqual((self.job / 'worker_pool.py').read_bytes(), (worker.HERE / 'worker_pool.py').read_bytes())
        self.assertEqual(snapshot['setup_sha256'], artifacts.sha256(capture / 'workbench/tools/worker-setup.sh'))
        import tarfile
        with tarfile.open(self.job / 'source.tar.gz') as archive:
            for name, data in contents.items():
                self.assertEqual(archive.extractfile(name).read(), data)

    def test_cli_lists_omitted_files_without_expansion(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.repack()
        cache.collect(self.job, self.reference)
        output = io.StringIO()
        with patch.object(sys, 'argv', ['worker.py', 'fetch', 'fixture', '--list']), \
                patch.object(worker, 'locate', return_value=(self.job, {'config': {'region': 'fixture'}})), \
                patch.object(worker, 'status', return_value={'artifact': self.reference}), \
                patch.object(artifacts, 'remote_manifest') as remote, redirect_stdout(output):
            self.assertEqual(worker.main(), 0)
        self.assertIn('S3      probe', output.getvalue())
        self.assertIn('present case-0.csv', output.getvalue())
        remote.assert_not_called()
        self.assertFalse((self.job / 'results/probe').exists())


if __name__ == '__main__':
    unittest.main()
