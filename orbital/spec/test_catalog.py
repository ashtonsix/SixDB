#!/usr/bin/env python3
"""Offline catalog and suite capture controls; no TLC or model claims."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import catalog
import suite


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="orbital-catalog-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "spec"
        self.root.mkdir()
        self.output = self.root.parent / "suite"
        self.quick = {"name": "Quick", "module": "Model", "config": "configs/quick.cfg", "tier": "quick"}
        self.witness = {"name": "Witness", "module": "Model", "config": "configs/witness.cfg",
                        "tier": "growth", "expect": "invariant:NeverSeen", "witness": "event observed"}
        self.write("first-cases.json", {"cases": [self.quick]})
        self.write("second-cases.json", {"cases": [self.witness]})
        self.manifest = self.write("cases.json", {"includes": ["first-cases.json", "second-cases.json"]})
        (self.root / "Model.tla").write_text("model source\n")
        (self.root / "configs").mkdir()
        (self.root / "configs/quick.cfg").write_text("quick configuration\n")
        (self.root / "configs/witness.cfg").write_text("witness configuration\n")
        (self.root / "check.py").write_text("# captured runner\n")

    def write(self, name, value):
        path = self.root / name
        path.write_text(json.dumps(value) + "\n")
        return path

    def invoke(self, *args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(suite, "__file__", str(self.root / "suite.py")), \
                patch("sys.argv", ["suite.py", *args]), \
                redirect_stdout(stdout), redirect_stderr(stderr):
            try:
                status = suite.main()
            except SystemExit as error:
                status = error.code
        return status, stdout.getvalue(), stderr.getvalue()

    def test_family_compatibility_and_duplicate_names_across_includes(self):
        self.assertEqual(catalog.load_cases(self.root / "first-cases.json"), [self.quick])
        self.assertEqual(catalog.load_cases(self.manifest), [self.quick, self.witness])
        self.assertEqual(set(catalog.catalog_paths(self.manifest)),
                         {self.manifest, self.root / "first-cases.json", self.root / "second-cases.json"})
        self.write("second-cases.json", {"cases": [{**self.witness, "name": "Quick"}]})
        with self.assertRaisesRegex(ValueError, "Duplicate case name.*Quick"):
            catalog.load_cases(self.manifest)

    def test_include_cycle_is_reported(self):
        self.write("second-cases.json", {"includes": ["cases.json"]})
        with self.assertRaisesRegex(ValueError, "include cycle.*cases.json.*second-cases.json.*cases.json"):
            catalog.load_cases(self.manifest)

    def test_invalid_heap_is_rejected_before_dispatch(self):
        for heap in (0, "0g", "1", "-1g", "2t", "1g -XX:+UseG1GC"):
            with self.subTest(heap=heap):
                self.write("first-cases.json", {"cases": [{**self.quick, "heap": heap}]})
                with patch.object(suite.subprocess, "run") as run:
                    status, _, err = self.invoke("--output", str(self.output))
                self.assertEqual(status, 2)
                self.assertIn("Invalid heap for Quick", err)
                self.assertFalse(self.output.exists())
                run.assert_not_called()

    def test_listing_and_unknown_selection_do_not_create_a_run(self):
        with patch.object(suite.subprocess, "run") as run:
            status, out, err = self.invoke("--list")
            self.assertEqual(status, 0, err)
            self.assertIn("Quick\tquick\tModel\tconfigs/quick.cfg", out)
            self.assertIn("Witness\tgrowth\tModel\tconfigs/witness.cfg\tinvariant:NeverSeen\tevent observed", out)
            status, out, err = self.invoke("--list", "--tier", "quick")
            self.assertEqual(status, 0, err)
            self.assertNotIn("Witness", out)
            status, out, err = self.invoke("--case", "Missing", "--output", str(self.output))
            self.assertEqual(status, 2)
            self.assertIn("Unknown case: Missing", err)
            self.assertFalse(self.output.exists())
            run.assert_not_called()

    def fake_run(self, command, **kwargs):
        name = Path(command[command.index("--config") + 1]).stem
        result_path = self.output / f"{name}.json"
        result_path.write_text(json.dumps({"status": "witnessed" if name == "witness" else "complete",
                                           "states": {"distinct": 2, "queue": 0}, "depth": 1,
                                           "elapsed_seconds": 0.01, "peak_rss_bytes": 0,
                                           "heap": command[command.index("--heap") + 1],
                                           "workers": int(command[command.index("--workers") + 1]),
                                           "expectation": "invariant:NeverSeen" if name == "witness" else None,
                                           "purpose": "reachability" if name == "witness" else "check"}))
        return subprocess.CompletedProcess(command, 0, json.dumps({"result": str(result_path)}), "")

    def test_catalog_resources_reach_checker_and_receipt(self):
        self.write("second-cases.json", {"cases": [{**self.witness, "heap": "2g", "workers": 4}]})
        with patch.object(suite.subprocess, "run", side_effect=self.fake_run):
            status, _, err = self.invoke("--tier", "all", "--output", str(self.output))
        self.assertEqual(status, 0, err)
        report = json.loads((self.output / "summary.json").read_text())
        self.assertEqual([(row["name"], row["heap"], row["workers"]) for row in report["cases"]],
                         [("Quick", "512m", 1), ("Witness", "2g", 4)])
        frozen = self.output / "suite-source"
        self.assertEqual(catalog.load_cases(frozen / report["catalog"])[1]["heap"], "2g")
        status, out, err = self.invoke("--list")
        self.assertEqual(status, 0, err)
        self.assertIn("\t4\t2g", out)

    def test_selection_and_captured_catalog_agree_despite_checkout_edit(self):
        original_read = catalog.read_catalog

        def edit_after_selection(manifest):
            answer = original_read(manifest)
            self.write("second-cases.json", {"cases": []})
            return answer

        with patch.object(suite, "read_catalog", side_effect=edit_after_selection), \
                patch.object(suite.subprocess, "run", side_effect=self.fake_run) as run:
            status, _, err = self.invoke("--tier", "quick", "--case", "Witness", "--output", str(self.output))
        self.assertEqual(status, 0, err)
        report = json.loads((self.output / "summary.json").read_text())
        self.assertEqual(report["selection"], [self.witness])
        self.assertEqual([row["name"] for row in report["cases"]], ["Witness"])
        frozen = self.output / "suite-source"
        self.assertEqual(catalog.load_cases(frozen / report["catalog"]), [self.quick, self.witness])
        for name, digest in report["source_sha256"].items():
            self.assertEqual(hashlib.sha256((frozen / name).read_bytes()).hexdigest(), digest)
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--module") + 1], str(frozen / "Model.tla"))
        self.assertEqual(command[command.index("--config") + 1], str(frozen / "configs/witness.cfg"))
        self.assertEqual(command[command.index("--expect") + 1], "invariant:NeverSeen")
        self.assertEqual(command[command.index("--witness") + 1], "event observed")

    def test_external_flat_selection_is_captured_and_keeps_model_paths(self):
        manifest = self.root.parent / "missing.json"
        manifest.write_text(json.dumps({"cases": [self.quick]}))
        with patch.object(suite.subprocess, "run", side_effect=self.fake_run) as run:
            status, _, err = self.invoke("--manifest", str(manifest), "--output", str(self.output))
        self.assertEqual(status, 0, err)
        report = json.loads((self.output / "summary.json").read_text())
        frozen = self.output / "suite-source"
        self.assertEqual(catalog.load_cases(frozen / report["catalog"]), [self.quick])
        self.assertEqual(run.call_args.args[0][run.call_args.args[0].index("--config") + 1],
                         str(frozen / "configs/quick.cfg"))


if __name__ == "__main__":
    unittest.main()
