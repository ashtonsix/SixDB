#!/usr/bin/env python3
"""Check source isolation, retention failures/retries, and verified restoration offline."""

import io
import csv
from contextlib import redirect_stdout
import json
from pathlib import Path
import subprocess
import shutil
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import artifacts
import worker_cache
import datasets
from evidence import digest, read_measurements, summarize, verify_compact, verify_exports
from experiment import source_files


class ArtifactsCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "run"
        self.source.mkdir()
        data = {
            "benchmark.json": json.dumps({"context": {}, "benchmarks": [
                {"name": "case/method/manual_time", "run_type": "iteration", "repetition_index": 0,
                 "real_time": 12.125, "iterations": 123, "time_unit": "ns", "updates": 64},
                {"name": "case/method/manual_time_mean", "run_type": "aggregate", "real_time": 12.125}]}),
            "accounting.csv": "case,method,correct\ncase,method,1\n",
            "summary.csv": "case,ns\ncase,12.125\n", "summary.md": "# Result\n",
            "source.tar.gz": "fixture", "git-head.stdout": "fixture-commit\n", "compiler.stdout": "compiler\n",
        }
        for name, content in data.items():
            (self.source / name).write_text(content)
        self.receipt = {"status": "complete", "source_unchanged": True, "started_utc": "fixture",
                        "source_digest": "fixture", "config": {"repetitions": 1}, "platform": "fixture",
                        "commands": [], "artifact_sha256": {name: digest(self.source / name) for name in data}}
        (self.source / "run.json").write_text(json.dumps(self.receipt))
        self.objects = {}

    def fake_aws(self, *args, **kwargs):
        key = args[args.index("--key") + 1]
        if args[0] == "put-object":
            self.assertEqual(args[args.index("--if-none-match") + 1], "*")
            if key in self.objects:
                return subprocess.CompletedProcess(args, 1, "", "PreconditionFailed")
            self.objects[key] = Path(args[args.index("--body") + 1]).read_bytes()
        else:
            if key not in self.objects:
                return subprocess.CompletedProcess(args, 1, "", "missing object")
            Path(args[-1]).write_bytes(self.objects[key])
        return subprocess.CompletedProcess(args, 0, "{}", "")

    def test_retain_retry_and_fetch(self):
        destination = self.root / "evidence"
        with patch.object(artifacts, "aws", self.fake_aws), patch.object(artifacts, "ROOT", self.root):
            artifacts.retain(self.source, destination)
            artifacts.retain(self.source, destination)
            self.assertEqual(len(self.objects), 1)
            receipt, data = read_measurements(destination)
            self.assertEqual(data["benchmarks"][0]["real_time"], 12.125)
            self.assertEqual(len(data["benchmarks"]), 1)
            restored = self.root / "build/restored"
            artifacts.fetch(destination, restored)
            for path in artifacts.files(self.source):
                self.assertEqual(path.read_bytes(), (restored / path.name).read_bytes())
            (destination / "samples.csv").write_text("corrupted")
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                read_measurements(destination)
            reference = json.loads((destination / "artifact.json").read_text())
            self.objects[reference["key"]] = b"corrupted remote bundle"
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                artifacts.fetch(destination, self.root / "build/bad")
            self.assertFalse((self.root / "build/bad").exists())

    def test_recovery_outside_checkout_preserves_existing_destination(self):
        evidence = self.root / 'evidence'
        destination = self.root / 'another-volume/recovered'
        with patch.object(artifacts, 'aws', self.fake_aws), \
                patch.object(artifacts, 'ROOT', self.root / 'checkout'):
            artifacts.retain(self.source, evidence)
            artifacts.fetch(evidence, destination)
            for path in artifacts.files(self.source):
                self.assertEqual(path.read_bytes(), (destination / path.name).read_bytes())
            (destination / 'notes.txt').write_text('keep my notes')
            with patch.object(artifacts, 'aws') as aws, self.assertRaisesRegex(ValueError, 'already exists'):
                artifacts.fetch(evidence, destination, selected=['summary.csv'])
            aws.assert_not_called()
            self.assertEqual((destination / 'notes.txt').read_text(), 'keep my notes')

    def test_routine_preview_is_bounded_but_checks_every_ignore_rule(self):
        staging = self.root / 'staging'
        staging.mkdir()
        for index in range(20):
            (staging / f'{index:02d}.txt').write_text('x' * (index + 1))
        (staging / 'small.stderr').write_text('')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        (self.root / '.gitignore').write_text('*.stderr\n')
        output = io.StringIO()
        with redirect_stdout(output):
            ignored = artifacts.preview_export(staging, self.root / 'evidence')
        self.assertEqual(ignored, {'evidence/small.stderr'})
        self.assertIn('21 files', output.getvalue())
        self.assertIn('19.txt', output.getvalue())
        self.assertNotIn('00.txt', output.getvalue())
        self.assertIn('small.stderr', output.getvalue())
        self.assertLessEqual(len(output.getvalue().splitlines()), 10)
        output = io.StringIO()
        with redirect_stdout(output):
            artifacts.preview_export(staging, self.root / 'evidence', detailed=True)
        self.assertIn('00.txt', output.getvalue())

    def test_upload_failure_preserves_source_and_writes_no_evidence(self):
        failure = subprocess.CompletedProcess([], 1, "", "unavailable")
        with patch.object(artifacts, "aws", return_value=failure):
            with self.assertRaisesRegex(RuntimeError, "local data preserved"):
                artifacts.retain(self.source, self.root / "evidence")
        self.assertTrue((self.source / "run.json").exists())
        self.assertFalse((self.root / "evidence").exists())

    def test_generic_worker_bundle_has_no_required_run_schema(self):
        (self.source / 'run.json').write_text('an arbitrary script output')
        with patch.object(artifacts, 'aws', self.fake_aws), tempfile.TemporaryDirectory() as temp:
            reference = artifacts.publish(self.source, Path(temp), bucket='worker-bucket',
                                          region='us-west-2', validate_run=False)
            self.assertEqual(reference['kind'], 'files')
            self.assertEqual(reference['bucket'], 'worker-bucket')
            self.assertEqual(reference['region'], 'us-west-2')
            restored = self.root / 'generic'
            artifacts.restore_bundle(reference, restored)
            self.assertEqual((restored / 'run.json').read_text(), 'an arbitrary script output')

    def test_retain_partially_collected_study_reuses_complete_worker_archive(self):
        (self.source / 'probe').write_bytes(b'\x7fELF' + b'a' * 2 ** 20)
        self.receipt['artifact_sha256']['probe'] = artifacts.sha256(self.source / 'probe')
        (self.source / 'run.json').write_text(json.dumps(self.receipt))
        output = self.root / 'worker-output'
        output.mkdir()
        shutil.copytree(self.source, output / 'study')
        (output / 'unrelated.csv').write_text('different study\n')
        job = self.root / 'build/workers/fixture'
        job.mkdir(parents=True)
        with patch.object(artifacts, 'aws', self.fake_aws), patch.object(artifacts, 'ROOT', self.root), \
                tempfile.TemporaryDirectory() as temp:
            reference = artifacts.publish(output, Path(temp), validate_run=False)
            worker_cache.collect(job, reference)
            self.assertFalse((job / 'results/study/probe').exists())
            manifest = json.loads((job / 'collection.json').read_text())['members']
            evidence = self.root / 'evidence'
            with patch.object(artifacts, 'remote_manifest', return_value=manifest) as remote, \
                    patch.object(artifacts, 'publish') as publish:
                artifacts.retain(job / 'results/study', evidence)
            remote.assert_called_once_with(reference)
            publish.assert_not_called()
            scoped = json.loads((evidence / 'artifact.json').read_text())
            self.assertEqual(scoped['sha256'], reference['sha256'])
            self.assertEqual(scoped['subdirectory'], 'study')
            recovered = self.root / 'build/recovered'
            artifacts.fetch(evidence, recovered)
            for original in artifacts.files(self.source):
                self.assertEqual(original.read_bytes(), (recovered / original.name).read_bytes())
            self.assertFalse((recovered / 'unrelated.csv').exists())
            selected = self.root / 'build/selected'
            artifacts.fetch(evidence, selected, selected=['probe'])
            self.assertEqual([p.name for p in artifacts.files(selected)], ['probe'])
            (job / 'results/study/summary.csv').write_text('changed locally')
            with patch.object(artifacts, 'remote_manifest', return_value=manifest), \
                    self.assertRaisesRegex(ValueError, 'differs from archived'):
                artifacts.retain(job / 'results/study', self.root / 'changed')
            self.assertFalse((self.root / 'changed').exists())
            (job / 'results/study/summary.csv').write_bytes((self.source / 'summary.csv').read_bytes())
            bad = dict(manifest)
            bad['study/probe'] = bad['study/probe'] | {'sha256': '0' * 64}
            with patch.object(artifacts, 'remote_manifest', return_value=bad), \
                    self.assertRaisesRegex(ValueError, 'missing or changed'):
                artifacts.retain(job / 'results/study', self.root / 'bad-archive')

    def test_scoped_reference_validates_scope_and_entire_archive(self):
        with patch.object(artifacts, 'aws', self.fake_aws), tempfile.TemporaryDirectory() as temp:
            reference = artifacts.publish(self.source, Path(temp))
            for scope in ['../escape', '/absolute', 'missing', 'summary.csv']:
                with self.subTest(scope=scope), self.assertRaises(ValueError):
                    artifacts.restore_bundle(reference | {'subdirectory': scope}, self.root / 'scoped')
                self.assertFalse((self.root / 'scoped').exists())

    def test_selected_fetch_avoids_unrelated_expansion_and_input_restoration(self):
        binary = self.source / 'bin/probe'
        binary.parent.mkdir()
        binary.write_bytes(b'kept executable\n')
        binary.chmod(0o755)
        (self.source / 'full.asm').write_bytes(b'expanded disassembly\n' * 100_000)
        reference_path = self.root / 'artifact.json'
        with patch.object(artifacts, 'aws', self.fake_aws), patch.object(artifacts, 'ROOT', self.root), \
                patch.object(artifacts, 'restore_inputs') as inputs, tempfile.TemporaryDirectory() as temp:
            reference = artifacts.publish(self.source, Path(temp), validate_run=False)
            reference_path.write_text(json.dumps(reference))
            restored = self.root / 'build/selected'
            output = io.StringIO()
            with redirect_stdout(output):
                artifacts.fetch(reference_path, restored, selected=['bin/probe', 'summary.csv'])
            self.assertEqual({str(p.relative_to(restored)) for p in artifacts.files(restored)},
                             {'bin/probe', 'summary.csv'})
            self.assertEqual((restored / 'bin/probe').read_bytes(), binary.read_bytes())
            self.assertEqual((restored / 'bin/probe').stat().st_mode & 0o777, 0o755)
            inputs.assert_not_called()
            self.assertIn('Partial recovery', output.getvalue())
            self.assertIn('input dataset restoration were not requested', output.getvalue())
            self.assertFalse(any(restored.parent.glob('.fetch-*')))
            self.objects[reference['key']] = b'corrupt archive'
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                artifacts.fetch(reference_path, self.root / 'build/corrupt', selected=['summary.csv'])
            self.assertFalse((self.root / 'build/corrupt').exists())

    def test_selected_fetch_requires_requested_members_and_full_archive_count(self):
        with patch.object(artifacts, 'aws', self.fake_aws), tempfile.TemporaryDirectory() as temp:
            reference = artifacts.publish(self.source, Path(temp))
            destination = self.root / 'selected'
            for selected, error in [(['missing.txt'], 'missing from bundle'),
                                    (['../escape'], 'relative file paths'),
                                    (['summary.csv', './summary.csv'], 'distinct files')]:
                with self.subTest(selected=selected), self.assertRaisesRegex(ValueError, error):
                    artifacts.restore_bundle(reference, destination, selected=selected)
                self.assertFalse(destination.exists())
            with self.assertRaisesRegex(ValueError, 'file count mismatch'):
                artifacts.restore_bundle(dict(reference, files=1), destination, selected=['summary.csv'])
            self.assertFalse(destination.exists())

    def test_selected_fetch_still_rejects_unsafe_unselected_members(self):
        bundle = self.root / 'unsafe.tar.gz'
        with tarfile.open(bundle, 'w:gz') as archive:
            info = tarfile.TarInfo('selected.txt')
            info.size = 4
            archive.addfile(info, io.BytesIO(b'kept'))
            info = tarfile.TarInfo('unused-link')
            info.type = tarfile.SYMTYPE
            info.linkname = '../escape'
            archive.addfile(info)
        reference = dict(bucket='fixture', region='fixture', key='unsafe',
                         files=2, bytes=bundle.stat().st_size, sha256=digest(bundle))
        self.objects['unsafe'] = bundle.read_bytes()
        destination = self.root / 'selected'
        with patch.object(artifacts, 'aws', self.fake_aws), self.assertRaisesRegex(ValueError, 'unsafe'):
            artifacts.restore_bundle(reference, destination, selected=['selected.txt'])
        self.assertFalse(destination.exists())

    def test_changed_or_incomplete_run_is_rejected(self):
        (self.source / "source.tar.gz").unlink()
        with self.assertRaises(FileNotFoundError):
            artifacts.pack(self.source, self.root / "bundle.tar.gz")
        self.receipt["status"] = "running"
        (self.source / "run.json").write_text(json.dumps(self.receipt))
        with self.assertRaisesRegex(ValueError, "unfinished"):
            artifacts.pack(self.source, self.root / "bundle.tar.gz")

    def test_unsafe_archive_is_rejected(self):
        for name, kind in [("../escape", tarfile.REGTYPE), ("link", tarfile.SYMTYPE)]:
            bundle = self.root / "unsafe.tar.gz"
            with tarfile.open(bundle, "w:gz") as archive:
                info = tarfile.TarInfo(name)
                info.type = kind
                info.linkname = "../escape"
                archive.addfile(info, io.BytesIO())
            with self.assertRaisesRegex(ValueError, "unsafe"):
                artifacts.unpack(bundle, self.root / "output")

    def test_snapshot_excludes_tracked_evidence_and_build(self):
        repo = self.root / "repo"
        repo.mkdir()
        subprocess.run(["git", "init", "-q", str(repo)], check=True)
        for name in ("probe.cpp", "workbench/spikes/example/evidence/source.tar.gz", "build/old.bin"):
            path = repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture")
        subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
        self.assertEqual(set(source_files(repo)), {"probe.cpp"})
        (repo / "workbench/spikes/example/evidence/new.csv").write_text("new evidence")
        self.assertEqual(set(source_files(repo)), {"probe.cpp"})

    def test_generic_counts_and_shared_input_recovery(self):
        with patch.object(artifacts, 'aws', self.fake_aws), patch.object(artifacts, 'ROOT', self.root), \
                patch.object(datasets, 'ROOT', self.root):
            prepared = datasets.cached('fixture', self.source / 'summary.csv', {}, {},
                                       lambda p: (p / 'values').write_bytes(b'original input\n'))
            meta = datasets.verify(prepared)
            self.receipt['inputs'] = {'inputs': {'path': str(prepared), 'id': meta['id'], 'key': meta['key']}}
            self.receipt['compact'] = {'files': ['accounting.csv', 'summary.md'],
                                       'regenerate': ['python3', 'analysis.py', '{evidence}']}
            (self.source / 'accounting.csv').write_bytes(b'case,count\r\nfixture,123\r\n')
            self.receipt['artifact_sha256']['accounting.csv'] = digest(self.source / 'accounting.csv')
            (self.source / 'run.json').write_text(json.dumps(self.receipt))
            artifacts.retain(self.source, self.root / 'evidence')
            self.assertEqual(len(self.objects), 2)  # one input and one run
            self.assertEqual((self.root / 'evidence/accounting.csv').read_bytes(), b'case,count\r\nfixture,123\r\n')
            self.assertEqual((self.root / 'evidence/.gitattributes').read_text(), '* -text\n')
            self.assertEqual(verify_compact(self.root / 'evidence')['regenerate'][0], 'python3')
            import shutil
            shutil.rmtree(prepared)
            restored = self.root / 'build/restored'
            artifacts.fetch(self.root / 'evidence', restored)
            self.assertEqual((restored / 'inputs/values').read_bytes(), b'original input\n')
            self.assertEqual(datasets.verify(prepared)['id'], meta['id'])
            # Repacking a restored run still references the input rather than embedding it.
            shutil.rmtree(prepared)  # The original machine/cache path need not exist.
            with tempfile.TemporaryDirectory() as temp:
                ref = artifacts.publish(restored, Path(temp))
                with tarfile.open(fileobj=io.BytesIO(self.objects[ref['key']])) as archive:
                    self.assertFalse(any(p.name.startswith('inputs/') for p in archive))
            self.assertEqual(json.loads((restored / 'input-artifacts.json').read_text())['inputs']['id'], meta['id'])
            (self.root / 'evidence/accounting.csv').write_text('corrupt')
            with self.assertRaisesRegex(ValueError, 'changed'):
                verify_compact(self.root / 'evidence')

    def test_preview_flags_ignored_export_before_upload(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        (self.root / '.gitignore').write_text('*.stderr\n')
        diagnostic = self.source / 'expected-error.stderr'
        diagnostic.write_text('expected compiler error\n')
        self.receipt['artifact_sha256'][diagnostic.name] = digest(diagnostic)
        self.receipt['compact'] = {'files': [diagnostic.name], 'regenerate': []}
        (self.source / 'run.json').write_text(json.dumps(self.receipt))
        destination = self.root / 'evidence'
        output = io.StringIO()
        with patch.object(artifacts, 'aws') as aws, redirect_stdout(output):
            ignored = artifacts.preview(self.source, destination)
            self.assertEqual(ignored, {'evidence/expected-error.stderr'})
            self.assertFalse(destination.exists())
            with self.assertRaisesRegex(ValueError, 'Git-ignored'):
                artifacts.retain(self.source, destination)
            aws.assert_not_called()
        self.assertIn('24 bytes', output.getvalue())
        self.assertIn('IGNORED by Git', output.getvalue())
        self.assertFalse(destination.exists())

    def test_selected_comparison_keeps_both_inputs_and_repetitions_with_full_recovery(self):
        # The guide's recipe: select a question before retention, keeping the
        # rest of the experiment recoverable without copying it into Git.
        for label, scale in [('baseline', 1), ('candidate', 2)]:
            rows = [dict(name=name, run_type='iteration', iterations=100,
                         repetition_index=rep, time_unit='ns', cpu_time=scale * (rep + 1),
                         real_time=scale * (rep + 1), encoded_bytes=64)
                    for name in ('read/64', 'update/64', 'diagnostic/controls') for rep in range(3)]
            (self.source / f'{label}.json').write_text(json.dumps({'context': {}, 'benchmarks': rows}))
        summarize([(label, self.source / f'{label}.json') for label in ('baseline', 'candidate')],
                  self.source / 'comparison', pattern='^(read|update)/', counters=['encoded_bytes'])
        selected = ['comparison/cases.csv', 'comparison/provenance.json']
        self.receipt['compact'] = {'files': selected, 'regenerate': []}
        self.receipt['artifact_sha256'] = {
            p.relative_to(self.source).as_posix(): digest(p)
            for p in artifacts.files(self.source) if p.name != 'run.json'}
        (self.source / 'run.json').write_text(json.dumps(self.receipt))
        destination = self.root / 'evidence'
        with patch.object(artifacts, 'aws', self.fake_aws), patch.object(artifacts, 'ROOT', self.root):
            artifacts.retain(self.source, destination)
            self.assertEqual(set(verify_compact(destination)['files_sha256']), set(selected))
            self.assertEqual(verify_compact(destination / 'comparison')['filter'], '^(read|update)/')
            with (destination / 'comparison/cases.csv').open() as file:
                kept = list(csv.DictReader(file))
            self.assertEqual({(r['input'], r['case']) for r in kept},
                             {(label, name) for label in ('baseline', 'candidate') for name in ('read/64', 'update/64')})
            self.assertTrue(all(len(json.loads(r['cpu_ns'])) == 3 for r in kept))
            restored = self.root / 'build/recovered'
            artifacts.fetch(destination, restored)
            for label in ('baseline', 'candidate'):
                self.assertEqual((restored / f'{label}.json').read_bytes(),
                                 (self.source / f'{label}.json').read_bytes())

    def test_preview_describes_coverage_and_duplicate_bytes_without_selecting(self):
        staging = self.root / 'staging'
        for profile in ('avx2', 'neon'):
            directory = staging / profile
            directory.mkdir(parents=True)
            (directory / 'samples.csv').write_text('name,value\nread/a,1\nread/a,2\nread/b,3\nwrite/a,4\n')
            (directory / 'description.json').write_text('{"same": "controls"}\n')
        arbitrary = staging / 'arbitrary.csv'
        arbitrary.write_bytes(b'\xff\xfe\x00')
        before = {p: p.read_bytes() for p in artifacts.files(staging)}
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertFalse(artifacts.preview_export(staging, self.root / 'evidence', detailed=True))
        self.assertIn('3 distinct names, 4 named rows', output.getvalue())
        self.assertIn('read 3; write 1', output.getvalue())
        self.assertIn('Across directories: samples.csv', output.getvalue())
        self.assertIn('Identical bytes: avx2/description.json = neon/description.json', output.getvalue())
        self.assertEqual({p: p.read_bytes() for p in artifacts.files(staging)}, before)
        self.assertFalse((self.root / 'evidence').exists())

    def test_verify_index_and_tree_cannot_be_masked_by_local_files(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        (self.root / '.gitignore').write_text('*.stderr\n')
        evidence = self.root / 'evidence'
        evidence.mkdir()
        diagnostic = evidence / 'expected-error.stderr'
        diagnostic.write_text('expected compiler error\n')
        manifest = evidence / 'provenance.json'
        def save_manifest():
            manifest.write_text(json.dumps({'files_sha256': {diagnostic.name: digest(diagnostic)},
                                            'full_bundle': 'artifact.json'}))
        save_manifest()
        (evidence / 'artifact.json').write_text('{}')
        def git(*args):
            return subprocess.check_output(['git', '-C', str(self.root), *args], text=True).strip()
        git('add', 'evidence')
        verify_compact(evidence)
        with self.assertRaisesRegex(ValueError, 'Missing Git evidence.*expected-error.stderr'):
            verify_exports(evidence, tree=':')
        renamed = diagnostic.with_suffix('.txt')
        diagnostic.rename(renamed)
        diagnostic = renamed
        save_manifest()
        git('add', 'evidence')
        tree = git('write-tree')
        self.assertEqual(verify_exports(evidence, tree=':'), 1)
        diagnostic.write_text('changed locally\n')
        with self.assertRaisesRegex(ValueError, 'changed'):
            verify_compact(evidence)
        self.assertEqual(verify_exports(evidence, tree=':'), 1)
        git('add', 'evidence')
        with self.assertRaisesRegex(ValueError, 'changed'):
            verify_exports(evidence, tree=':')
        self.assertEqual(verify_exports(evidence, tree=tree), 1)
        git('rm', '--cached', 'evidence/artifact.json')
        # Restore just the matching content in the index, keeping the missing reference.
        diagnostic.write_text('expected compiler error\n')
        git('add', 'evidence/expected-error.txt')
        with self.assertRaisesRegex(ValueError, 'Missing Git evidence.*artifact.json'):
            verify_exports(evidence, tree=':')


if __name__ == "__main__":
    unittest.main()
