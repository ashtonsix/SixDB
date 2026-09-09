#!/usr/bin/env python3
"""Development study of structural failures; never fit the retained test split.

The first permutation counterexample motivates this study. Its development
cases and augmentation are explicitly separate from the fresh stress holdout.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

import numpy as np

import train


def structure_cases(seed: int, count: int, *, holdout: bool):
    rng = np.random.default_rng(seed)
    blobs, labels = [], []
    # Pair a contiguous arrangement with arbitrary permutations. Every row
    # remains a full, ordinary 256-position bitset, including dense complements.
    for byte in (1, 3, 15, 31, 127, 255):
        for occupied in range(1, 32):
            base = np.array([byte] * occupied + [0] * (32 - occupied), dtype=np.uint8)
            for i in range(count):
                if i < count // 4:
                    value = np.roll(base, int(rng.integers(0, 32)))
                else:
                    value = rng.permutation(base)
                blobs.extend((value.tobytes(), (value ^ 255).tobytes()))
                labels.extend(("byte_permutations", "byte_permutations"))
    if holdout:
        # Not present in the development permutation family: heterogeneous
        # byte populations and independently patterned 64-bit regions.
        for _ in range(4096):
            alphabet = rng.choice(np.array([0, 1, 3, 15, 31, 127, 255], dtype=np.uint8), 4, replace=False)
            value = rng.choice(alphabet, 32)
            blobs.append(value.tobytes()); labels.append("mixed_byte_alphabets")
        for _ in range(4096):
            parts = []
            for _ in range(4):
                byte = int(rng.choice([0, 1, 3, 15, 31, 127, 255]))
                occupancy = int(rng.integers(0, 9))
                parts.extend(rng.permutation([byte] * occupancy + [0] * (8 - occupancy)))
            blobs.append(bytes(parts)); labels.append("independent_quarters")
    bits = np.frombuffer(b"".join(blobs), dtype=np.uint8).reshape(-1, 32)
    return train.describe(bits), np.array(labels)


def pieces(rows, bins, low_enum):
    minority = np.minimum(rows[:, 6], 256 - rows[:, 6])
    bucket = np.searchsorted(bins, minority, side="left")
    if low_enum is not None:
        bucket[rows[:, 2] <= low_enum] = len(bins) + 1
    return bucket


def fit(rows, weights, name, columns, low_enum):
    bins = [8, 32, 96]
    partial = (rows[:, 6] > 0) & (rows[:, 6] < 256)
    bucket = pieces(rows, bins, low_enum)
    x = train.design(rows, columns)
    coefficients, counts, fallbacks = [], [], []
    for piece in range(4 + (low_enum is not None)):
        mask = partial & (bucket == piece)
        counts.append(int(mask.sum()))
        fallbacks.append(bool(mask.sum() < 32))
        if mask.sum() < 32:
            mask = partial & (pieces(rows, bins, None) == piece) if piece < 4 else partial
        root = np.sqrt(weights[mask] / weights[mask].mean())
        coefficients.append(np.linalg.lstsq(x[mask] * root[:, None], rows[mask, 7] * root, rcond=None)[0].tolist())
    return {"name": name, "columns": columns, "features": [train.FEATURES[c] for c in columns],
            "minority_bin_upper_inclusive": bins, "low_enum_upper_inclusive": low_enum,
            "coefficients_float": coefficients, "training_samples_per_piece": counts,
            "fallback_piece": fallbacks,
            "coefficients_q12": np.rint(np.array(coefficients) * 4096).astype(int).tolist()}


def predict(model, rows):
    bucket = pieces(rows, model["minority_bin_upper_inclusive"], model["low_enum_upper_inclusive"])
    x = train.design(rows, model["columns"]).astype(np.int64)
    coefficients = np.array(model["coefficients_q12"], dtype=np.int64)
    result = np.clip(((coefficients[bucket] * x).sum(axis=1) + 2048) // 4096, 0, 47)
    result[(rows[:, 6] == 0) | (rows[:, 6] == 256)] = 0
    return result


def evaluate(model, rows, weights, partition, labels):
    prediction = predict(model, rows)
    result = []
    slices = [("partial", (rows[:, 6] > 0) & (rows[:, 6] < 256))]
    slices += [(label, labels == label) for label in np.unique(labels)]
    for label, mask in slices:
        if mask.any():
            result.append({"model": model["name"], "partition": partition, "slice": label,
                           **train.metrics(rows[mask, 7], prediction[mask], weights[mask])})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    data = np.load(args.sample)
    train_mask = data["partitions"] == "train"
    validation = data["partitions"] == "validation"
    real, weights = data["rows"][train_mask], data["weights"][train_mask]
    development, dev_labels = structure_cases(0xBECDE7, 24, holdout=False)
    holdout, holdout_labels = structure_cases(0xBEC901D, 37, holdout=True)
    models, metrics = [], []
    for augmentation in (0, .01):
        rows = real if augmentation == 0 else np.concatenate((real, development))
        fit_weights = weights if augmentation == 0 else np.concatenate((weights, np.full(len(development), weights.sum() * augmentation / len(development))))
        for name in ("distance_enum", "proposed_three", "symmetric_three", "quarter_four"):
            for gate in (None, 0, 16, 32):
                label = f"{name}_low{gate}_aug{augmentation:g}"
                model = fit(rows, fit_weights, label, train.SPECS[name], gate)
                model["development_augmentation_weight_ratio"] = augmentation
                models.append(model)
                metrics.extend(evaluate(model, data["rows"][validation], data["weights"][validation], "real_validation", data["corpora"][validation]))
                metrics.extend(evaluate(model, development, np.ones(len(development)), "structure_development", dev_labels))
                # This report is inspected only after candidates are fixed in
                # code. Refining from it would make it development evidence.
                metrics.extend(evaluate(model, holdout, np.ones(len(holdout)), "fresh_structure_holdout", holdout_labels))
    selected_names = ("distance_enum_lowNone_aug0", "symmetric_three_low0_aug0.01", "quarter_four_low0_aug0.01")
    selected = [next(m for m in models if m["name"] == name) for name in selected_names]
    # These candidates were frozen using validation/development before this
    # report was added. Test results do not choose coefficients or thresholds.
    test_mask = data["partitions"] == "test"
    for model in selected:
        metrics.extend(evaluate(model, data["rows"][test_mask], data["weights"][test_mask],
                                "real_test_after_candidate_freeze", data["corpora"][test_mask]))
    train.write_csv(args.output / "refinement.csv", metrics)
    train.write_csv(args.output / "refinement-summary.csv", [r for r in metrics if r["model"] in selected_names])
    (args.output / "refinement-selected.json").write_text(json.dumps(selected, indent=2) + "\n")
    cases = train.oracle_cases()
    descriptions = train.describe(np.frombuffer(b"".join(cases), dtype=np.uint8).reshape(-1, 32))
    predictions = [predict(model, descriptions) for model in selected]
    with (args.output / "structured-vectors.bin").open("wb") as stream:
        for index, (blob, row) in enumerate(zip(cases, descriptions)):
            packed = int(row[0]) | int(row[3]) << 8 | int(row[2]) << 16 | int(row[5]) << 24
            stream.write(blob + struct.pack("<QBBB", packed, *(int(prediction[index]) for prediction in predictions)))
    (args.output / "refinement-models.json").write_text(json.dumps(models, indent=2) + "\n")
    (args.output / "refinement-provenance.json").write_text(json.dumps({
        "sample_sha256": train.digest(args.sample), "script_sha256": train.digest(Path(__file__)),
        "development_seed": 0xBECDE7, "holdout_seed": 0xBEC901D,
        "development_cases": len(development), "holdout_cases": len(holdout),
        "augmentation": "0 or 1% of natural real training tile weight; only development structures",
        "model_changes": "Predeclared extra low-enum piece with threshold none/0/16/32; same four population pieces otherwise",
        "test_policy": "Retained real test split is never fitted; previously seen baseline test metrics are not selection input. Only the three frozen candidates are scored after selection; no further tuning followed these results.",
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
