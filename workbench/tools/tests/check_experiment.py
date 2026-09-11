#!/usr/bin/env python3
"""Check that captured builds tolerate live edits and retain incremental reuse."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

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
                run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja'])
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


if __name__ == '__main__':
    unittest.main()
