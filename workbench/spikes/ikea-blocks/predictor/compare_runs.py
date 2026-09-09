#!/usr/bin/env python3
"""Compare the user's corrected one-run feature without retuning frozen models.

The original expression omitted a tilde. The intended feature is
pop(bits & ~(bits << 1)), exactly population minus adjacent-11 count.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

import refine
import train


def one_run_rows(rows):
    result = rows.copy()
    # Reuse the old structure-feature column in this comparison only. Neither
    # the source sample nor the meaning of the frozen models is changed.
    result[:, 1] = rows[:, 6] - rows[:, 1]
    return result


def check_formula():
    cases = train.oracle_cases()
    rows = one_run_rows(train.describe(np.frombuffer(b"".join(cases), dtype=np.uint8).reshape(-1, 32)))
    for i, blob in enumerate(cases):
        value = int.from_bytes(blob, "little")
        runs = (value & ~(value << 1)).bit_count()
        assert rows[i, 1] == runs
        first, last = value & 1, value >> 255
        assert rows[i, 3] == 2 * runs - first - last
        complement_runs = ((~value & ((1 << 256) - 1)) & ~((~value & ((1 << 256) - 1)) << 1)).bit_count()
        assert runs - complement_runs == first + last - 1
    return {"cases": len(cases), "direct_formula_matches_population_minus_adjacent11": True,
            "transition_boundary_identity": True, "complement_run_count_difference_at_most_one": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample", type=Path, required=True)
    parser.add_argument("--frozen-models", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    checks = check_formula()
    data = np.load(args.sample)
    train_mask = data["partitions"] == "train"
    real, weights = data["rows"][train_mask], data["weights"][train_mask]
    development, dev_labels = refine.structure_cases(0xBECDE7, 24, holdout=False)
    old_holdout, old_labels = refine.structure_cases(0xBEC901D, 37, holdout=True)
    frozen = json.loads(args.frozen_models.read_text())
    # The feature is the sole change. No search of bins, thresholds, weights or
    # quantization follows the results. Natural-only/no-gate is a historical
    # comparator; augmentation/zero-gate matches the frozen structure models.
    candidates = []
    for augmented, gate in ((False, None), (True, 0)):
        fitting_rows = real if not augmented else np.concatenate((real, development))
        fitting_weights = weights if not augmented else np.concatenate((weights, np.full(len(development), weights.sum() * .01 / len(development))))
        for quarters in (False, True):
            name = "one_runs_quarters" if quarters else "one_runs"
            name += "_zero_augmented" if augmented else "_natural"
            columns = [0, 1, 2, 5] if quarters else [0, 1, 2]
            model = refine.fit(one_run_rows(fitting_rows), fitting_weights, name, columns, gate)
            model["features"] = ["distance", "one_runs", "enum_bits"] + (["quarter_dispersion"] if quarters else [])
            model["development_augmentation_weight_ratio"] = .01 if augmented else 0
            candidates.append(model)
    metrics = []
    for model in frozen + candidates:
        corrected = model in candidates
        for name, mask in (("real_validation", data["partitions"] == "validation"),
                           ("real_test_comparison", data["partitions"] == "test")):
            rows = one_run_rows(data["rows"][mask]) if corrected else data["rows"][mask]
            metrics.extend(refine.evaluate(model, rows, data["weights"][mask], name, data["corpora"][mask]))
        for name, rows, labels in (("structure_development", development, dev_labels),
                                  ("previous_structure_holdout_comparison", old_holdout, old_labels)):
            if corrected:
                rows = one_run_rows(rows)
            metrics.extend(refine.evaluate(model, rows, np.ones(len(rows)), name, labels))
    train.write_csv(args.output / "one-run-comparison.csv", metrics)
    (args.output / "one-run-models.json").write_text(json.dumps(candidates, indent=2) + "\n")
    (args.output / "one-run-provenance.json").write_text(json.dumps({
        "script_sha256": train.digest(Path(__file__)), "sample_sha256": train.digest(args.sample),
        "frozen_models_sha256": train.digest(args.frozen_models), "formula_checks": checks,
        "correction": "The user intended pop(bits & ~(bits << 1)), not adjacent11",
        "fixed_comparison": "Same source samples/splits and previously chosen population pieces, enum-zero gate, Q12 arithmetic and augmentation; no tuning after comparison",
        "test_status": "Existing real and structure holdouts have already been inspected for the frozen models; these are comparison sets, not newly untouched test data",
        "frozen_models": "Loaded exactly from the earlier retained fit, never refitted here",
        "native_changes": "None; evaluating a feature correction before deciding whether another native candidate is useful",
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
