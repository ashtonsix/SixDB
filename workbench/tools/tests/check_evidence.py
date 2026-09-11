#!/usr/bin/env python3
"""Check summary units, repetitions, optional counters and failure boundaries."""
import csv
import json
from pathlib import Path
import tempfile
import unittest

from evidence import summarize, verify_compact


class SummaryCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / 'samples.json'
        self.rows = [{'run_name': 'read', 'run_type': 'iteration', 'iterations': 100,
                      'threads': 1, 'time_unit': 'us', 'cpu_time': cpu, 'real_time': cpu + 1,
                      'items_per_second': rate, 'encoded_bytes': 4096, 'changing': value}
                     for cpu, rate, value in [(2, 5e8, 1), (4, 2.5e8, 2), (3, 1e9 / 3, 3)]]

    def write(self, rows=None):
        self.input.write_text(json.dumps({'context': {'machine': 'fixture'}, 'benchmarks': self.rows if rows is None else rows}))

    def test_repetitions_units_counters_and_recovery_reference(self):
        self.write(self.rows + [dict(self.rows[0], run_type='aggregate', cpu_time=999)])
        reference = self.root / 'artifact.json'
        reference.write_text('{"uri":"s3://fixture/retained"}\n')
        destination = self.root / 'summary'
        meta = summarize([('zen', self.input)], destination, counters=['encoded_bytes', 'changing'], artifact=reference)
        self.assertEqual(meta['cases'], 1)
        with (destination / 'cases.csv').open() as file:
            row = next(csv.DictReader(file))
        self.assertEqual(json.loads(row['cpu_ns']), [2000, 4000, 3000])
        self.assertEqual(float(row['median_cpu_ns']), 3000)
        self.assertEqual(json.loads(row['ns_per_item']), [2, 4, 3])
        self.assertEqual(row['counter:encoded_bytes'], '4096')
        self.assertEqual(json.loads(row['counter:changing']), [1, 2, 3])
        self.assertEqual((destination / 'artifact.json').read_bytes(), reference.read_bytes())
        self.assertEqual(verify_compact(destination), meta)
        (destination / 'cases.csv').write_text('edited')
        with self.assertRaisesRegex(ValueError, 'changed'):
            verify_compact(destination)

    def test_labels_filter_and_missing_item_rate(self):
        rows = [{k: v for k, v in row.items() if k != 'items_per_second'} for row in self.rows]
        self.write(rows + [{'name': 'broken', 'error_occurred': True, 'error_message': 'oracle'}])
        meta = summarize([('a', self.input), ('b', self.input)], self.root / 'selected', pattern='^read$')
        self.assertEqual(meta['cases'], 2)
        with (self.root / 'selected/cases.csv').open() as file:
            self.assertTrue(all(row['ns_per_item'] == '' for row in csv.DictReader(file)))
        with self.assertRaisesRegex(ValueError, 'oracle'):
            summarize([('a', self.input)], self.root / 'bad')
        self.assertFalse((self.root / 'bad').exists())

    def test_invalid_selection_and_data_do_not_install_output(self):
        for change in [{'cpu_time': float('nan')}, {'iterations': 0}, {'time_unit': 'cycles'}, {'items_per_second': 0}]:
            with self.subTest(change=change):
                self.write([dict(self.rows[0], **change)])
                with self.assertRaises(ValueError):
                    summarize([('a', self.input)], self.root / 'bad')
                self.assertFalse((self.root / 'bad').exists())
        self.write()
        for kwargs in [{'pattern': 'missing'}, {'counters': ['typo']}]:
            with self.assertRaises(ValueError):
                summarize([('a', self.input)], self.root / 'bad', **kwargs)
        with self.assertRaisesRegex(ValueError, 'distinct'):
            summarize([('a', self.input), ('a', self.input)], self.root / 'bad')
        self.assertFalse((self.root / 'bad').exists())


if __name__ == '__main__':
    unittest.main()
