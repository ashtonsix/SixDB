#!/usr/bin/env python3
"""Check cached preparation, concurrent callers, retries, and pinned downloads offline."""
import json
import multiprocessing
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import datasets
import storage


class DatasetCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.patch = patch.object(datasets, 'ROOT', self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        unmanaged = patch.object(storage, 'managed_root', return_value=None)
        unmanaged.start()
        self.addCleanup(unmanaged.stop)
        self.recipe = self.root / 'recipe.py'
        self.recipe.write_text('recipe v1')

    def test_reuse_parameters_and_recipe_identity(self):
        builds = self.root / 'build-count.txt'
        def build(out):
            with builds.open('a') as count:
                count.write('built\n')
            (out / 'values').write_bytes(b'preserved bytes\r\n')
            return {'rows': 1}
        def get():
            return datasets.cached('example', self.recipe, {'source': 'pin'}, {'rows': 1}, build)
        def acquire(index):
            (self.root / f'caller-{index}').write_text(str(get()))
        processes = [multiprocessing.get_context('fork').Process(target=acquire, args=(i,)) for i in range(4)]
        for process in processes:
            process.start()
        for process in processes:
            process.join(10)
            self.assertEqual(process.exitcode, 0)
        paths = [Path((self.root / f'caller-{i}').read_text()) for i in range(4)]
        self.assertEqual(len(set(paths)), 1)
        self.assertEqual(builds.read_text().splitlines(), ['built'])
        first = datasets.verify(paths[0])
        self.assertEqual(get(), paths[0])
        other = datasets.cached('example', self.recipe, {'source': 'pin'}, {'rows': 2}, build)
        self.assertNotEqual(other, paths[0])
        self.recipe.write_text('recipe v2')
        self.assertNotEqual(get(), paths[0])
        self.assertEqual(datasets.verify(paths[0])['id'], first['id'])
        (paths[0] / 'values').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'changed'):
            datasets.verify(paths[0])

    def test_failed_preparation_is_retryable(self):
        def fail(out):
            (out / 'partial').write_text('partial output')
            raise RuntimeError('interrupted')
        with self.assertRaisesRegex(RuntimeError, 'interrupted'):
            datasets.cached('example', self.recipe, {}, {}, fail)
        self.assertEqual(list((self.root / 'build/datasets/prepared').iterdir()), [])
        result = datasets.cached('example', self.recipe, {}, {}, lambda p: (p / 'data').write_text('complete'))
        self.assertEqual(set(datasets.verify(result)['files_sha256']), {'data'})

    def test_metadata_and_unavailable_mirror(self):
        request = {'format': 2, 'name': 'example', 'recipe_sha256': datasets.digest(self.recipe),
                   'inputs': {}, 'parameters': {}}
        ref = self.root / 'reference.json'
        ref.write_text(json.dumps({'key': datasets.identity(request)}))
        builds = self.root / 'build-count.txt'
        def build(out):
            with builds.open('a') as count:
                count.write('built\n')
            (out / 'data').write_text('data')
            return {'sample_positions': [0, 8]}
        with patch.object(datasets, 'restore', side_effect=ValueError('corrupt remote bytes')):
            with self.assertRaisesRegex(ValueError, 'corrupt'):
                datasets.cached('example', self.recipe, {}, {}, build, ref)
        self.assertFalse(builds.exists())
        with patch.object(datasets, 'restore', side_effect=FileNotFoundError('aws not installed')):
            result = datasets.cached('example', self.recipe, {}, {}, build, ref)
        self.assertEqual(builds.read_text().splitlines(), ['built'])
        meta = datasets.verify(result)
        meta['details']['sample_positions'] = [1, 9]
        (result / 'prepared.json').write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            datasets.verify(result)

    def test_legacy_and_download_hashes(self):
        old = self.root / 'old'
        old.write_bytes(b'original')
        spec = {'url': old.as_uri(), 'sha256': datasets.digest(old), 'bytes': old.stat().st_size,
                'legacy_cache': 'old'}
        path = datasets.source(spec)
        self.assertEqual(path.read_bytes(), old.read_bytes())
        self.assertEqual(datasets.source(spec), path)
        path.unlink()
        old.write_bytes(b'wrong')
        with self.assertRaisesRegex(ValueError, 'pinned bytes'):
            datasets.source(spec)
        self.assertFalse(path.exists())


if __name__ == '__main__':
    unittest.main()
