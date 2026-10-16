#!/usr/bin/env python3
"""Whole-build accounting and conservative CMake object expiry; no cloud access."""
import io
import os
from pathlib import Path
import tempfile
import subprocess
import time
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

import local_cache as cache
import storage


class LocalCacheChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build/old-cmake'
        self.object = self.build / 'CMakeFiles/check.dir/check.cpp.o'
        self.object.parent.mkdir(parents=True)
        (self.build / 'CMakeCache.txt').write_text('fixture')
        self.object.write_bytes(b'\x7fELF' + b'compiled bytes' * 1000)
        self.env = patch.dict(os.environ, {'XDG_CACHE_HOME': str(self.root / 'cache'),
            'SIXDB_LOCAL_BUILD_GIB': '0', 'SIXDB_LOCAL_BUILD_DAYS': '7'})
        self.env.start()
        self.addCleanup(self.env.stop)
        self.age()

    def age(self):
        old = time.time() - 9 * 86400
        for path in sorted(self.build.rglob('*'), key=lambda p: -len(p.parts)) + [self.build]:
            os.utime(path, (old, old), follow_symlinks=False)

    def maintain(self):
        with patch.object(cache, 'compiler_running', return_value=False), \
                patch.object(storage, 'headroom', return_value={'destination': {'free': 100 * storage.GIB}}), \
                redirect_stdout(io.StringIO()):
            return cache.maintain(self.root)

    def test_safe_object_expires_but_measurements_and_executable_stay_counted(self):
        notes = self.build / 'results.csv'
        notes.write_bytes(b'data\n' * 5000)
        executable = self.build / 'check'
        executable.write_bytes(b'\x7fELF' + b'executable' * 1000)
        self.age()
        before = cache.inventory(self.root)['bytes']
        allocated = self.object.stat().st_blocks * 512
        result = self.maintain()
        self.assertFalse(self.object.exists())
        self.assertEqual(notes.read_bytes(), b'data\n' * 5000)
        self.assertTrue(executable.exists())
        self.assertEqual(result['bytes'], before - allocated)
        self.assertTrue(result['over_budget'])  # Protected/unique bytes never silently disappear.

    def test_recent_activity_anywhere_in_build_keeps_old_objects(self):
        (self.build / 'fresh-result.csv').write_text('still working')
        self.maintain()
        self.assertTrue(self.object.exists())

    def test_ancestor_pin_keeps_objects_without_hiding_bytes(self):
        (self.root / 'build/.keep-local').touch()
        result = self.maintain()
        self.assertTrue(self.object.exists())
        self.assertGreaterEqual(result['bytes'], self.object.stat().st_blocks * 512)

    def test_compiler_or_controller_defers_unlinking(self):
        with patch.object(cache, 'compiler_running', return_value=True), redirect_stdout(io.StringIO()):
            result = cache.maintain(self.root)
        self.assertTrue(self.object.exists())
        self.assertTrue(result['compiler_running'])

    def test_same_size_edit_after_inventory_is_not_removed(self):
        candidate, = cache.inventory(self.root)['candidates']
        data = self.object.read_bytes()
        self.object.write_bytes(data[:-1] + b'!')
        self.assertEqual(cache.reclaim(candidate), 0)
        self.assertTrue(self.object.exists())

    def test_new_pin_after_inventory_is_not_removed(self):
        candidate, = cache.inventory(self.root)['candidates']
        (self.build / '.keep-local').touch()
        self.assertEqual(cache.reclaim(candidate), 0)
        self.assertTrue(self.object.exists())

    def test_reconfiguration_after_inventory_is_not_removed(self):
        candidate, = cache.inventory(self.root)['candidates']
        (self.build / 'CMakeCache.txt').write_text('changed configuration')
        self.assertEqual(cache.reclaim(candidate), 0)
        self.assertTrue(self.object.exists())

    def test_tracked_object_is_never_an_automatic_candidate(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), 'add', str(self.object)], check=True)
        self.maintain()
        self.assertTrue(self.object.exists())

    def test_suffix_and_directory_names_do_not_establish_compiler_output(self):
        self.object.write_text('authored notes, despite the suffix')
        self.age()
        self.maintain()
        self.assertTrue(self.object.exists())

    def test_all_build_bytes_count_except_actual_worker_roots(self):
        baseline = cache.inventory(self.root)['bytes']
        workers = self.root / 'build/workers/job/results'
        workers.mkdir(parents=True)
        (workers / 'compiler.o').write_bytes(b'worker' * 1000)
        unknown = self.root / 'build/prototype/workers/unique.bin'
        unknown.parent.mkdir(parents=True)
        unknown.write_bytes(b'unique' * 1000)
        added = sum(p.stat().st_blocks * 512 for p in [unknown, unknown.parent, unknown.parent.parent])
        self.assertEqual(cache.inventory(self.root)['bytes'], baseline + added)

    def test_links_and_embedded_git_are_preserved_and_hardlinks_count_once(self):
        baseline = cache.inventory(self.root)['bytes']
        link = self.build / 'another.o'
        os.link(self.object, link)
        outside = self.root / 'outside'
        outside.write_bytes(b'outside' * 1000)
        (self.build / 'symlink.o').symlink_to(outside)
        self.age()
        self.assertEqual(cache.inventory(self.root)['bytes'], baseline)
        self.maintain()
        self.assertTrue(self.object.exists())
        link.unlink()
        (self.build / '.git').mkdir()
        self.age()
        self.maintain()
        self.assertTrue(self.object.exists())

    def test_conventional_nested_worker_exclusion_does_not_hide_other_workspaces(self):
        baseline = cache.inventory(self.root)['bytes']
        workspace = self.root / 'build/workspaces/one'
        worker = workspace / 'build/workers/job/results'
        worker.mkdir(parents=True)
        (worker / 'data').write_bytes(b'worker result' * 1000)
        unknown = workspace / 'not-build/workers/unique'
        unknown.parent.mkdir(parents=True)
        unknown.write_bytes(b'local data' * 1000)
        added = [workspace.parent, workspace, workspace / 'build',
                 workspace / 'not-build', unknown.parent, unknown]
        expected = baseline + sum(p.stat().st_blocks * 512 for p in added)
        self.assertEqual(cache.inventory(self.root)['bytes'], expected)

    def test_sparse_files_use_allocated_storage_and_other_worktrees_count(self):
        other = self.root / 'other-checkout'
        large = other / 'build/unknown/data'
        large.parent.mkdir(parents=True)
        with large.open('wb') as stream:
            stream.truncate(1024 ** 3)
        expected = cache.inventory(self.root)['bytes'] + sum(p.stat().st_blocks * 512
            for p in [large, large.parent, large.parent.parent])
        with patch.object(cache.worker_cache, 'checkouts', return_value={self.root, other, self.root / 'gone/worktree'}):
            self.assertEqual(cache.inventory(self.root)['bytes'], expected)

    def test_alias_order_does_not_scan_nested_worktree_builds_twice(self):
        nested = self.root / 'build/workspaces/nested'
        (nested / 'build').mkdir(parents=True)
        alias = self.root / 'longer/alias/path/to/checkout'
        alias.parent.mkdir(parents=True)
        alias.symlink_to(self.root, target_is_directory=True)
        with patch.object(cache.worker_cache, 'checkouts', return_value={alias, nested}):
            self.assertEqual(cache.build_roots(self.root), [self.root / 'build'])

    def test_other_filesystem_objects_cannot_be_automatic_candidates(self):
        real_stat = Path.stat
        def different_device(path, *args, **kwargs):
            info = real_stat(path, *args, **kwargs)
            if path == self.object:
                fields = list(info)
                fields[2] += 1  # st_dev: simulate a nested filesystem boundary.
                return os.stat_result(fields)
            return info
        with patch.object(Path, 'stat', different_device):
            self.assertEqual(cache.inventory(self.root)['candidates'], [])
        self.assertTrue(self.object.exists())


if __name__ == '__main__':
    unittest.main()
