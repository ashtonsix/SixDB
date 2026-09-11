#!/usr/bin/env python3
"""Check isolated Clang compiles, serial accounting and failure receipts on tiny fixtures."""

import importlib.util
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOL = Path(__file__).resolve().parents[1] / 'compile_probe.py'
spec = importlib.util.spec_from_file_location('compile_probe', TOOL)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class CompileProbeChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='sixdb compile probe ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        self.database = self.build / 'compile_commands.json'
        self.compiler = shutil.which('clang++-21')
        if not self.compiler:
            self.skipTest('requires pinned Linux clang++-21')
        self.entries = []
        for name in ('first', 'second'):
            source = self.root / (name + '.cpp')
            source.write_text('template<int N> int value() { return N * 2; }\n'
                              f'int {name}() {{ return value<7>(); }}\n')
            obj, dep = self.build / (name + '.o'), self.build / (name + '.d')
            obj.write_text('original object'); dep.write_text('original dependencies')
            argv = [self.compiler, '-std=c++23', '-O3', '-g', '-MD', '-MF', str(dep),
                    '-MT', str(obj), '-o', str(obj), '-c', str(source)]
            self.entries.append({'directory': str(self.build), 'file': str(source),
                                 'output': str(obj), 'arguments': argv})
        self.entries[0]['command'] = shlex.join(self.entries[0].pop('arguments'))
        self.database.write_text(json.dumps(self.entries))
        (self.build / '.ninja_log').write_text('# untouched\n')

    def cli(self, output, *extra, success=True):
        result = subprocess.run([sys.executable, TOOL, self.build, '--output', output, *extra],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def test_serial_compiles_preserve_outputs_and_flags(self):
        output = self.root / 'results'
        self.cli(output, '--source', 'first.cpp', '--source', 'second.cpp', '--time-trace')
        report = json.loads((output / 'summary.json').read_text())
        self.assertEqual(report['status'], 'complete')
        self.assertEqual(len(report['compiles']), 2)
        for record in report['compiles']:
            self.assertGreater(record['max_process_rss_kib'], 0)
            self.assertTrue(record['source_unchanged'])
            self.assertIn('-O3', record['argv']); self.assertIn('-g', record['argv'])
            self.assertTrue((output / record['output'] / 'trace.json').is_file())
            self.assertTrue((output / record['output'] / 'dependencies.d').is_file())
            self.assertEqual(Path(record['original_object']).read_text(), 'original object')
            self.assertEqual(Path(record['original_object']).with_suffix('.d').read_text(),
                             'original dependencies')
        self.assertEqual(report['serial_compile_wall_seconds'],
                         sum(r['wall_seconds'] for r in report['compiles']))
        self.assertEqual(report['max_process_rss_kib'],
                         max(r['max_process_rss_kib'] for r in report['compiles']))
        self.assertEqual((self.build / '.ninja_log').read_text(), '# untouched\n')
        self.cli(output, '--source', 'first.cpp', success=False)

    def test_plan_and_object_disambiguation(self):
        another = dict(self.entries[0], output=str(self.build / 'other.o'))
        self.database.write_text(json.dumps([*self.entries, another]))
        output = self.root / 'planned'
        self.cli(output, '--source', 'first.cpp', '--plan', success=False)
        result = self.cli(output, '--object', 'first.o', '--plan')
        self.assertEqual(len(json.loads(result.stdout)), 1)
        self.assertFalse(output.exists())
        self.cli(output, '--source', 'missing.cpp', success=False)

    def test_compiler_error_stops_with_evidence(self):
        Path(self.entries[0]['file']).write_text('#error deliberate fixture failure\n')
        output = self.root / 'failure'
        self.cli(output, '--source', 'first.cpp', '--source', 'second.cpp', success=False)
        report = json.loads((output / 'summary.json').read_text())
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(len(report['compiles']), 1)
        row = report['compiles'][0]
        self.assertNotEqual(row['returncode'], 0)
        self.assertIn('deliberate fixture failure', (output / row['output'] / 'stderr.txt').read_text())
        self.assertGreater(row['max_process_rss_kib'], 0)
        self.assertFalse(row['oom_evidence']['compiler_pid_confirmed'])

    def test_killed_compiler_retains_signal_and_rss(self):
        fake = self.root / 'clang-fixture'
        fake.write_text('#!' + sys.executable + '\nimport os, signal\n'
                        'buffer = bytearray(8 * 1024 * 1024)\n'
                        'os.kill(os.getpid(), signal.SIGKILL)\n')
        fake.chmod(0o755)
        entry = probe.select(self.database, ['first.cpp'], [])[0]
        entry['argv'][0] = str(fake)
        output = self.root / 'killed'
        argv = probe.probe_command(entry, output)
        with patch.object(probe, 'kernel_snapshot', return_value={'unavailable': 'fixture'}), \
                patch.object(probe, 'cgroup_snapshot', return_value={'unavailable': 'fixture'}):
            row = probe.compile_one(entry, output, argv)
        self.assertEqual(row['returncode'], -9)
        self.assertEqual(row['status'], 'failed')
        self.assertGreater(row['max_process_rss_kib'], 8192)
        self.assertFalse(row['oom_evidence']['compiler_pid_confirmed'])
        self.assertTrue((output / 'compile.json').is_file())

    def test_oom_scope_does_not_implicate_other_process(self):
        before = {'path': '/group', 'events': {'oom_kill': 1}}
        after = {'path': '/group', 'events': {'oom_kill': 2}}
        kernel_before = {'records': ['[1.0] Out of memory: Killed process 101 (clang)']}
        kernel_after = {'records': [*kernel_before['records'],
                                    '[2.0] Out of memory: Killed process 202 (clang)']}
        row = probe.oom_evidence(before, after, kernel_before, kernel_after, 101)
        self.assertEqual(row['cgroup']['delta']['oom_kill'], 1)
        self.assertFalse(row['compiler_pid_confirmed'])
        self.assertTrue(probe.oom_evidence(before, after, kernel_before, kernel_after, 202)
                        ['compiler_pid_confirmed'])

    def test_rss_is_per_compile_not_previous_child_high_water(self):
        fake = self.root / 'clang-memory-fixture'
        fake.write_text('#!' + sys.executable + '\nimport sys\nfrom pathlib import Path\n'
                        'source = sys.argv[sys.argv.index("-c") + 1]\n'
                        'buffer = bytearray((64 if "first.cpp" in source else 1) * 1024 * 1024)\n'
                        'Path(sys.argv[sys.argv.index("-o") + 1]).write_bytes(b"fixture")\n')
        fake.chmod(0o755)
        peaks = []
        for index, entry in enumerate(probe.select(self.database, ['first.cpp', 'second.cpp'], [])):
            entry['argv'][0] = str(fake)
            output = self.root / f'memory-{index}'
            with patch.object(probe, 'kernel_snapshot', return_value={'unavailable': 'fixture'}), \
                    patch.object(probe, 'cgroup_snapshot', return_value={'unavailable': 'fixture'}):
                row = probe.compile_one(entry, output, probe.probe_command(entry, output))
            peaks.append(row['max_process_rss_kib'])
        self.assertGreater(peaks[0] - peaks[1], 32 * 1024)

    def test_output_options_and_unsupported_modes(self):
        entry = probe.select(self.database, ['first.cpp'], [])[0]
        entry['argv'] += ['-MFold.d', '-MJold.json', '-ftime-trace=old-trace.json',
                          '--serialize-diagnostics=old.dia']
        output = self.root / 'redirected'
        argv = probe.probe_command(entry, output)
        self.assertIn('-MF' + str(output / 'dependencies.d'), argv)
        self.assertIn('-MJ' + str(output / 'compilation.json'), argv)
        self.assertIn('-ftime-trace=' + str(output / 'trace.json'), argv)
        self.assertIn('--serialize-diagnostics=' + str(output / 'diagnostics.dia'), argv)
        for flag in ('@flags.rsp', '-save-temps', '-fmodules'):
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                probe.probe_command(dict(entry, argv=entry['argv'] + [flag]), output)


if __name__ == '__main__':
    unittest.main()
