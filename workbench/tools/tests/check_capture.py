#!/usr/bin/env python3
"""Exercise live/derived capture boundaries and compatibility with worker snapshots."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import capture
import worker
from experiment import sha256, source_files


class CaptureCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'repo'
        self.root.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        for name, data in {
            '.gitignore': '/build/\ngenerated/\n',
            'src/keep.cpp': 'unchanged', 'src/change.cpp': 'committed', 'src/deleted.cpp': 'removed later',
            'include/old.h': 'old header', 'workbench/spikes/test/evidence/summary.csv': 'excluded output',
            'workbench/tools/worker_runtime.py': 'runtime', 'workbench/tools/worker_pool.py': 'pool',
            'workbench/tools/worker-setup.sh': '#!/bin/sh\n',
        }.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
        self.git('add', '.')
        self.git('-c', 'user.name=Capture fixture', '-c', 'user.email=fixture@example.test', 'commit', '-qm', 'fixture')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root, text=True)

    def output(self, name):
        return self.root / 'build/captures' / name

    def test_live_dirty_sources_and_worker_compatibility(self):
        (self.root / 'src/change.cpp').write_text('dirty working bytes')
        (self.root / 'src/deleted.cpp').unlink()
        script = self.root / 'run.sh'
        script.write_text('#!/bin/sh\nexit 0\n')
        script.chmod(0o755)
        before = self.git('status', '--porcelain')
        destination = self.output('base')
        metadata = capture.capture(self.root, destination)
        self.assertEqual(source_files(destination), source_files(self.root))
        self.assertEqual(before, self.git('status', '--porcelain'))
        self.assertFalse((destination / 'workbench/spikes/test/evidence/summary.csv').exists())
        self.assertEqual(metadata, capture.verify(destination))
        packed = self.root / 'build/packed'
        packed.mkdir()
        with patch.object(worker, 'ROOT', destination):
            actual = worker.snapshot(packed)
        self.assertEqual(actual['digest'], metadata['source_digest'])
        self.assertEqual(actual['files'], metadata['source_files_sha256'])
        with tarfile.open(packed / 'source.tar.gz') as archive:
            self.assertEqual(archive.getmember('run.sh').mode, 0o755)

    def test_small_overlay_keeps_base_and_replaces_directory_exactly(self):
        base = self.output('base')
        metadata = capture.capture(self.root, base)
        (self.root / 'src/keep.cpp').write_text('unrelated live edit')
        prototype = self.root / 'build/prototype.cpp'
        prototype.write_text('candidate')
        headers = self.root / 'build/headers'
        headers.mkdir()
        (headers / 'new.h').write_text('new header')
        candidate = self.output('candidate')
        result = capture.capture(base, candidate, inputs=self.root,
                                 replacements=['src/change.cpp=build/prototype.cpp', 'include=build/headers',
                                               'generated/extra.cpp=build/prototype.cpp'],
                                 removed=['src/deleted.cpp'], expected={'src/change.cpp': sha256(b'candidate')})
        self.assertEqual((candidate / 'src/keep.cpp').read_text(), 'unchanged')
        self.assertFalse((candidate / 'include/old.h').exists())
        self.assertEqual((candidate / 'include/new.h').read_text(), 'new header')
        self.assertIn('generated/extra.cpp', result['source_files_sha256'])
        self.assertEqual(metadata, capture.verify(base))
        capture.verify(candidate)

    def test_existing_destination_and_bad_hash_leave_sources_untouched(self):
        destination = self.output('base')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            capture.capture(self.root, destination, expected={'src/change.cpp': '0' * 64})
        self.assertFalse(destination.exists())
        self.assertFalse(capture.receipt_path(destination).exists())
        metadata = capture.capture(self.root, destination)
        with self.assertRaisesRegex(ValueError, 'new capture'):
            capture.capture(self.root, destination)
        self.assertEqual(metadata, capture.verify(destination))
        (destination / 'src/change.cpp').write_text('changed after capture')
        with self.assertRaisesRegex(ValueError, 'src/change.cpp'):
            capture.verify(destination)

    def test_previous_commit_symlink_cannot_write_outside_new_capture(self):
        outside = Path(self.temp.name) / 'outside'
        outside.write_text('preserve')
        link = self.root / 'link.cpp'
        link.symlink_to(outside)
        self.git('add', 'link.cpp')
        self.git('-c', 'user.name=Capture fixture', '-c', 'user.email=fixture@example.test', 'commit', '-qm', 'old link')
        link.unlink()
        link.write_text('now regular source')
        destination = self.output('regular')
        capture.capture(self.root, destination)
        self.assertEqual(outside.read_text(), 'preserve')
        self.assertFalse((destination / 'link.cpp').is_symlink())
        self.assertEqual((destination / 'link.cpp').read_text(), 'now regular source')
        link.chmod(0o755)
        (destination / 'link.cpp').chmod(0o755)
        with self.assertRaisesRegex(ValueError, 'link.cpp'):
            capture.verify(destination)

    def test_replacement_paths_do_not_escape_or_include_outputs(self):
        for name in ['../escape', '/absolute', '.git/config', 'build/probe.cpp',
                     'workbench/spikes/test/evidence/raw.json', 'tools/__pycache__/helper.pyc']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                capture.capture(self.root, self.output('bad'), replacements=[name + '=src/keep.cpp'])
            self.assertFalse(self.output('bad').exists())

    def test_directory_replacements_and_inherited_captures_skip_python_caches(self):
        (self.root / 'build').mkdir()
        inherited = self.root / 'old.pyc'
        inherited.write_bytes(b'previously captured cache')
        self.git('add', '--intent-to-add', '--force', 'old.pyc')
        helpers = self.root / 'build/helpers'
        (helpers / '__pycache__').mkdir(parents=True)
        (helpers / '__pycache__/helper.cpython-313.pyc').write_bytes(b'generated cache')
        (helpers / 'helper.pyc').write_bytes(b'legacy cache')
        (helpers / 'helper.py').write_text('print("source")\n')
        destination = self.output('helpers')
        result = capture.capture(self.root, destination, replacements=['helpers=build/helpers'])
        self.assertFalse(any(capture.python_cache(name) for name in result['source_files_sha256']))
        self.assertEqual((destination / 'helpers/helper.py').read_bytes(), (helpers / 'helper.py').read_bytes())
        self.assertEqual(result, capture.verify(destination))

    def test_root_mount_alias_uses_removable_worktrees(self):
        # OrbStack exposes the same Linux root through a second mount. Use the
        # real alias when invoked there, without requiring mount privileges.
        alias = next((p for p in Path.cwd().parents if p != Path(p.anchor)
                      and os.path.samefile(p, p.anchor)), None)
        if alias is None:
            self.skipTest('requires a working directory under a root filesystem alias')
        alias_tmp = alias / 'tmp'
        if alias_tmp.exists() and not os.path.samefile(alias_tmp, '/tmp'):
            self.assertEqual(capture.checkout_path(alias_tmp / 'not-created'), alias_tmp / 'not-created')
        # /tmp can be a separate mount, absent below the root alias. Put this
        # fixture beside the checkout, whose two paths are known to be shared.
        parent = Path('/') / Path.cwd().relative_to(alias) / 'build'
        parent.mkdir(exist_ok=True)
        scratch = tempfile.TemporaryDirectory(prefix='check-capture-', dir=parent)
        self.addCleanup(scratch.cleanup)
        fixture = Path(scratch.name) / 'repo'
        shutil.copytree(self.root, fixture)
        self.root = fixture
        base = self.output('base')
        alias_base = alias / base.relative_to('/')
        # An earlier worktree may already refer to Git metadata through the
        # other mount. Derived captures should not inherit that spelling.
        subprocess.run(['git', 'worktree', 'add', '--quiet', '--detach', str(alias_base)],
                       cwd=alias / self.root.relative_to('/'), check=True)
        candidate = self.output('candidate')
        alias_candidate = alias / candidate.relative_to('/')
        metadata = capture.capture(alias_base, alias_candidate)
        self.assertEqual(metadata['base'], str(base))
        self.assertEqual(metadata, capture.verify(alias_candidate))
        gitdir = Path((candidate / '.git').read_text().removeprefix('gitdir: ').strip())
        self.assertTrue(gitdir.is_relative_to(self.root / '.git/worktrees'))
        self.assertEqual((gitdir / 'gitdir').read_text().strip(), str(candidate / '.git'))
        self.git('worktree', 'remove', '--force', str(candidate))
        self.assertFalse(candidate.exists())
        before = self.git('worktree', 'list', '--porcelain')
        failed = self.output('failed')
        with patch.object(capture, 'source_files', side_effect=[source_files(base), ValueError('injected failure')]):
            with self.assertRaisesRegex(ValueError, 'injected failure'):
                capture.capture(alias_base, alias / failed.relative_to('/'))
        self.assertFalse(failed.exists())
        self.assertFalse(capture.receipt_path(failed).exists())
        self.assertEqual(before, self.git('worktree', 'list', '--porcelain'))


if __name__ == '__main__':
    unittest.main()
