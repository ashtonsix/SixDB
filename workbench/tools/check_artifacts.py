#!/usr/bin/env python3
"""Check source isolation, retention failures/retries, and verified restoration offline."""

import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import artifacts
import datasets
from evidence import digest, read_measurements, verify_compact
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

    def test_upload_failure_preserves_source_and_writes_no_evidence(self):
        failure = subprocess.CompletedProcess([], 1, "", "unavailable")
        with patch.object(artifacts, "aws", return_value=failure):
            with self.assertRaisesRegex(RuntimeError, "local data preserved"):
                artifacts.retain(self.source, self.root / "evidence")
        self.assertTrue((self.source / "run.json").exists())
        self.assertFalse((self.root / "evidence").exists())

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


if __name__ == "__main__":
    unittest.main()
