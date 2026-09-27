#!/usr/bin/env python3
"""Small runner controls; these fixtures do not validate any Orbital model."""
from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import check

MODEL = r"""---- MODULE Tiny ----
EXTENDS Naturals
VARIABLE x
Init == x = 0
Next == IF x = 0 THEN x' = 1 ELSE UNCHANGED x
Spec == Init /\ [][Next]_x
Safe == x \in {0, 1}
NeverOne == x = 0
InitiallyFalse == x = 1
EventuallyOne == <> (x = 1)
====
"""


class ParserTests(unittest.TestCase):
    def output(self, messages):
        out = check.Output()
        for code, level, text in messages:
            out.feed(f"@!@!@STARTMSG {code}:{level} @!@!@\n{text}\n@!@!@ENDMSG {code} @!@!@\n", 1.0)
        return out

    def test_success_requires_exhaustion_receipt(self):
        out = self.output([(2193, 0, "Model checking completed. No error has been found."),
                           (2186, 0, "Finished")])
        self.assertEqual(check.classify(out, 0, None, None, None)[0], "tool_error")
        out.feed("2 states generated, 2 distinct states found, 0 states left on queue.\n", 2)
        self.assertEqual(check.classify(out, 0, None, None, None)[0], "complete")
        self.assertEqual(check.classify(out, 0, "timeout", None, None)[0], "incomplete_timeout")

    def test_wrong_property_or_extra_error_cannot_pass(self):
        out = self.output([(2110, 1, "Invariant Wrong is violated."), (2186, 0, "Finished")])
        self.assertEqual(check.classify(out, 12, None, "invariant:Expected", None)[0], "unexpected_violation")
        out = self.output([(2110, 1, "Invariant Expected is violated."), (1001, 1, "Out of memory"), (2186, 0, "Finished")])
        self.assertEqual(check.classify(out, 12, None, "invariant:Expected", None)[0], "unexpected_violation")
        self.assertEqual(check.classify(out, 0, "timeout", "invariant:Expected", None)[0], "incomplete_timeout")

    def test_progress_and_property_parser(self):
        out = check.Output()
        out.feed("Progress(7) at date: 12,000 states generated (1 s/min), 1,234 distinct states found (2 ds/min), 321 states left on queue.\n", 5)
        self.assertEqual(out.progress, [{"elapsed_seconds": 5, "depth": 7, "generated": 12000, "distinct": 1234, "queue": 321}])
        self.assertEqual(check.temporal_properties("SPECIFICATION Spec\nPROPERTIES\n A\n B\nINVARIANT Inv\n"), ["A", "B"])
        self.assertEqual(check.temporal_properties("PROPERTY A \\* B\n(* PROPERTY C *)\nINVARIANT Inv"), ["A"])


@unittest.skipUnless(sys.platform == "linux" and check.DEFAULT_JAR.is_file(), "Requires Linux and the existing pinned TLC jar")
class TinyTLC(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="orbital-tlc-runner-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.models = self.root / "models"
        self.models.mkdir()
        (self.models / "Tiny.tla").write_text(MODEL)
        self.config = self.models / "Tiny.cfg"

    def execute(self, config="SPECIFICATION Spec\nINVARIANT Safe\n", *extra):
        self.config.write_text(config)
        args = check.parser().parse_args(["--module", str(self.models / "Tiny.tla"), "--output", str(self.root / "runs"), "--timeout", "10", *extra])
        return check.run(args)

    def test_complete_capture_and_metrics(self):
        destination, result = self.execute()
        self.assertEqual(result["status"], "complete", (destination / "tlc.log").read_text())
        self.assertEqual(result["states"]["distinct"], 2)
        self.assertEqual(result["states"]["queue"], 0)
        self.assertGreater(result["peak_rss_bytes"], 0)
        self.assertEqual(result["jar"]["sha256"], check.JAR_SHA256)
        self.assertEqual((destination / "tool-source.json").read_bytes(), check.TOOL_SOURCE_BYTES)
        self.assertEqual(result["workers"], 1)
        self.assertEqual(result["heap"], "512m")
        self.assertNotIn("-deadlock", result["argv"])
        self.assertEqual((destination / "sources/Tiny.tla").read_text(), MODEL)
        self.assertEqual(result["source_sha256"]["Tiny.tla"], check.sha(MODEL.encode()))
        self.assertEqual(json.loads((destination / "result.json").read_text())["status"], "complete")
        second, _ = self.execute()
        self.assertNotEqual(destination, second)

    def test_invariant_and_distinct_reachability_label(self):
        cfg = "SPECIFICATION Spec\nINVARIANT NeverOne\n"
        destination, result = self.execute(cfg, "--expect", "invariant:NeverOne")
        self.assertEqual(result["status"], "expected_violation", (destination / "tlc.log").read_text())
        _, result = self.execute(cfg, "--expect", "invariant:NeverOne", "--witness", "x reaches one")
        self.assertEqual(result["status"], "witnessed")
        self.assertEqual(result["purpose"], "reachability")
        _, result = self.execute(cfg, "--expect", "invariant:Different")
        self.assertEqual(result["status"], "unexpected_violation")

    def test_initial_violation(self):
        destination, result = self.execute("SPECIFICATION Spec\nINVARIANT InitiallyFalse\n", "--expect", "invariant:InitiallyFalse")
        self.assertEqual(result["status"], "expected_violation", (destination / "tlc.log").read_text())

    def test_temporal_negative_and_name_binding(self):
        cfg = "SPECIFICATION Spec\nPROPERTY EventuallyOne\n"
        destination, result = self.execute(cfg, "--expect", "temporal:EventuallyOne")
        self.assertEqual(result["status"], "expected_violation", (destination / "tlc.log").read_text())
        with self.assertRaisesRegex(ValueError, "exactly"):
            self.execute(cfg, "--expect", "temporal:Different")
        with self.assertRaisesRegex(ValueError, "exactly"):
            self.execute(cfg + "PROPERTIES Safe\n", "--expect", "temporal:EventuallyOne")

    def test_deadlock_not_success(self):
        (self.models / "Tiny.tla").write_text(MODEL.replace("ELSE UNCHANGED x", "ELSE FALSE"))
        destination, result = self.execute()
        self.assertEqual(result["status"], "unexpected_violation", (destination / "tlc.log").read_text())
        self.assertEqual(result["returncode"], 11)
        with self.assertRaisesRegex(ValueError, "deadlock checking"):
            self.execute("SPECIFICATION Spec\nCHECK_DEADLOCK FALSE\n")

    def test_missing_control_and_parse_error(self):
        _, result = self.execute("SPECIFICATION Spec\nINVARIANT Safe\n", "--expect", "invariant:Safe")
        self.assertEqual(result["status"], "missing_expected_violation")
        (self.models / "Tiny.tla").write_text("not a TLA module\n")
        _, result = self.execute("SPECIFICATION Spec\nINVARIANT Safe\n", "--expect", "invariant:Safe")
        self.assertEqual(result["status"], "tool_error")

    def test_timeout_is_incomplete(self):
        destination, result = self.execute("SPECIFICATION Spec\nINVARIANT Safe\n", "--timeout", "0.001")
        self.assertEqual(result["status"], "incomplete_timeout")
        self.assertTrue((destination / "tlc.log").is_file())
        self.assertGreater(result["peak_rss_bytes"], 0)

    def test_subdirectory_config_is_captured_and_used(self):
        (self.models / "configs").mkdir()
        self.config = self.models / "configs/Tiny.cfg"
        destination, result = self.execute()
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["configuration"], "configs/Tiny.cfg")
        self.assertIn("configs/Tiny.cfg", result["source_sha256"])
        self.assertTrue((destination / "sources/configs/Tiny.cfg").is_file())

    def test_shared_source_cache_and_explicit_jar_verification(self):
        cache = self.root / "shared-cache"
        cached_jar = cache / "downloads" / check.JAR_SHA256
        cached_jar.parent.mkdir(parents=True)
        cached_jar.write_bytes(check.DEFAULT_JAR.read_bytes())
        with patch.dict(os.environ, {"SIXDB_DATA_CACHE": str(cache)}), patch.object(check, "DEFAULT_JAR", self.root / "absent.jar"):
            self.assertEqual(check.resolve_jar(None), cached_jar)
        bad_jar = self.root / "wrong.jar"
        bad_jar.write_bytes(b"wrong tool bytes")
        with self.assertRaisesRegex(ValueError, "source.json"):
            self.execute("SPECIFICATION Spec\nINVARIANT Safe\n", "--jar", str(bad_jar))


if __name__ == "__main__":
    unittest.main()
