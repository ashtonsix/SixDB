#!/usr/bin/env python3
"""Check that captured builds tolerate live edits and retain incremental reuse."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import datasets
from experiment import Run


class CapturedRunCheck(unittest.TestCase):
    def test_live_edits_and_incremental_build(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / '.gitignore').write_text('/build/\n')
            (root / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\nproject(probe LANGUAGES CXX)\nadd_executable(probe main.cpp)\n')
            (root / 'main.cpp').write_text('#include "value.h"\nint main() { return VALUE; }\n')
            (root / 'value.h').write_text('#define VALUE 0\n')
            (root / 'notes.md').write_text('before')
            workspace = root / 'build/workspace'

            def build(run):
                run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja',
                                       '-DCMAKE_CXX_COMPILER=clang++-21'])
                run.step('build', ['cmake', '--build', str(run.build_dir)])
                run.step('execute', [str(run.build_dir / 'probe')])

            a = Run(root, root / 'build/a', {}, workspace=workspace)
            cmake = (root / 'CMakeLists.txt').read_text()
            (root / 'CMakeLists.txt').write_text('temporarily unfinished live CMake edit')
            a.step('editor-configure', ['cmake', '-S', str(root), '-B', str(root / 'build/editor')], check=False)
            self.assertNotEqual(a.receipt['commands'][-1]['returncode'], 0)
            (root / 'value.h').write_text('#define VALUE 1\n')
            (root / 'notes.md').write_text('editing live documentation')
            build(a)
            self.assertIsNone(a.finish())
            self.assertEqual(a.receipt['source_mode'], 'captured')
            self.assertTrue(a.receipt['source_unchanged'])
            # Revert the live code; changed notes alone must not recompile the TU.
            (root / 'value.h').write_text('#define VALUE 0\n')
            (root / 'CMakeLists.txt').write_text(cmake)
            b = Run(root, root / 'build/b', {}, workspace=workspace)
            build(b)
            self.assertIn('no work to do', (b.output / 'build.stdout').read_text())
            self.assertIsNone(b.finish())
            # An actual captured-source change still invalidates its evidence.
            c = Run(root, root / 'build/c', {}, workspace=workspace)
            h = c.source_root / 'value.h'
            h.chmod(0o644)
            h.write_text('#define VALUE 2\n')
            self.assertIsNotNone(c.finish())
            self.assertEqual(c.receipt['status'], 'failed')

    def test_captured_preparations_share_data_and_respect_explicit_cache(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ):
            os.environ.pop('SIXDB_DATA_CACHE', None)
            root = Path(temp) / 'repo'
            root.mkdir()
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / '.gitignore').write_text('/build/\n')
            (root / 'datasets.py').write_text(Path(datasets.__file__).read_text())
            tools = Path(datasets.__file__).parent
            shutil.copy2(tools / 'storage.py', root / 'storage.py')
            shutil.copytree(tools / 'sixdb_storage', root / 'sixdb_storage',
                            ignore=shutil.ignore_patterns('__pycache__'))
            (root / 'recipe.py').write_text('stable recipe bytes\n')
            (root / 'prepare.py').write_text('''from pathlib import Path
import datasets
def prepare(output):
    count = datasets.cache_root() / 'preparations'
    count.write_text(count.read_text() + 'x' if count.exists() else 'x')
    (output / 'rows').write_text('same input bytes')
    return {'rows': 1}
print(datasets.cached('fixture', Path(__file__).with_name('recipe.py'), {}, {}, prepare))
''')
            def prepare_run(name):
                run = Run(root, root / 'build' / name, {}, workspace=root / 'build' / (name + '-workspace'))
                run.step('prepare', [sys.executable, str(run.source_root / 'prepare.py')])
                prepared = Path((run.output / 'prepare.stdout').read_text().strip())
                self.assertEqual(datasets.verify(prepared)['details'], {'rows': 1})
                self.assertFalse((run.source_root / 'build/datasets').exists())
                self.assertIsNone(run.finish())
                return prepared
            first, second = prepare_run('first'), prepare_run('second')
            self.assertEqual(first, second)
            self.assertEqual((root / 'build/datasets/preparations').read_text(), 'x')
            external = Path(temp) / 'shared cache'
            os.environ['SIXDB_DATA_CACHE'] = str(external)
            third = prepare_run('external')
            self.assertTrue(third.is_relative_to(external))
            self.assertEqual((external / 'preparations').read_text(), 'x')

    def test_child_build_budget_and_explicit_cmake_setting(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ):
            root = Path(temp)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / '.gitignore').write_text('/build/\n')
            for key in ('SIXDB_BUILD_JOBS', 'CMAKE_BUILD_PARALLEL_LEVEL'):
                os.environ.pop(key, None)
            run = Run(root, root / 'build/run', {})
            def observed(name):
                run.step(name, [sys.executable, '-c',
                               'import os; print(os.environ["CMAKE_BUILD_PARALLEL_LEVEL"])'])
                return (run.output / (name + '.stdout')).read_text().strip()
            self.assertEqual(observed('default'), '1')
            os.environ['CMAKE_BUILD_PARALLEL_LEVEL'] = '3'
            self.assertEqual(observed('cmake'), '3')
            os.environ['SIXDB_BUILD_JOBS'] = '2'
            self.assertEqual(observed('worker-budget'), '2')
            self.assertIsNone(run.finish())


if __name__ == '__main__':
    unittest.main()
