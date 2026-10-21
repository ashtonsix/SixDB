#!/usr/bin/env python3
"""Check research navigation finds orphan notes and ignores retired catalog entries."""
from pathlib import Path
import subprocess
import tempfile
import unittest

import check_docs


class NavigationCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='research navigation ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_question_route_can_reach_note_through_a_study(self):
        self.write('workbench/notebook/README.md', '[Route](route.md)\n')
        self.write('workbench/notebook/route.md', '[Study](../spikes/example/)\n')
        self.write('workbench/spikes/README.md', '[Example](example/README.md)\n')
        self.write('workbench/spikes/example/README.md',
                   '[Hidden finding](../../notebook/detail.md#useful-result)\n')
        self.write('workbench/notebook/detail.md', '# Useful result\n')
        orphan = self.write('workbench/notebook/orphan.md', '# Easily missed\n')
        hints = check_docs.inspect(self.root)[0]
        self.assertEqual(hints, [
            'workbench/notebook/orphan.md: not reachable from workbench/notebook/README.md'])
        self.write('workbench/notebook/detail.md', '# Useful result\n[Related](orphan.md)\n')
        self.assertEqual(check_docs.inspect(self.root)[0], [])
        orphan.unlink()
        self.assertTrue(any('missing target orphan.md' in h for h in check_docs.inspect(self.root)[0]))

    def test_deleted_tracked_study_is_not_an_uncatalogued_live_study(self):
        self.write('workbench/spikes/README.md', '# Studies\n')
        old = self.write('workbench/spikes/retired/README.md', '# Old study\n')
        subprocess.run(['git', '-C', str(self.root), 'add', '.'], check=True)
        old.unlink()
        self.assertEqual(check_docs.inspect(self.root)[0], [])
        self.write('workbench/spikes/new/README.md', '# New study\n')
        self.assertEqual(check_docs.inspect(self.root)[0], [
            'workbench/spikes/README.md: catalog could link new/README.md'])

    def test_ignored_scratch_is_not_a_notebook_obligation(self):
        self.write('.gitignore', 'workbench/notebook/scratch/\n')
        self.write('workbench/notebook/README.md', '# Notebook\n')
        self.write('workbench/notebook/scratch/local.md', '# Private scratch\n')
        self.assertEqual(check_docs.inspect(self.root)[0], [])


if __name__ == '__main__':
    unittest.main()
