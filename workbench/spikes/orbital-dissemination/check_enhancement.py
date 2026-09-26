#!/usr/bin/env python3
"""Exact selection, binding and network work-accounting checks."""

import argparse
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import random

from enhancement_study import (Binding, BindingError, decode, resolve_reference,
                               run_enhancement, selection, selection_reference,
                               source_hashes)


BINDING = Binding("table", "v1", "cut1", "predicate1", "rows0-4096", "mapping1")
ROWS = tuple(range(4096))


def rejects(action):
    try:
        action()
    except BindingError:
        return
    raise AssertionError("expected binding/coverage rejection")


def check_exact_encoding_choice_and_roundtrip():
    cases = [(frozenset((3, 400, 2020, 3500)), "row_ids"),
             (frozenset(random.Random(9).sample(ROWS, 409)), "bitmap"),
             (frozenset(range(100, 3376)), "spans"), (frozenset(), "row_ids")]
    for matches, encoding in cases:
        selected = selection(BINDING, matches, ROWS)
        assert selected.encoding == encoding
        assert decode(selected, BINDING, ROWS) == matches


def check_versions_cut_predicate_domain_and_physical_mapping():
    selected = selection(BINDING, random.Random(9).sample(ROWS, 409), ROWS)
    assert selected.encoding == "bitmap"
    for field in BINDING.__dict__:
        wrong = replace(BINDING, **{field: "different"})
        rejects(lambda wrong=wrong: decode(selected, wrong, ROWS))
    sparse = selection(BINDING, (3, 400, 2020, 3500), ROWS)
    remapped = replace(BINDING, mapping="mapping2")
    assert decode(sparse, remapped, ROWS[::-1]) == frozenset((3, 400, 2020, 3500))
    spans = selection(BINDING, range(100, 3376), ROWS)
    rejects(lambda: decode(spans, remapped, ROWS[::-1]))


def check_empty_requires_complete_coverage_message():
    empty = selection(BINDING, (), ROWS)
    assert empty.bytes == 112 and decode(empty, BINDING, ROWS) == frozenset()
    rejects(lambda: decode(replace(empty, complete=False), BINDING, ROWS))
    result = run_enhancement(dict(count=2, selectivity=0), "blocking")
    assert result["completed_recipients"] == 6
    assert result["counters"]["complete_empty_results"] == 6
    assert result["counters"]["selection_payload_bytes"] == 6 * 112


def check_transform_reference_includes_state_binding():
    selected = selection(BINDING, (3, 400, 2020, 3500), ROWS)
    reference = selection_reference(selected)
    assert resolve_reference(reference, {reference: selected}, BINDING, ROWS) == frozenset((3, 400, 2020, 3500))
    rejects(lambda: resolve_reference(reference, {}, BINDING, ROWS))
    wrong = replace(selected, binding=replace(BINDING, cut="old"))
    rejects(lambda: resolve_reference(reference, {reference: wrong}, BINDING, ROWS))
    assert selection_reference(wrong) != reference


def check_resident_inputs_save_compute_without_claiming_base_shipping():
    cfg = dict(count=3, residency="all", selectivity=.1)
    raw = run_enhancement(cfg, "raw")
    enhanced = run_enhancement(cfg, "blocking")
    assert raw["completed_recipients"] == enhanced["completed_recipients"] == 9
    assert raw["counters"]["useful_result_rows"] == enhanced["counters"]["useful_result_rows"]
    assert raw["counters"].get("raw_base_payload_bytes", 0) == 0
    assert enhanced["counters"].get("selected_row_payload_bytes", 0) == 0
    assert raw["counters"]["predicate_invocations"] == 9
    assert enhanced["counters"]["predicate_invocations"] == 3
    assert enhanced["counters"]["predicate_work_us"] < raw["counters"]["predicate_work_us"]


def check_nonresident_outputs_charge_selected_rows():
    cfg = dict(count=2, residency="none", selectivity=.001)
    raw = run_enhancement(cfg, "raw")
    enhanced = run_enhancement(cfg, "blocking")
    assert raw["completed_recipients"] == enhanced["completed_recipients"] == 6
    assert raw["counters"]["raw_base_payload_bytes"] == 6 * 4096 * 32
    assert enhanced["counters"]["selected_row_payload_bytes"] == 6 * 4 * 32
    assert enhanced["counters"]["useful_result_rows"] == raw["counters"]["useful_result_rows"]
    assert enhanced["counters"]["wire_bytes"] < raw["counters"]["wire_bytes"]


def check_optional_stale_sidecar_falls_back_without_bad_completion():
    cfg = dict(count=3, residency="all", selectivity=.1, stale_optional_sidecar=True)
    result = run_enhancement(cfg, "optional")
    assert result["completed_recipients"] == 9
    assert result["counters"]["rejected_selection_bindings"] == 3
    assert result["counters"]["predicate_invocations"] >= 9


def check_stale_residency_and_cold_relay_are_charged():
    cfg = dict(count=2, residency="all", selectivity=.001,
               stale_residents=["public"], relay_resident=False)
    raw = run_enhancement(cfg, "raw")
    enhanced = run_enhancement(cfg, "blocking")
    assert not raw["usable_resident"]["public"]
    assert raw["counters"]["raw_base_payload_bytes"] == 2 * 4096 * 32
    assert enhanced["counters"]["relay_input_payload_bytes"] == 2 * 4096 * 32
    assert enhanced["counters"]["selected_row_payload_bytes"] == 2 * 4 * 32
    assert enhanced["completed_recipients"] == 6


def check_edge_selective_is_explicit_static_placement():
    cfg = dict(count=2, residency="mixed", selectivity=.1)
    result = run_enhancement(cfg, "edge_selective")
    assert result["chosen_paths"] == {"local": "enhanced", "az": "enhanced", "public": "raw"}
    assert result["completed_recipients"] == 6
    assert result["counters"]["predicate_invocations"] == 4


def check_unreceived_selection_does_not_mean_empty():
    cfg = dict(count=1, selectivity=0, max_retries=0,
               faults=[dict(kind="partition", src="relay", dst="public", at_us=0)])
    result = run_enhancement(cfg, "blocking")
    assert result["completed_recipients"] == 2
    assert result["completed"] == 0 and result["unfinished"] == 1
    assert result["by_recipient"]["public"]["completed"] == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    before = source_hashes()
    passed = []
    for name, check in sorted(globals().copy().items()):
        if name.startswith("check_") and callable(check):
            check()
            passed.append(name.removeprefix("check_"))
    after = source_hashes()
    if before != after:
        raise RuntimeError("source changed during checks")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        source = Path(__file__).resolve()
        before[source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
        args.output.write_text(json.dumps(dict(checks=passed, source_sha256=before), indent=2) + "\n")
    print(f"{len(passed)} enhancement checks passed")


if __name__ == "__main__":
    main()
