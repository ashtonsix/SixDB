#!/usr/bin/env python3
"""Check result adapters, retained-byte identity and offline viewer data handling (Python + Node)."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import results


def table(fields, rows, name='cases.csv'):
    return {'name': name, 'fields': fields.split(','), 'rows': rows}


class ResultsTests(unittest.TestCase):
    def test_shared_units_and_repetitions(self):
        rows = results.measurements([table('case,input,cpu_ns,median_cpu_ns,ns_per_item,median_ns_per_item', [{
            'case': 'scan/64', 'input': 'candidate', 'cpu_ns': '[120, 100, 110]',
            'median_cpu_ns': '110', 'ns_per_item': '[12, 10, 11]', 'median_ns_per_item': '11',
            'counter:logical_values': '10'}])])
        self.assertEqual(rows[0]['metrics'][0], {
            'unit': 'CPU ns / iteration', 'median': 110, 'min': 100, 'max': 120,
            'samples': [120, 100, 110]})
        self.assertEqual(rows[0]['metrics'][1]['median'], 11)
        self.assertEqual(rows[0]['counters'], {'logical_values': '10'})

    def test_google_time_units_and_aggregate_exclusion(self):
        measured = results.measurements([table('name,cpu_time,time_unit', [
            {'name': 'scan', 'cpu_time': '1', 'time_unit': 'us'},
            {'name': 'scan', 'cpu_time': '0.003', 'time_unit': 'ms'},
            {'name': 'scan_median', 'cpu_time': '99', 'time_unit': 'ns', 'run_type': 'aggregate'},
            {'name': 'unsupported', 'cpu_time': '5', 'time_unit': 'cycles'}])])
        self.assertEqual(len(measured), 1)
        self.assertEqual(measured[0]['metrics'][0]['samples'], [1000, 3000])

    def test_controls_are_recorded_not_inferred(self):
        ts = [table('case,repetition,cpu_ns', [
            {'case': name, 'repetition': str(i), 'cpu_ns': str(n)}
            for name, numbers in [('native', [10, 14, 12]), ('packed', [5, 7, 6])]
            for i, n in enumerate(numbers)]),
            table('case,median_cpu_ns,control,ratio', [
                {'case': 'packed', 'median_cpu_ns': '6', 'control': 'native', 'ratio': '0.5'},
                {'case': 'native', 'median_cpu_ns': '12', 'control': '', 'ratio': ''},
                {'case': 'unknown', 'median_cpu_ns': '6', 'control': 'unretained', 'ratio': '0.4'}], 'comparisons.csv')]
        paired = results.comparisons(ts, results.measurements(ts))
        self.assertEqual(len(paired), 2)
        self.assertEqual(paired[0]['ratio'], 0.5)
        self.assertEqual(paired[0]['baseline']['samples'], [10, 14, 12])
        self.assertIsNone(paired[1]['baseline'])

    def test_store_units_and_missing_raw_repetitions(self):
        ts = [table('case,base_ns,candidate_ns,ratio,base_samples,candidate_samples', [
            {'case': name, 'base_ns': '10', 'candidate_ns': '8', 'ratio': '.8',
             'base_samples': '[9,10,11]', 'candidate_samples': '[7,8,9]'}
            for name in ['bulk/scan', 'resident/store']]),
            table('case,unit,before_median,after_median,after_over_before', [
                {'case': 'scan', 'unit': 'ns/value', 'before_median': '3',
                 'after_median': '2', 'after_over_before': '0.6667'}])]
        paired = results.comparisons(ts, [])
        self.assertEqual([p['candidate']['unit'] for p in paired], ['ns / value', 'ns / query', 'ns/value'])
        self.assertEqual(paired[-1]['candidate']['samples'], [])

    def test_dates(self):
        self.assertLess(results.date_key('20260910'), results.date_key('2026-09-11T12:00:00Z'))
        self.assertLess(results.date_key(''), results.date_key('20260910'))
        self.assertEqual(results.friendly('seriespack-range-execution'), 'SeriesPack range execution')

    def test_study_ownership_after_module_replacement(self):
        self.assertEqual(results.study_for('workbench/benchmarks/seriespack'), 'SeriesPack')
        self.assertEqual(results.study_for('workbench/benchmarks/another'), 'Another')
        self.assertEqual(results.study_for('workbench/spikes/ikea-composition/seriespack-predecessor'),
                         'SeriesPack predecessor')
        self.assertEqual(results.study_for('workbench/spikes/ikea-composition/ikea2-campaign'),
                         results.study_for('ikea2/bench'))

    def test_context_projection(self):
        context = results.scientific_context({
            'config': {'machine': 'zen5', 'env': {'SECRET': 'private'}, 'subnets': ['private']},
            'benchmark_context': {'cpu': 3, 'host_name': 'private', 'nested': {'hostname': 'private'}},
            'source': {'digest': 'hash', 'commands': ['private'], 'source_files': ['private']},
        }, {'tune': 'zen5', 'env': 'private'})
        self.assertNotIn('private', json.dumps(context))
        self.assertEqual(context['recorded']['source']['digest'], 'hash')

    def test_current_and_historical_profile_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            evidence = root / 'workbench/benchmarks/seriespack/evidence/20260911'
            evidence.mkdir(parents=True)
            with patch.object(results, 'ROOT', root):
                for name in ['IKEA_PROFILE', 'IKEA2_PROFILE']:
                    (evidence / 'provenance.json').write_text(json.dumps({
                        'config': {'env': {name: 'avx2', 'OTHER_SETTING': 'not display evidence'}}}))
                    run = results.load_run(evidence, [])
                    self.assertEqual(run['profile'], 'avx2')
                    self.assertEqual(run['study'], 'SeriesPack')
                    self.assertNotIn('OTHER_SETTING', json.dumps(run))

    def test_generator_original_bytes_unknown_schema_and_hash_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            evidence = root / 'workbench/spikes/fixture/evidence/20260911'
            evidence.mkdir(parents=True)
            source = evidence / 'unusual.csv'
            original = '\ufeffoperation,description\r\nscan,"a, b\nquoted ""text"""\r\n'.encode()
            source.write_bytes(original)
            provenance = evidence / 'provenance.json'
            provenance.write_text(json.dumps({'files_sha256': {'unusual.csv': results.sha(original)}}))
            visible = (str(source.relative_to(root)) + '\0').encode()
            output = root / 'build/results'
            with patch.object(results, 'ROOT', root), patch.object(results.subprocess, 'check_output', return_value=visible):
                catalog = results.generate(output, [])
                self.assertEqual(catalog['errors'], [])
                run = json.loads((output / catalog['runs'][0]['data']).read_text())
                self.assertEqual(run['files'][0]['text'].encode(), original)
                self.assertEqual(run['integrity']['status'], 'verified')
                self.assertEqual(run['measurements'], [])
                self.assertEqual(run['comparisons'], [])
                self.assertEqual(run['files'][0]['rows'], 1)
                source.write_bytes(original + b'more,rows\r\n')
                changed = results.load_run(evidence, [source])
                self.assertEqual(changed['integrity']['status'], 'mismatch')
                provenance.write_text('{bad JSON')
                failed = results.generate(output, [])
                self.assertEqual(len(failed['errors']), 1)
                self.assertIn('malformed JSON', failed['errors'][0]['error'])

    def test_javascript_csv_comparison_and_offline_safety(self):
        app = results.APP / 'app.js'
        script = r'''
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {csv, compareRuns, snapshotData, escape} = require(process.argv[1]);
assert.deepEqual(csv('\ufeffa,b\r\nx,"a, b\nquoted ""text"""\r\n'),
  {fields:['a','b'],rows:[['x','a, b\nquoted "text"']]});
const row = (n, input='', unit='ns') => ({case:'scan', input, table:'cases.csv', counters:{n:10},
  metrics:[{unit,median:n,min:n,max:n,samples:[n]}]});
const run = rows => ({label:'baseline',measurements:rows});
assert.equal(compareRuns(run([row(5)]),run([row(10)]),'ns')[0].ratio,.5);
assert.equal(compareRuns(run([row(5,'A')]),run([row(10,'B')]),'ns').length,0);
assert.equal(compareRuns(run([row(5)]),run([row(10,'','cycles')]),'ns').length,0);
assert.equal(compareRuns(run([row(5)]),run([row(10),row(20)]),'ns').length,0);
assert.equal(compareRuns(run([row(5)]),run([row(0)]),'ns').length,0);
const different=row(10);different.counters.n=20;
assert.match(compareRuns(run([row(5)]),run([different]),'ns')[0].basis,/counters differ/);
const data = {notes:'</script><script>danger</script>\u2028\u2029'};
const encoded = snapshotData(data), sandbox={};
assert(!encoded.includes('<'));
vm.runInNewContext('result='+encoded,sandbox);
assert.equal(sandbox.result.notes,data.notes);
assert.equal(escape('<img>'), '&lt;img&gt;');
'''
        subprocess.run(['node', '-e', script, str(app)], check=True)


if __name__ == '__main__':
    unittest.main()
