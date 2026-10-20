#!/usr/bin/env python3
"""Check historical offline reports retain units/nulls and cannot launch retired campaigns."""
import csv
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
ALLOCATION = ROOT / 'workbench/benchmarks/orbital/run.py'
PERFORMANCE = ROOT / 'workbench/spikes/orbital-mvcc/performance'


def load(path):
    spec = importlib.util.spec_from_file_location(path.parent.name + path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class HistoricalReports(unittest.TestCase):
    def test_service_units_repetitions_and_missing_quantiles(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            report = load(PERFORMANCE / 'cooperative_run.py').report
            case = dict(repetition=0, mode='delay', treatment='loaded', execution='ring', quantum=32)
            data = dict(operations=4, read_complete_ns=40, all_work_complete_ns=80,
                        caller_cpu_ns=20, process_cpu_ns=24)
            data.update(dict.fromkeys(('returned_quanta', 'poll_calls', 'messages', 'digest', 'logical_rows',
                                      'driver_record_bytes', 'message_record_bytes', 'completion_control_bytes',
                                      'message_ops_static_bytes', 'group_sample_bytes', 'delay_storage_bytes',
                                      'ring_arena_charge'), 1))
            observed = {name: dict(count=2, p50=7, p99=11, max=13)
                        for name in ('group_read_ns', 'group_complete_ns', 'completion_delay_ns')}
            observed['completion_delay_ns']['p99'] = None
            report([(case, data, observed), (case | {'repetition': 1}, data, observed)], output)
            with (output / 'measurements.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 2)
            self.assertEqual(float(rows[0]['read_ns_per_request']), 10)
            self.assertEqual(float(rows[0]['all_work_ns_per_request']), 20)
            with (output / 'summary.csv').open() as stream:
                summary, = csv.DictReader(stream)
            self.assertEqual(summary['observations'], '2')
            self.assertEqual(json.loads(summary['read_ns_per_request_observations']), [10, 10])
            self.assertEqual(summary['completion_delay_ns_p99_median'], '')
            self.assertEqual(json.loads(summary['completion_delay_ns_p99_observations']), [None, None])

    def test_failed_qualification_still_refuses_summary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'run.json').write_text(json.dumps({'runs': [{'directory': 'case'}]}))
            (root / 'case').mkdir()
            (root / 'case/result.json').write_text(json.dumps({'validated': False}))
            with self.assertRaisesRegex(ValueError, 'Unvalidated'):
                load(ALLOCATION).summarize(root)
            (root / 'trial-case-0.json').write_text(json.dumps({'passed': False}))
            with self.assertRaisesRegex(ValueError, 'qualification failed'):
                load(PERFORMANCE / 'run.py').summarize(root)

    def test_old_execution_commands_refuse_without_creating_output(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'must-not-run'
            for script in (ALLOCATION, PERFORMANCE / 'run.py', PERFORMANCE / 'broader_run.py',
                           PERFORMANCE / 'cooperative_run.py'):
                with self.subTest(script=script):
                    result = subprocess.run([sys.executable, str(script), '--output', str(output),
                                             '--cpu', '0'], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 2)
                    self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
