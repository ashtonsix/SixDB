"""Check logical expectations and, optionally, the real Ikea hash handoff."""

import argparse
import copy
import json
import shlex
import subprocess
import unittest

import workload as w

BINARY = None
COUNTERS = ("row_visits", "dictionary_values_examined", "hash_updates", "null_updates",
            "counter_bytes_peak", "parts_skipped", "block_bits_selected", "block_bits_excluded",
            "coarse_ranges_entered", "position_probes", "row_record_bytes_modeled",
            "groups_skipped", "groups_full", "groups_mixed")


def decode_output(output):
    lines = iter(output.splitlines())
    answers, counters = [], []
    for line in lines:
        header = shlex.split(line)
        if len(header) != 3 or header[0] != "RESULT":
            raise ValueError("missing result header")
        groups = []
        for _ in range(int(header[2])):
            has_key, key, count, summed = map(int, next(lines).split())
            if has_key not in (0, 1) or count <= 0:
                raise ValueError("invalid output group")
            groups.append({"key": key if has_key else None, "count": count, "sum": summed})
        work = next(lines).split()
        if len(work) != len(COUNTERS) + 1 or work[0] != "WORK2":
            raise ValueError("missing work counters")
        counters.append(dict(zip(COUNTERS, map(int, work[1:]))))
        answers.append({"query_id": header[1], "groups": groups})
    return answers, counters


def run(document, mode, text=None):
    process = subprocess.run([BINARY, mode], input=w.transport(document) if text is None else text,
                             capture_output=True, text=True, timeout=30, check=True)
    return decode_output(process.stdout)


def compare(document):
    expected, counters = w.oracle(document), {}
    for mode in ("rowwise", "by-code"):
        actual, counters[mode] = run(document, mode)
        if actual != expected:
            raise AssertionError(f"{mode} hash output disagrees with the logical oracle")
    return counters


def coarse_example():
    document = {"format": w.FORMAT, "kind": "authored-synthetic", "snapshot": "basis-0",
                "parts": [{"part_id": "p", "dictionary_edition": "p-d0", "position_limit": 19,
                           "dictionary": [10, 20], "rows": [
                               {"position": p, "category": None if p == 18 else (20 if p % 2 else 10),
                                "x": None if p == 7 else 2 * p}
                               for p in range(19) if p not in (5, 13)]}], "queries": []}
    for name, blocks in (("all", None), ("none", []), ("mixed-tail", [1, 2, 3, 6]),
                         ("clustered", [0, 1]), ("scattered", [0, 2])):
        document["queries"].append(w.query(name, {"p": None}, coarse={"p": {
            "positions_per_block": 3, "blocks_per_group": 2, "blocks": blocks}}))
    document["queries"].append(w.query("intersect", {"p": [0, 3, 4, 11, 18]}, coarse={"p": {
        "positions_per_block": 3, "blocks_per_group": 2, "blocks": [6, 3, 1, 2]}}))
    return document


class LogicalChecks(unittest.TestCase):
    def test_coarse_blocks_and_fine_selection_have_independent_coordinates(self):
        document = coarse_example()
        answers = w.oracle(document)
        self.assertEqual(answers[1]["groups"], [])
        self.assertEqual(answers[2]["groups"], [
            {"key": None, "count": 1, "sum": 36}, {"key": 10, "count": 4, "sum": 56},
            {"key": 20, "count": 3, "sum": 46}])
        self.assertEqual(answers[-1]["groups"], [
            {"key": None, "count": 1, "sum": 36}, {"key": 10, "count": 1, "sum": 8},
            {"key": 20, "count": 2, "sum": 28}])
        # An expanded fine mask is an independent correctness control, never the execution path.
        expanded = copy.deepcopy(document)
        for query in expanded["queries"]:
            coarse = query.pop("coarse_selection")["p"]
            positions = [p for p in range(19) if coarse["blocks"] is None or
                         p // coarse["positions_per_block"] in coarse["blocks"]]
            fine = query["selection"]["p"]
            query["selection"]["p"] = positions if fine is None else sorted(set(fine) & set(positions))
        self.assertEqual(answers, w.oracle(expanded))

    def test_invalid_coarse_mask_refuses(self):
        for replacement in ({"positions_per_block": 0, "blocks": []},
                            {"positions_per_block": 3, "blocks": [7]},
                            {"positions_per_block": 3, "blocks": [1, 1]},
                            {"positions_per_block": 3, "blocks_per_group": 0, "blocks": None}):
            document = coarse_example()
            document["queries"][0]["coarse_selection"]["p"] = replacement
            with self.assertRaises(ValueError):
                w.validate(document)

    def test_mask_variants_reuse_original_rows(self):
        document = w.example()
        masked = w.mask_variants(document, positions_per_block=127, blocks_per_group=5)
        self.assertIs(masked["parts"], document["parts"])
        self.assertNotIn("coarse_selection", document["queries"][0])
        answers = w.oracle(masked)
        self.assertEqual(answers[-5]["groups"], answers[0]["groups"])
        self.assertEqual(answers[-4]["groups"], [])
    def test_hand_calculated_groups(self):
        def group(key, count, summed):
            return {"key": key, "count": count, "sum": summed}
        expected = [
            [group(None, 2, 22), group(0, 1, 19), group(10, 2, 20), group(20, 1, 0),
             group(w.U64, 2, 14)],
            [group(10, 2, 20)],
            [group(0, 1, 19), group(20, 1, 0), group(w.U64, 2, 14)],
            [group(None, 2, 22), group(0, 4, 39), group(1, 2, 14)],
            [], [group(None, 2, 22)], []]
        self.assertEqual([a["groups"] for a in w.oracle(w.example())], expected)

    def test_dictionary_editions_do_not_define_logical_identity(self):
        old = w.example()
        recoded = copy.deepcopy(old)
        for part in recoded["parts"]:
            part["dictionary"].reverse()
            part["dictionary_edition"] += "-recoded"
        self.assertEqual(w.oracle(old), w.oracle(recoded))
        self.assertNotEqual(w.transport(old), w.transport(recoded))

    def test_old_logical_input_survives_new_snapshot(self):
        old = w.example()
        before = w.oracle(old)
        new = copy.deepcopy(old)
        new["snapshot"] = "basis-1"
        for query in new["queries"]:
            query["snapshot"] = new["snapshot"]
        new["parts"][0]["rows"][0]["x"] = None
        self.assertNotEqual(before, w.oracle(new))
        self.assertEqual(before, w.oracle(old))

    def test_stale_selection_and_invalid_identity_refuse(self):
        for change in (lambda d: d["queries"][0].update(snapshot="another-cut"),
                       lambda d: d["parts"][0]["rows"][0].update(position=65536),
                       lambda d: d["parts"][0]["rows"].append(d["parts"][0]["rows"][0]),
                       lambda d: d["parts"][0]["rows"][0].update(category=99),
                       lambda d: d["queries"][0]["selection"].update(unknown=[])):
            bad = w.example()
            change(bad)
            with self.assertRaises(ValueError):
                w.oracle(bad)

    def test_repeatable_json_integer_roundtrip(self):
        first, second = w.generate(713), w.generate(713)
        self.assertEqual(first, second)
        self.assertEqual(w.oracle(first), w.oracle(json.loads(json.dumps(first))))

    def test_corpus_provenance_is_retained_without_changing_semantics(self):
        document = w.example()
        expected = w.oracle(document)
        # A label-only validator case, not a claim that this fixture came from a corpus.
        document["kind"] = "corpus-derived"
        with self.assertRaises(ValueError):
            w.validate(document)
        document["source"] = {"reference": "validation-placeholder-not-a-dataset",
                              "synthetic_changes": ["all rows are the authored example"]}
        self.assertEqual(w.oracle(document), expected)
        self.assertEqual(json.loads(json.dumps(document))["source"], document["source"])


@unittest.skipUnless(BINARY, "supply --binary to check the compiled Ikea handoff")
class HashChecks(unittest.TestCase):
    def test_coarse_skip_work_and_full_mixed_groups(self):
        work = self.check_document(coarse_example())
        for mode in ("rowwise", "by-code"):
            all_rows, none, mixed, clustered, scattered, intersect = work[mode]
            for name in ("row_visits", "dictionary_values_examined", "hash_updates", "null_updates",
                         "counter_bytes_peak", "coarse_ranges_entered", "position_probes",
                         "row_record_bytes_modeled"):
                self.assertEqual(none[name], 0, (mode, name))
            self.assertEqual(none["groups_skipped"], 4)
            self.assertEqual(all_rows["row_visits"], 17)
            self.assertEqual(all_rows["groups_full"], 4)
            self.assertEqual(mixed["row_visits"], 9)
            self.assertEqual((mixed["groups_skipped"], mixed["groups_full"], mixed["groups_mixed"]),
                             (1, 2, 1))
            self.assertEqual(mixed["coarse_ranges_entered"], 3)
            self.assertEqual(clustered["coarse_ranges_entered"], 1)
            self.assertEqual(scattered["coarse_ranges_entered"], 2)
            self.assertEqual(intersect["row_visits"], 4)
        for grain, group in ((1, 1), (4, 3), (127, 5), (256, 16), (65536, 2)):
            self.check_document(w.mask_variants(w.example(), positions_per_block=grain,
                                                blocks_per_group=group))
    def check_document(self, document):
        return compare(document)

    def test_boundaries_and_recoded_editions(self):
        document = w.example()
        self.check_document(document)
        for part in document["parts"]:
            part["dictionary"].reverse()
            part["dictionary_edition"] += "-recoded"
        self.check_document(document)

    def test_generated_populations_and_work_explanations(self):
        for seed, categories in ((1, 1), (2, 2), (3, 12), (4, 257), (5, 2048)):
            with self.subTest(seed=seed, categories=categories):
                self.check_document(w.generate(seed, parts=3, rows=513, categories=categories))
        work = self.check_document(w.generate(7, parts=2, rows=1024, categories=8))
        self.assertLess(work["by-code"][0]["hash_updates"], work["rowwise"][0]["hash_updates"])

    def test_full_population_count_and_maximum_value(self):
        document = w.example()
        document["parts"] = [{"part_id": "full", "dictionary_edition": "full-d0",
                              "position_limit": 65536, "dictionary": [w.U64],
                              "rows": [{"position": p, "category": w.U64, "x": w.U32}
                                       for p in range(65536)]}]
        document["queries"] = [w.query("full", {"full": None})]
        expected = [{"query_id": "full", "groups": [
            {"key": w.U64, "count": 65536, "sum": 281474976645120}]}]
        self.assertEqual(w.oracle(document), expected)
        self.check_document(document)

    def test_empty_parts_and_null_only_dictionary(self):
        document = w.example()
        document["parts"] = [
            {"part_id": "empty", "dictionary_edition": "empty-d0", "position_limit": 0,
             "dictionary": [], "rows": []},
            {"part_id": "null", "dictionary_edition": "null-d0", "position_limit": 1,
             "dictionary": [], "rows": [{"position": 0, "category": None, "x": 0}]}]
        document["queries"] = [w.query("all", {"empty": None, "null": None})]
        self.check_document(document)

    def test_wrong_code_to_dictionary_mapping_is_detected(self):
        document = w.example()
        text = w.transport(document)
        # Preserve positions and numeric totals, but swap which logical groups own them.
        corrupted = text.replace(f"10 {w.U64} 0 20\n", f"{w.U64} 10 0 20\n", 1)
        self.assertNotEqual(text, corrupted)
        for mode in ("rowwise", "by-code"):
            actual, _ = run(document, mode, corrupted)
            self.assertNotEqual(actual, w.oracle(document))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary")
    parser.add_argument("--input", nargs="+", help="check existing logical JSON documents")
    args = parser.parse_args()
    BINARY = args.binary
    if args.input:
        if not BINARY:
            parser.error("--input requires --binary")
        for path in args.input:
            with open(path) as source:
                document = json.load(source)
            compare(document)
            print(f"{path}: {len(document['queries'])} queries agree through both hash programs")
    else:
        # Decorators run before argument parsing; update the suite's skip state here.
        HashChecks.__unittest_skip__ = not bool(BINARY)
        unittest.main(argv=[__file__], verbosity=2)
