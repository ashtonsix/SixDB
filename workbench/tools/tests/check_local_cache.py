#!/usr/bin/env python3
"""Native quota diagnostics, fail-closed configuration and unique-file preservation."""
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

import local_cache as cache
import nativequota
import storage


class LocalCacheChecks(unittest.TestCase):
    def setUp(self):
        base = Path(__file__).resolve().parents[3] / 'build'
        base.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base, prefix='check-native-cache-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / 'config'
        self.env = patch.dict(os.environ, {'XDG_CONFIG_HOME': str(self.config)}, clear=False)
        self.env.start()
        self.addCleanup(self.env.stop)
        self.previous_override = os.environ.pop('SIXDB_STORAGE_CONFIG', None)
        self.addCleanup(self.restore_override)
        self.build = self.root / 'build'
        self.build.mkdir()
        self.unique = self.build / 'old.o'
        self.unique.write_bytes(b'\x7fELFunique compiler bytes')
        os.utime(self.unique, (1, 1))

    def restore_override(self):
        if self.previous_override is not None:
            os.environ['SIXDB_STORAGE_CONFIG'] = self.previous_override

    def maintain(self, **kwargs):
        with redirect_stdout(io.StringIO()):
            return cache.maintain(self.root, **kwargs)

    def install_native_config(self):
        try:
            filesystem, group = nativequota.identity(self.root)
            quota = Path('/sys/fs/btrfs') / filesystem / 'qgroups' / group.replace('/', '_')
            limit = int((quota / 'max_referenced').read_text())
            flags = int((quota / 'limit_flags').read_text())
        except (OSError, ValueError):
            self.skipTest('this check needs an already capped native Btrfs subvolume')
        if not flags & 1 or limit < 8192:
            self.skipTest('this check needs an existing finite native Btrfs quota')
        path = self.config / 'sixdb/storage.json'
        path.parent.mkdir(parents=True)
        value = {'format': 1, 'root': str(self.root / 'hub'), 'scope': str(self.root),
                 'filesystem_uuid': filesystem, 'qgroup': group, 'budget_bytes': limit}
        path.write_text(json.dumps(value))
        return path, value

    def test_unconfigured_is_explicitly_diagnostic_and_preserves_old_objects(self):
        result = self.maintain(prune=True)
        self.assertEqual(result['scope'], 'unconfigured')
        self.assertGreater(result['headroom']['destination']['free'], 0)
        self.assertEqual(self.unique.read_bytes(), b'\x7fELFunique compiler bytes')
        self.assertFalse((self.root / 'hub').exists())

    def test_native_report_uses_kernel_quota_and_preserves_unique_old_objects(self):
        _, value = self.install_native_config()
        result = self.maintain(prune=True)
        self.assertEqual(result['scope'], 'native-subvolume-quota')
        self.assertEqual(result['budget_bytes'], value['budget_bytes'])
        self.assertGreater(result['bytes'], 0)
        self.assertGreaterEqual(result['available_bytes'], 0)
        self.assertLessEqual(result['available_bytes'], result['budget_bytes'])
        self.assertEqual(result['removed_bytes'], 0)
        self.assertEqual(self.unique.read_bytes(), b'\x7fELFunique compiler bytes')
        self.assertFalse((self.root / 'hub').exists())

    def test_saved_budget_below_live_limit_refuses_without_touching_output(self):
        path, value = self.install_native_config()
        value['budget_bytes'] -= 4096
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'hard limit'):
            self.maintain(prune=True)
        self.assertEqual(self.unique.read_bytes(), b'\x7fELFunique compiler bytes')

    def test_explicit_missing_configuration_refuses(self):
        os.environ['SIXDB_STORAGE_CONFIG'] = str(self.root / 'missing.json')
        with self.assertRaisesRegex(ValueError, 'configuration is missing'):
            self.maintain(prune=True)
        self.assertTrue(self.unique.exists())


if __name__ == '__main__':
    unittest.main()
