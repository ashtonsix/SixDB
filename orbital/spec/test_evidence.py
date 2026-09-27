#!/usr/bin/env python3
"""Receipt controls: stale/corrupt/misclassified evidence cannot establish a case."""
import csv
import json
from pathlib import Path
import tempfile
import unittest
import evidence
import recover


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = self.root / 'spec'
        self.spec.mkdir()
        (self.spec / 'configs').mkdir()
        self.files = {'Tiny.tla': 'EXTENDS Helper\n', 'Helper.tla': 'answer == 17\n',
                      'Unused.tla': 'ignored == TRUE\n', 'configs/Tiny.cfg': 'SPECIFICATION Spec\n'}
        for name, data in self.files.items():
            (self.spec / name).write_text(data)
        (self.spec / 'check.py').write_bytes(Path(evidence.check.__file__).read_bytes())
        self.current = {name: evidence.sha((self.spec / name).read_bytes()) for name in [*self.files, 'check.py']}
        self.case = {'name': 'Tiny', 'module': 'Tiny', 'config': 'configs/Tiny.cfg', 'tier': 'quick'}
        self.runs = self.root / 'runs'
        self.runs.mkdir()

    def receipt(self, name='run', *, status='complete', expected=None, witness=None, violated='Bad', stamp='1'):
        run = self.runs / name
        (run / 'sources/configs').mkdir(parents=True)
        for relative, data in self.files.items():
            (run / 'sources' / relative).write_text(data)
        (run / 'check.py').write_bytes((self.spec / 'check.py').read_bytes())
        tool = json.dumps({'sha256': evidence.check.JAR_SHA256}).encode()
        (run / 'tool-source.json').write_bytes(tool)
        log = ''.join(f'Parsing file /old/sources/{n}\n' for n in ('Tiny.tla', 'Helper.tla'))
        log += 'Semantic processing of module Helper\nSemantic processing of module Tiny\n'
        def message(code, text, level=0):
            return f'@!@!@STARTMSG {code}:{level} @!@!@\n{text}\n@!@!@ENDMSG {code} @!@!@\n'
        failure = status in {'unexpected_violation', 'expected_violation', 'witnessed'}
        log += message(2110, f'Invariant {violated} is violated.', 1) if failure else message(2193, 'Model checking completed.')
        log += '2 states generated, 2 distinct states found, 0 states left on queue.\n'
        log += message(2186, 'Finished')
        (run / 'tlc.log').write_text(log)
        data = dict(module='Tiny.tla', configuration='configs/Tiny.cfg', case='Tiny',
                    expectation=expected or 'complete', witness=witness,
                    purpose='reachability' if witness else 'negative_control' if expected else 'check',
                    runner_sha256=self.current['check.py'],
                    jar={'sha256': evidence.check.JAR_SHA256, 'source_metadata_sha256': evidence.sha(tool)},
                    source_sha256=self.current, cwd='/old/sources', status=status,
                    states={'generated': 2, 'distinct': 2, 'queue': 0}, returncode=12 if failure else 0,
                    workers=1, started_utc=stamp, elapsed_seconds=1)
        evidence.write_json(run / 'result.json', data)
        return run / 'result.json'

    def inspect(self, path, case=None):
        return evidence.inspect_receipt(path, case or self.case, self.current, evidence.check.JAR_SHA256)

    def temporal_case(self, properties='Completes'):
        self.files['configs/Tiny.cfg'] = f'SPECIFICATION Spec\nPROPERTY {properties}\n'
        (self.spec / 'configs/Tiny.cfg').write_text(self.files['configs/Tiny.cfg'])
        self.current['configs/Tiny.cfg'] = evidence.sha(self.files['configs/Tiny.cfg'].encode())
        return dict(self.case, expect='temporal:Completes')

    def temporal_receipt(self, name, *, stamp='1', oom=True, violation='Completes', extra=None, heap='512m'):
        path = self.receipt(name, status='unexpected_violation' if oom else 'expected_violation',
                            expected='temporal:Completes', stamp=stamp)
        log = path.parent / 'tlc.log'
        prefix = log.read_text().split('@!@!@STARTMSG', 1)[0]
        text = (f'Temporal property {violation} was violated.' if violation
                else 'Temporal properties were violated.')
        messages = [(2116, text), (2264, 'The following behavior constitutes a counter-example:')]
        if extra:
            messages.append(extra)
        if oom:
            messages.append((1003, 'Java ran out of memory during liveness checking.'))
        else:
            messages.append((2186, 'Finished'))
        log.write_text(prefix + '2 states generated, 2 distinct states found, 0 states left on queue.\n' +
                       ''.join(f'@!@!@STARTMSG {code}:{0 if code == 2186 else 1} @!@!@\n'
                               f'{body}\n@!@!@ENDMSG {code} @!@!@\n' for code, body in messages))
        data = json.loads(path.read_text())
        data['returncode'] = 1 if oom else 13
        data['heap'] = heap
        evidence.write_json(path, data)
        return path

    def test_actual_import_closure_not_unrelated_siblings(self):
        path = self.receipt()
        self.current['Unused.tla'] = 'changed'
        self.assertEqual(set(self.inspect(path)['dependency_sha256']), {'Tiny.tla', 'Helper.tla', 'configs/Tiny.cfg'})
        del self.current['Helper.tla']
        with self.assertRaisesRegex(ValueError, 'dependency differs'):
            self.inspect(path)

    def test_changed_config_corrupt_capture_and_runner_rejected(self):
        path = self.receipt()
        (path.parent / 'sources/Helper.tla').write_text('corrupt')
        with self.assertRaisesRegex(ValueError, 'corrupt'):
            self.inspect(path)
        (path.parent / 'sources/Helper.tla').write_text(self.files['Helper.tla'])
        self.current['configs/Tiny.cfg'] = 'edited'
        with self.assertRaisesRegex(ValueError, 'dependency differs'):
            self.inspect(path)
        self.current['configs/Tiny.cfg'] = evidence.sha(self.files['configs/Tiny.cfg'].encode())
        (path.parent / 'check.py').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'runner differs'):
            self.inspect(path)

    def test_missing_import_line_cannot_hide_changed_dependency(self):
        path = self.receipt()
        log = path.parent / 'tlc.log'
        log.write_text(log.read_text().replace('Parsing file /old/sources/Helper.tla\n', ''))
        self.current['Helper.tla'] = 'changed'
        with self.assertRaisesRegex(ValueError, 'module lists disagree'):
            self.inspect(path)

    def test_false_green_and_nonempty_queue_rejected(self):
        for status in ('complete', 'incomplete_timeout'):
            path = self.receipt(status, status=status)
            if status == 'complete':
                log = path.parent / 'tlc.log'
                log.write_text(log.read_text().replace('0 states left', '1 states left'))
                with self.assertRaisesRegex(ValueError, 'raw diagnostic'):
                    self.inspect(path)
            else:
                self.assertEqual(self.inspect(path)['status'], 'incomplete_timeout')

    def test_named_defect_and_witness_remain_distinct(self):
        case = dict(self.case, expect='invariant:Bad')
        path = self.receipt('bad', status='expected_violation', expected=case['expect'])
        self.assertEqual(self.inspect(path, case)['status'], 'expected_violation')
        witness = dict(case, witness='reaches target')
        path = self.receipt('witness', status='witnessed', expected=case['expect'], witness=witness['witness'])
        self.assertEqual(self.inspect(path, witness)['purpose'], 'reachability')
        path = self.receipt('wrong', status='expected_violation', expected=case['expect'], violated='Another')
        with self.assertRaisesRegex(ValueError, 'raw diagnostic'):
            self.inspect(path, case)

    def test_current_counterexample_blocks_old_green(self):
        self.receipt('good', stamp='1')
        self.receipt('bad', status='unexpected_violation', stamp='2')
        report = evidence.collect([self.case], [self.runs], self.spec)
        self.assertIsNone(report['cases'][0]['selected'])
        self.assertEqual(report['cases'][0]['disposition'], 'conflicting_result')

    def test_diagnostic_oom_needs_later_complete_receipt_and_survives_bundling(self):
        case = dict(self.temporal_case(), heap='1g')
        failed = self.temporal_receipt('diagnostic-oom')
        row = evidence.collect([case], [self.runs], self.spec)['cases'][0]
        self.assertIsNone(row['selected'])
        self.assertEqual(row['disposition'], 'conflicting_result')
        completed = self.temporal_receipt('completed', stamp='2', oom=False, heap='1g')
        report = evidence.collect([case], [self.runs], self.spec)
        row = report['cases'][0]
        self.assertEqual(row['selected']['result'], str(completed))
        reconciliation, = row['reconciled_diagnostics']
        self.assertEqual(reconciliation['resolved_by'], str(completed))
        self.assertEqual(reconciliation['run']['status'], 'unexpected_violation')
        self.assertEqual(reconciliation['run']['heap'], '512m')
        self.assertEqual(row['selected']['heap'], '1g')
        messages = reconciliation['run']['diagnostic_failure']['messages']
        self.assertEqual([m['code'] for m in messages], [2116, 2264, 1003])
        original_log = (failed.parent / 'tlc.log').read_bytes()
        output = self.root / 'bundle'
        evidence.bundle(report, output)
        evidence.shutil.rmtree(self.runs)
        retained = output / reconciliation['run']['bundle_result']
        self.assertEqual((retained.parent / 'tlc.log').read_bytes(), original_log)
        self.assertEqual(self.inspect(retained, case)['status'], 'unexpected_violation')
        self.assertEqual(self.inspect(output / row['selected']['bundle_result'], case)['status'],
                         'expected_violation')

    def test_old_success_does_not_excuse_new_diagnostic_failure(self):
        case = self.temporal_case()
        self.temporal_receipt('old', stamp='1', oom=False)
        self.temporal_receipt('new', stamp='2')
        row = evidence.collect([case], [self.runs], self.spec)['cases'][0]
        self.assertIsNone(row['selected'])
        self.assertEqual(row['reconciled_diagnostics'], [])

    def test_contrary_diagnostics_still_block_later_expected_result(self):
        case = self.temporal_case()
        for name, violation, extra in [
                ('wrong-property', 'Another', None),
                ('invariant', 'Completes', (2110, 'Invariant Safety is violated.')),
                ('deadlock', 'Completes', (2114, 'Deadlock reached.')),
                ('other-tool-error', 'Completes', (1001, 'Unexpected exception'))]:
            with self.subTest(name=name):
                failed = self.temporal_receipt(name, violation=violation, extra=extra)
                later = self.temporal_receipt(name + '-later', stamp='2', oom=False)
                row = evidence.collect([case], [self.runs], self.spec)['cases'][0]
                self.assertIsNone(row['selected'])
                self.assertEqual(row['disposition'], 'conflicting_result')
                self.assertEqual(row['reconciled_diagnostics'], [])
                evidence.shutil.rmtree(failed.parent)
                evidence.shutil.rmtree(later.parent)

    def test_oom_before_counterexample_is_not_reconciled(self):
        case = self.temporal_case()
        failed = self.temporal_receipt('oom')
        log = failed.parent / 'tlc.log'
        text = log.read_text()
        start = text.index('@!@!@STARTMSG 1003:1')
        text = text[:start]
        log.write_text(log.read_text()[start:] + text)
        self.temporal_receipt('later', stamp='2', oom=False)
        self.assertIsNone(evidence.collect([case], [self.runs], self.spec)['cases'][0]['selected'])

    def test_unnamed_temporal_diagnostic_requires_sole_configured_property(self):
        case = self.temporal_case()
        self.temporal_receipt('unnamed', violation=None)
        self.temporal_receipt('later', stamp='2', oom=False, violation=None)
        row = evidence.collect([case], [self.runs], self.spec)['cases'][0]
        self.assertEqual(row['selected']['status'], 'expected_violation')
        self.assertEqual(len(row['reconciled_diagnostics']), 1)
        self.temporal_case('Completes Another')
        path = self.temporal_receipt('ambiguous', violation=None)
        with self.assertRaisesRegex(ValueError, 'sole configured property'):
            self.inspect(path, case)

    def test_named_temporal_mismatch_blocks_even_if_producer_accepted_it(self):
        case = self.temporal_case()
        # The producer binds unnamed temporal output through the config; the
        # collector additionally refuses an explicitly different diagnostic.
        self.temporal_receipt('wrong', violation='Another', oom=False)
        self.temporal_receipt('later', stamp='2', oom=False)
        row = evidence.collect([case], [self.runs], self.spec)['cases'][0]
        self.assertIsNone(row['selected'])
        self.assertEqual(row['disposition'], 'conflicting_result')

    def test_compact_exports_are_stable_and_missing_rows_are_explicit(self):
        self.receipt()
        report = evidence.collect([self.case], [self.runs], self.spec)
        report['cases'][0]['selected']['heap'] = '1g'
        report['cases'][0]['case']['heap'] = '2g'
        report['cases'].insert(0, {'case': {'name': 'Absent', 'module': 'Absent',
                                  'config': 'configs/Absent.cfg', 'workers': 4, 'heap': '2g'},
                                  'selected': None, 'disposition': 'missing'})
        first, second = self.root / 'first', self.root / 'second'
        first.mkdir(); second.mkdir()
        evidence.write_compact(report, first, {'b.json': 'b', 'a.json': 'a'}, 'collector')
        report['cases'].reverse()
        evidence.write_compact(report, second, {'a.json': 'a', 'b.json': 'b'}, 'collector')
        for name in ('checks.csv', 'inputs.json'):
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
        with (first / 'checks.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual([r['case'] for r in rows], ['Absent', 'Tiny'])
        self.assertEqual([rows[0][k] for k in ('status', 'distinct', 'workers', 'heap', 'receipt_sha256')],
                         ['missing', '', '4', '2g', ''])
        self.assertEqual([rows[1][k] for k in ('status', 'distinct', 'workers', 'heap')],
                         ['complete', '2', '1', '1g'])
        inputs = json.loads((first / 'inputs.json').read_text())
        self.assertEqual(set(inputs['source_sha256']), {'Tiny.tla', 'Helper.tla', 'configs/Tiny.cfg', 'check.py'})
        self.assertEqual(inputs['source_sha256']['check.py'], self.current['check.py'])
        self.assertEqual(inputs['jar_sha256'], evidence.check.JAR_SHA256)
        self.assertEqual(inputs['catalog_sha256'], {'a.json': 'a', 'b.json': 'b'})

    def test_compact_exports_reject_conflicting_source_runner_or_jar_hashes(self):
        self.receipt()
        original = evidence.collect([self.case], [self.runs], self.spec)
        for changed in ('dependency', 'runner', 'jar'):
            with self.subTest(changed=changed):
                report = json.loads(json.dumps(original))
                other = json.loads(json.dumps(report['cases'][0]))
                other['case']['name'] = 'Other'
                if changed == 'dependency':
                    other['selected']['dependency_sha256']['Helper.tla'] = 'different'
                else:
                    other['selected'][changed + '_sha256'] = 'different'
                report['cases'].append(other)
                output = self.root / changed
                output.mkdir()
                with self.assertRaisesRegex(ValueError, 'Contradictory selected'):
                    evidence.write_compact(report, output, {}, 'collector')
                self.assertEqual(list(output.iterdir()), [])

    def test_bundle_rechecks_tool_bytes_after_selection(self):
        path = self.receipt()
        report = evidence.collect([self.case], [self.runs], self.spec)
        (path.parent / 'check.py').write_text('corrupt after collection')
        with self.assertRaisesRegex(ValueError, 'changed before bundling'):
            evidence.bundle(report, self.root / 'bundle')

    def test_incomplete_does_not_mask_completed_and_bundle_survives_original_removal(self):
        self.receipt('good', stamp='1')
        self.receipt('later', status='incomplete_timeout', stamp='2')
        report = evidence.collect([self.case], [self.runs], self.spec)
        row = report['cases'][0]
        self.assertEqual(row['selected']['status'], 'complete')
        self.assertEqual(row['other_current_runs'][0]['status'], 'incomplete_timeout')
        output = self.root / 'bundle'
        evidence.bundle(report, output)
        evidence.shutil.rmtree(self.runs)
        run = output / row['selected']['bundle_result']
        self.assertEqual(self.inspect(run)['status'], 'complete')
        self.assertFalse((run.parent / 'sources/Unused.tla').exists())

    def resume_receipt(self, name='resumed'):
        path = self.receipt(name)
        root, generation = path.parent, '26-09-27-18-17-05.941'
        lineage = root / 'lineage'
        original = lineage / 'original'
        original.mkdir(parents=True)
        for name in ('check.py', 'tool-source.json'):
            evidence.shutil.copyfile(root / name, original / name)
        evidence.shutil.copytree(root / 'sources', original / 'sources')
        data = json.loads(path.read_bytes())
        data.update(heap='512m', java={'path': '/usr/bin/java', 'version': 'test Java 21'}, locale='C')
        data['jar']['path'] = '/jar/tla2tools.jar'
        data['argv'] = ['/usr/bin/java', '-Xmx512m', '-XX:+UseParallelGC', '-Duser.language=en',
                        '-Duser.country=US', '-cp', '/jar/tla2tools.jar', 'tlc2.TLC', '-tool',
                        '-workers', '1', '-fp', '0', '-metadir', '/old/metadir',
                        '-config', 'configs/Tiny.cfg', 'Tiny']
        prefix = (root / 'tlc.log').read_text().split('@!@!@STARTMSG')[0]
        def message(code, body):
            return f'@!@!@STARTMSG {code}:0 @!@!@\n{body}\n@!@!@ENDMSG {code} @!@!@\n'
        startup = message(2187, 'Running breadth-first search with fp 0 and seed 5747212894843556478')
        checkpoint = message(2195, 'Checkpointing.') + message(2196, 'Checkpoint completed.')
        original_log = prefix + startup + checkpoint
        (original / 'tlc.log').write_text(original_log)
        frozen = dict(data, status='running')
        evidence.write_json(original / 'result.json', frozen)
        (lineage / 'metadir.txt').write_text(generation + '\n')
        (lineage / 'capture-helper.py').write_text('# fixture capture\n')
        rescue = {'format': 1, 'kind': 'manual-tlc-checkpoint-rescue', 'argv': data['argv'],
                  'cwd': data['cwd'], 'fingerprint': 0, 'seed': 5747212894843556478,
                  'java_version': data['java']['version'], 'jar_sha256': evidence.check.JAR_SHA256,
                  'generation': generation, 'log_request_offset': len((prefix + startup).encode()),
                  'log_ack_end_offset': len(original_log.rstrip('\n').encode()),
                  'capture_helper_sha256': recover.digest(lineage / 'capture-helper.py')}
        members = {p.relative_to(lineage).as_posix(): {'bytes': p.stat().st_size, 'sha256': recover.digest(p)}
                   for p in lineage.rglob('*') if p.is_file()}
        members['states/' + generation + '/state.chkpt'] = {'bytes': 1, 'sha256': evidence.sha(b'x')}
        evidence.write_json(lineage / 'files.json', members)
        evidence.write_json(lineage / 'rescue.json', rescue)
        for name in ('files.json', 'rescue.json'):
            members[name] = {'bytes': (lineage / name).stat().st_size, 'sha256': recover.digest(lineage / name)}
        evidence.write_json(lineage / 'archive-members.json', members)
        reference = {'sha256': 'a' * 64, 'bytes': 1, 'files': len(members)}
        evidence.write_json(lineage / 'artifact.json', reference)
        evidence.shutil.copyfile(recover.__file__, root / 'recover.py')
        evidence.shutil.copyfile(recover.__file__, self.spec / 'recover.py')
        invocation = recover.invocation(lineage, frozen, rescue)
        evidence.write_json(root / 'invocation.json', invocation)
        data.pop('runner_sha256')
        data.update(kind=recover.KIND, schema=1, producer_sha256=invocation['producer_sha256'],
                    classifier_sha256=invocation['classifier_sha256'],
                    invocation_sha256=recover.digest(root / 'invocation.json'),
                    resumed_from={'kind': 'artifact', 'reference': reference,
                                  'rescue_sha256': recover.digest(lineage / 'rescue.json')},
                    argv=recover.command(frozen, rescue, '/usr/bin/java', Path('/jar/tla2tools.jar'), Path('/state/states')))
        log = (root / 'tlc.log').read_text()
        log = (prefix + startup + message(2197, 'Starting recovery from checkpoint /state/states/' + generation + '/')
               + message(2198, 'Recovery completed. 1 states examined. 1 states on queue.') + log[len(prefix):])
        (root / 'tlc.log').write_text(log)
        data.update(log_sha256=recover.digest(root / 'tlc.log'), recovery=recover.recovery_diagnostics(root / 'tlc.log'))
        path.unlink()
        path = root / 'resume-result.json'
        evidence.write_json(path, data)
        return path

    def test_recovery_visible_and_bundle_preserves_lineage_not_bulk_worker_files(self):
        path = self.resume_receipt()
        (path.parent / 'source.tar.gz').write_bytes(b'worker payload excluded')
        report = evidence.collect([self.case], [self.runs], self.spec)
        selected = report['cases'][0]['selected']
        self.assertIsNotNone(selected, report['rejected_receipts'])
        self.assertEqual(selected['runner_file'], 'recover.py')
        self.assertEqual(selected['recovered_states'], 1)
        self.assertEqual(list(evidence.receipts(self.runs)), [path])
        output = self.root / 'bundle'
        evidence.bundle(report, output)
        evidence.write_compact(report, output, {}, 'collector')
        with (output / 'checks.csv').open() as stream:
            row = list(csv.DictReader(stream))[0]
        self.assertEqual((row['runner'], row['recovered_states']), ('recover.py', '1'))
        self.assertEqual(json.loads((output / 'inputs.json').read_bytes())['source_sha256']['recover.py'], recover.digest(Path(recover.__file__)))
        evidence.shutil.rmtree(self.runs)
        bundled = output / selected['bundle_result']
        self.assertEqual(self.inspect(bundled)['status'], 'complete')
        self.assertFalse((bundled.parent / 'source.tar.gz').exists())
        self.assertEqual(json.loads((bundled.parent / 'lineage/original/result.json').read_bytes())['status'], 'running')

    def test_recovery_missing_generation_and_wrong_seed_cannot_be_green(self):
        for mutation in ('absent', 'generation', 'seed'):
            with self.subTest(mutation=mutation):
                path = self.resume_receipt(mutation)
                log = path.parent / 'tlc.log'
                text = log.read_text()
                if mutation == 'absent':
                    text = evidence.re.sub(r'@!@!@STARTMSG 219[78]:.*?@!@!@ENDMSG 219[78] @!@!@\n', '', text, flags=evidence.re.S)
                elif mutation == 'generation':
                    text = text.replace('/state/states/26-09-27-18-17-05.941/', '/other/')
                else:
                    text = text.replace('seed 5747212894843556478', 'seed 0')
                log.write_text(text)
                data = json.loads(path.read_bytes())
                data['log_sha256'] = recover.digest(log)
                if mutation != 'absent':
                    data['recovery'] = recover.recovery_diagnostics(log)
                evidence.write_json(path, data)
                with self.assertRaisesRegex(ValueError, 'recovery|diagnostics'):
                    self.inspect(path)

    def test_changed_recovery_lineage_source_and_producer_are_rejected(self):
        for target in ('seed', 'source', 'producer', 'invocation', 'missing'):
            with self.subTest(target=target):
                path = self.resume_receipt(target)
                if target == 'seed':
                    candidate = path.parent / 'lineage/rescue.json'
                    data = json.loads(candidate.read_bytes()); data['seed'] = 0
                    evidence.write_json(candidate, data)
                    manifest = path.parent / 'lineage/archive-members.json'
                    members = json.loads(manifest.read_bytes())
                    members['rescue.json'] = {'bytes': candidate.stat().st_size, 'sha256': recover.digest(candidate)}
                    evidence.write_json(manifest, members)
                elif target == 'source':
                    (path.parent / 'lineage/original/sources/Helper.tla').write_text('changed')
                elif target == 'producer':
                    data = json.loads(path.read_bytes()); data['producer_sha256'] = 'different'
                    evidence.write_json(path, data)
                elif target == 'invocation':
                    candidate = path.parent / 'invocation.json'
                    data = json.loads(candidate.read_bytes()); data['producer_sha256'] = 'different'
                    evidence.write_json(candidate, data)
                    data = json.loads(path.read_bytes()); data['invocation_sha256'] = recover.digest(candidate)
                    evidence.write_json(path, data)
                else:
                    (path.parent / 'lineage/original/result.json').unlink()
                with self.assertRaises((ValueError, OSError)):
                    self.inspect(path)

    def test_original_violation_cannot_be_hidden_by_a_completed_continuation(self):
        path = self.resume_receipt()
        lineage = path.parent / 'lineage'
        log = lineage / 'original/tlc.log'
        log.write_text(log.read_text() + '@!@!@STARTMSG 2110:1 @!@!@\nInvariant Bad is violated.\n@!@!@ENDMSG 2110 @!@!@\n')
        # Keep manifests coherent to isolate semantic rejection from byte corruption.
        members = json.loads((lineage / 'archive-members.json').read_bytes())
        members['original/tlc.log'] = {'bytes': log.stat().st_size, 'sha256': recover.digest(log)}
        declared = {name: value for name, value in members.items() if name not in {'files.json', 'rescue.json'}}
        evidence.write_json(lineage / 'files.json', declared)
        members['files.json'] = {'bytes': (lineage / 'files.json').stat().st_size, 'sha256': recover.digest(lineage / 'files.json')}
        evidence.write_json(lineage / 'archive-members.json', members)
        with self.assertRaisesRegex(ValueError, 'already reports an error/violation'):
            self.inspect(path)

    def test_checkpoint_corruption_and_missing_members_reject_before_execution(self):
        snapshot = self.root / 'snapshot'; snapshot.mkdir()
        (snapshot / 'one').write_bytes(b'known checkpoint bytes')
        job = {'script': 'recover.sh', 'args': [], 'source': {'digest': 'source'},
               'config': {'env': {}, 'checkpoint_script': 'checkpoint.sh'}}
        raw = json.dumps({'format': 1, 'identity': recover.checkpoint_module().identity(job),
                          'files': [{'path': 'one', 'bytes': 22, 'chunks': [{'bytes': 22, 'sha256': evidence.sha(b'known checkpoint bytes')}]}]}).encode()
        job['resume'] = {'checkpoint': {'sha256': evidence.sha(raw)}}
        recover.verify_worker_snapshot(snapshot, raw, job)
        (snapshot / 'one').write_bytes(b'wrong checkpoint bytes')
        with self.assertRaisesRegex(ValueError, 'member differs'):
            recover.verify_worker_snapshot(snapshot, raw, job)
        (snapshot / 'one').unlink()
        with self.assertRaises(FileNotFoundError):
            recover.verify_worker_snapshot(snapshot, raw, job)

    def test_recovery_timeout_must_be_finite_positive(self):
        from unittest.mock import patch
        for timeout in (-1, 0, float('inf'), float('nan')):
            args = recover.parser().parse_args(['--reference', 'unused', '--output', 'unused'])
            args.timeout = timeout
            with patch.object(recover.sys, 'platform', 'linux'), self.assertRaisesRegex(ValueError, 'finite and positive'):
                recover.run(args)


if __name__ == '__main__':
    unittest.main()
