#!/usr/bin/env python3
"""Fit BEC256 byte-size estimates. This script does not encode a BEC body.

Input readers preserve posting-list groups; no tile is assigned its own split.
Run with --help. Outputs belong under ignored build/ or SIXDB_RESULTS.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import platform
import struct
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

SEED = 0xBEC256
PC = np.array([i.bit_count() for i in range(256)], dtype=np.int16)
ENUM_WIDTH = np.array([(math.comb(8, i) - 1).bit_length() for i in range(9)], dtype=np.int16)
WIDTH = np.array([i.bit_length() for i in range(129)], dtype=np.int16)
FEATURES = ("distance", "adjacent11", "enum_bits", "transitions", "half_imbalance", "quarter_dispersion")
SAMPLE_EDGES = np.array([0, 1, 4, 8, 16, 32, 64, 96, 127, 128, 129, 160, 192, 224, 240, 248, 252, 255, 256])
SPECS = {
    "distance": [0],
    "distance_enum": [0, 2],
    "proposed_three": [0, 1, 2],  # Historical missing-tilde expression: adjacent11, not the corrected one-run feature.
    "symmetric_three": [0, 3, 2],
    "half_four": [0, 3, 2, 4],
    "quarter_four": [0, 3, 2, 5],
}


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1 << 20), b""):
            h.update(data)
    return h.hexdigest()


def describe(bits: np.ndarray) -> np.ndarray:
    """[six candidate features, population, exact coded bytes, exact bits]."""
    b = bits.astype(np.int16)
    byte_pop = PC[b]
    pop = byte_pop.sum(axis=1, dtype=np.int32)
    enum = ENUM_WIDTH[byte_pop].sum(axis=1, dtype=np.int32)
    adjacent = PC[b & ((b << 1) & 255)].sum(axis=1, dtype=np.int32)
    adjacent += ((b[:, 1:] & 1) * (b[:, :-1] >> 7)).sum(axis=1, dtype=np.int32)
    transition = 2 * (pop - adjacent) - (b[:, 0] & 1) - (b[:, -1] >> 7)
    halves = byte_pop.reshape(-1, 2, 16).sum(axis=2, dtype=np.int32)
    quarters = byte_pop.reshape(-1, 4, 8).sum(axis=2, dtype=np.int32)
    # Each internal node's alternatives number min(P, width-P)+1.
    # Taking bit_length(min(P, width-P)) gives ceil(log2(alternatives)).
    tree = np.zeros(len(bits), dtype=np.int32)
    for nbytes in (32, 16, 8, 4, 2):
        counts = byte_pop.reshape(-1, 32 // nbytes, nbytes).sum(axis=2, dtype=np.int32)
        tree += WIDTH[np.minimum(counts, 8 * nbytes - counts)].sum(axis=1, dtype=np.int32)
    exact_bits = tree + enum
    return np.column_stack((abs(128 - pop), adjacent, enum, transition,
                            abs(halves[:, 0] - halves[:, 1]),
                            abs(4 * quarters - pop[:, None]).sum(axis=1),
                            pop, (exact_bits + 7) // 8, exact_bits)).astype(np.int32)


def scalar_description(blob: bytes) -> tuple[list[int], int, int, int]:
    """Independent integer/range oracle for the vectorized training labels."""
    value = int.from_bytes(blob, "little")
    pop = value.bit_count()
    enum = sum((math.comb(8, byte.bit_count()) - 1).bit_length() for byte in blob)
    cost = enum
    for width in (256, 128, 64, 32, 16):
        for start in range(0, 256, width):
            count = ((value >> start) & ((1 << width) - 1)).bit_count()
            cost += (min(count, width) - max(0, 2 * count - width)).bit_length()
    adjacent = (value & (value << 1)).bit_count()
    transitions = ((value ^ (value >> 1)) & ((1 << 255) - 1)).bit_count()
    halves = [(value >> start & ((1 << 128) - 1)).bit_count() for start in (0, 128)]
    quarters = [(value >> start & ((1 << 64) - 1)).bit_count() for start in (0, 64, 128, 192)]
    features = [abs(128 - pop), adjacent, enum, transitions, abs(halves[0] - halves[1]),
                sum(abs(4 * q - pop) for q in quarters)]
    return features, pop, (cost + 7) // 8, cost


def oracle_cases() -> list[bytes]:
    rng = np.random.default_rng(SEED ^ 0x0AC1E)
    cases = [bytes(32), bytes([255]) * 32]
    cases.extend(bytes([i]) * 32 for i in range(256))
    cases.extend((1 << i).to_bytes(32, "little") for i in range(256))
    random_bytes = rng.integers(0, 256, (4096, 32), dtype=np.uint8).tobytes()
    cases.extend(random_bytes[i:i + 32] for i in range(0, len(random_bytes), 32))
    # Piece boundaries and neighbours, including asymmetric and complemented
    # clustered arrangements, exercise selection and coefficient rounding.
    for pop in (1, 7, 8, 9, 31, 32, 33, 95, 96, 97, 127, 128):
        for _ in range(32):
            value = sum(1 << int(i) for i in rng.choice(256, pop, replace=False))
            cases.extend((value.to_bytes(32, "little"), (value ^ ((1 << 256) - 1)).to_bytes(32, "little")))
    for pop in range(257):
        value = (1 << pop) - 1
        cases.extend((value.to_bytes(32, "little"), (value ^ ((1 << 256) - 1)).to_bytes(32, "little")))
    for full_bytes in range(33):
        base = np.array([255] * full_bytes + [0] * (32 - full_bytes), dtype=np.uint8)
        cases.extend(rng.permutation(base).tobytes() for _ in range(8))
    return cases


def oracle_check() -> dict:
    cases = oracle_cases()
    arr = np.frombuffer(b"".join(cases), dtype=np.uint8).reshape(-1, 32)
    got = describe(arr)
    for index, blob in enumerate(cases):
        features, pop, size, bits = scalar_description(blob)
        assert got[index].tolist() == features + [pop, size, bits], (index, got[index])
    comp = describe(arr ^ 255)
    assert np.array_equal(got[:, [0, 2, 3, 4, 5, 7, 8]], comp[:, [0, 2, 3, 4, 5, 7, 8]])
    assert got[2 + 15, 8] == 374
    assert got[:2, 7].tolist() == [0, 0]
    return {"cases": len(cases), "complement_symmetry": True, "maximum_repeated_0f_bits": 374}


def file_entries(msmarco: Path | None, roaring: Path | None) -> list[dict]:
    entries = []
    for directory, is_marco in ((msmarco, True), (roaring, False)):
        if directory is None:
            continue
        meta = json.loads((directory / "prepared.json").read_text())
        if meta["details"].get("min_cardinality") != 1:
            raise ValueError("Use the complete min_cardinality=1 prepared corpus; do not discard sparse windows")
        for index in sorted(directory.glob("*.windows.jsonl")):
            name = index.name.removesuffix(".windows.jsonl")
            corpus = "msmarco" if is_marco else name
            family = "msmarco" if is_marco else "dimension" if name.startswith("dimension_") else name.removesuffix("_srt")
            groups = defaultdict(list)
            with index.open() as stream:
                for line in stream:
                    record = json.loads(line)
                    groups[record["term"] if is_marco else record["bitmap_id"]].append(record)
            for group, windows in sorted(groups.items()):
                entries.append({"corpus": corpus, "family": family, "group": group,
                                "path": directory / (name + ".kw16"), "index": index, "windows": windows})
    if not entries:
        raise ValueError("No prepared .windows.jsonl indexes found")
    return entries


def group_splits(entries: list[dict]) -> dict[tuple[str, str], str]:
    # The original/row-sorted collections lack a proven original-column join.
    # Keeping entire related source families together is stronger than a
    # parent-list split and prevents unknown sibling identities from leaking.
    families = sorted({e["family"] for e in entries},
                      key=lambda family: hashlib.sha256(f"{SEED}:{family}".encode()).digest())
    if len(families) < 3:
        raise ValueError("Need at least three source families for the conservative family-group split")
    nval = max(1, len(families) // 5)
    ntest = max(1, len(families) // 5)
    by_family = {family: "train" if i < len(families) - nval - ntest else "validation" if i < len(families) - ntest else "test"
                 for i, family in enumerate(families)}
    result = {}
    for entry in entries:
        result[entry["family"], entry["group"]] = by_family[entry["family"]]
    return result


def tile_batches(entry: dict):
    """All 256 tiles in each occupied 65536-position window, in bounded batches."""
    windows = entry["windows"]
    data = np.memmap(entry["path"], dtype=np.uint8, mode="r")
    for first in range(0, len(windows), 32):
        last = min(first + 32, len(windows))
        bits = np.zeros((last - first, 8192), dtype=np.uint8)
        for row, window in enumerate(windows[first:last]):
            offset, card = window["offset"], window["cardinality"]
            stored_card = int.from_bytes(data[offset:offset + 4], "little")
            if card != stored_card or offset + 4 + 2 * card > len(data):
                raise ValueError(f"Window index disagrees with prepared bytes: {entry['path']}")
            local = np.frombuffer(data, dtype="<u2", count=card, offset=offset + 4)
            np.bitwise_or.at(bits[row], local >> 3, (1 << (local & 7)).astype(np.uint8))
        yield bits.reshape(-1, 32)


class Sampler:
    """Uniform priority reservoir per corpus/split/population stratum.

    Every stratum gets a budget. Inverse inclusion weights recover the observed
    window-tile mixture; dense tails are retained even when rare.
    """
    def __init__(self, cap: int):
        self.cap = cap
        self.rng = np.random.default_rng(SEED)
        self.rows = {}
        self.priorities = {}
        self.counts = defaultdict(int)

    def add(self, corpus: str, partition: str, rows: np.ndarray):
        strata = np.searchsorted(SAMPLE_EDGES, rows[:, 6], side="right") - 1
        for stratum in np.unique(strata):
            key = corpus, partition, int(stratum)
            part = rows[strata == stratum]
            self.counts[key] += len(part)
            rank = self.rng.random(len(part))
            if key in self.rows:
                part = np.concatenate((self.rows[key], part))
                rank = np.concatenate((self.priorities[key], rank))
            if len(part) > self.cap:
                keep = np.argpartition(rank, self.cap - 1)[:self.cap]
                part, rank = part[keep], rank[keep]
            self.rows[key], self.priorities[key] = part, rank

    def finish(self):
        rows, weights, corpora, partitions = [], [], [], []
        summary = []
        for key in sorted(self.rows):
            part = self.rows[key]
            rows.append(part)
            weights.extend([self.counts[key] / len(part)] * len(part))
            corpora.extend([key[0]] * len(part))
            partitions.extend([key[1]] * len(part))
            summary.append({"corpus": key[0], "partition": key[1], "population_stratum_min": int(SAMPLE_EDGES[key[2]]),
                            "population_tiles": self.counts[key], "sample_tiles": len(part)})
        return np.concatenate(rows), np.array(weights), np.array(corpora), np.array(partitions), summary


def synthetic() -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(SEED ^ 0x57E55)
    blobs, labels = [], []
    for pop in range(257):
        for _ in range(12):
            places = rng.choice(256, pop, replace=False)
            value = sum(1 << int(i) for i in places)
            blobs.append(value.to_bytes(32, "little")); labels.append("fixed_population")
        for offset in (0, 1, 7, 31, 63, 127, 191, 255):
            value = sum(1 << ((offset + i) % 256) for i in range(pop))
            blobs.append(value.to_bytes(32, "little")); labels.append("single_run_rotated")
    for byte in range(256):
        blobs.append(bytes([byte]) * 32); labels.append("repeated_byte")
    for probability in (0.001, 0.01, 0.1, 0.5, 0.9, 0.99, 0.999):
        for run in (2, 4, 16, 64):
            for _ in range(96):
                on = rng.random() < probability
                value = 0
                leave = 1 / run
                enter = min(1., probability * leave / (1 - probability))
                for i in range(256):
                    value |= int(on) << i
                    on = rng.random() >= leave if on else rng.random() < enter
                blobs.append(value.to_bytes(32, "little")); labels.append("markov_runs")
    return describe(np.frombuffer(b"".join(blobs), dtype=np.uint8).reshape(-1, 32)), np.array(labels)


def design(rows: np.ndarray, columns: list[int]) -> np.ndarray:
    return np.column_stack((np.ones(len(rows)), rows[:, columns])).astype(np.float64)


def fit(rows: np.ndarray, weights: np.ndarray, name: str, columns: list[int], bins: list[int]) -> dict:
    minority = np.minimum(rows[:, 6], 256 - rows[:, 6])
    partial = minority > 0
    x = design(rows, columns)
    root = np.sqrt(weights[partial] / weights[partial].mean())
    fallback = np.linalg.lstsq(x[partial] * root[:, None], rows[partial, 7] * root, rcond=None)[0]
    coefficients, counts = [], []
    bucket = np.searchsorted(bins, minority, side="left")
    for i in range(len(bins) + 1):
        mask = partial & (bucket == i)
        counts.append(int(mask.sum()))
        if mask.sum() < max(32, 4 * x.shape[1]):
            beta = fallback
        else:
            root = np.sqrt(weights[mask] / weights[mask].mean())
            beta = np.linalg.lstsq(x[mask] * root[:, None], rows[mask, 7] * root, rcond=None)[0]
        coefficients.append(beta.tolist())
    return {"name": name, "columns": columns, "features": [FEATURES[c] for c in columns],
            "minority_bin_upper_inclusive": bins, "coefficients_float": coefficients,
            "training_samples_per_piece": counts}


def predict(model: dict, rows: np.ndarray, fraction_bits: int | None) -> np.ndarray:
    minority = np.minimum(rows[:, 6], 256 - rows[:, 6])
    bucket = np.searchsorted(model["minority_bin_upper_inclusive"], minority, side="left")
    coefficients = np.array(model["coefficients_float"])
    x = design(rows, model["columns"])
    if fraction_bits is None or fraction_bits == -1:
        result = np.sum(coefficients[bucket] * x, axis=1)
        if fraction_bits == -1:
            result = np.floor(result + .5)
    else:
        # Independent exact integer simulation of the emitted header:
        # coefficients rounded ties-to-even once; evaluation rounds halves up.
        q = np.rint(coefficients * (1 << fraction_bits)).astype(np.int64)
        acc = (q[bucket] * x.astype(np.int64)).sum(axis=1)
        result = (acc + (1 << (fraction_bits - 1))) // (1 << fraction_bits)
    result = np.clip(result, 0, 47)
    result[minority == 0] = 0
    return result


def weighted_quantile(values: np.ndarray, weights: np.ndarray, q: float) -> float:
    order = np.argsort(values)
    total = np.cumsum(weights[order])
    index = min(int(np.searchsorted(total, q * total[-1], side="left")), len(order) - 1)
    return float(values[order[index]])


def metrics(actual: np.ndarray, prediction: np.ndarray, weights: np.ndarray) -> dict:
    error = prediction - actual
    absolute = abs(error)
    result = {"sample_tiles": len(actual), "weighted_tiles": float(weights.sum()),
              "mae_bytes": float(np.average(absolute, weights=weights)),
              "bias_bytes": float(np.average(error, weights=weights)),
              "rmse_bytes": float(np.sqrt(np.average(error * error, weights=weights))),
              "p50_abs_bytes": weighted_quantile(absolute, weights, .5),
              "p95_abs_bytes": weighted_quantile(absolute, weights, .95),
              "p99_abs_bytes": weighted_quantile(absolute, weights, .99),
              "max_abs_bytes": float(absolute.max())}
    for threshold in (8, 16, 24, 32):
        # Strictly smaller byte size is the compression decision.
        wanted, chosen = actual < threshold, prediction < threshold
        result[f"threshold_{threshold}_wrong_fraction"] = float(np.average(wanted != chosen, weights=weights))
        result[f"threshold_{threshold}_false_compress_fraction"] = float(np.average(~wanted & chosen, weights=weights))
        result[f"threshold_{threshold}_missed_compress_fraction"] = float(np.average(wanted & ~chosen, weights=weights))
    return result


def evaluate(model: dict, precision: int | None, rows: np.ndarray, weights: np.ndarray,
             corpora: np.ndarray, partition: str) -> list[dict]:
    prediction = predict(model, rows, precision)
    partial = (rows[:, 6] != 0) & (rows[:, 6] != 256)
    groups = [("all", np.ones(len(rows), dtype=bool)), ("partial", partial)]
    groups += [("corpus:" + c, corpora == c) for c in np.unique(corpora)]
    groups += [(f"pop:{lo}-{hi}", (rows[:, 6] >= lo) & (rows[:, 6] <= hi))
               for lo, hi in ((0, 0), (1, 8), (9, 32), (33, 96), (97, 159), (160, 223), (224, 248), (249, 255), (256, 256))]
    result = []
    for name, mask in groups:
        if mask.any():
            arithmetic = "float" if precision is None else "float_rounded" if precision == -1 else f"q{precision}"
            result.append({"model": model["name"], "arithmetic": arithmetic,
                           "partition": partition, "slice": name,
                           **metrics(rows[mask, 7], prediction[mask], weights[mask])})
    return result


def write_csv(path: Path, rows: list[dict]):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msmarco", type=Path, help="Prepared msmarco-keyset directory with min_cardinality=1")
    parser.add_argument("--roaring", type=Path, help="Prepared real-roaring directory with min_cardinality=1")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cap-per-stratum", type=int, default=2000)
    parser.add_argument("--oracle-only", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    oracle = oracle_check()
    if args.oracle_only:
        (args.output / "oracle.json").write_text(json.dumps(oracle, indent=2) + "\n")
        print(json.dumps(oracle)); return
    if args.cap_per_stratum < 1:
        parser.error("--cap-per-stratum must be positive")
    entries = file_entries(args.msmarco, args.roaring)
    splits = group_splits(entries)
    sampler = Sampler(args.cap_per_stratum)
    group_rows = []
    for index, e in enumerate(entries):
        partition = splits[e["family"], e["group"]]
        tile_count = 0
        for bits in tile_batches(e):
            sampler.add(e["corpus"], partition, describe(bits))
            tile_count += len(bits)
        group_rows.append({"corpus": e["corpus"], "family": e["family"], "group": e["group"],
                           "partition": partition, "postings": sum(w["cardinality"] for w in e["windows"]), "window_tiles": tile_count})
        if index % 100 == 0:
            print(f"{index}/{len(entries)} lists: {e['corpus']} {e['group']}", flush=True)
    rows, weights, corpora, partitions, sampling = sampler.finish()
    np.savez_compressed(args.output / "sample.npz", rows=rows, weights=weights, corpora=corpora, partitions=partitions)
    write_csv(args.output / "groups.csv", group_rows)
    write_csv(args.output / "sampling.csv", sampling)
    train = partitions == "train"
    models = []
    for base, cols in SPECS.items():
        for bins in ([], [8, 32, 96]):
            name = base + ("_piecewise" if bins else "")
            models.append(fit(rows[train], weights[train], name, cols, bins))
    results = []
    stress, labels = synthetic()
    for model in models:
        for precision in (None, -1, 8, 12, 16):
            for partition in ("validation", "test"):
                mask = partitions == partition
                results.extend(evaluate(model, precision, rows[mask], weights[mask], corpora[mask], partition))
            results.extend(evaluate(model, precision, stress, np.ones(len(stress)), labels, "synthetic_stress"))
    write_csv(args.output / "metrics.csv", results)
    summary = [r for r in results if r["arithmetic"] == "q12" and
               (r["slice"] == "partial" or r["partition"] == "test" and r["slice"].startswith("corpus:"))]
    write_csv(args.output / "summary.csv", summary)
    for model in models:
        for precision in (8, 12, 16):
            model[f"coefficients_q{precision}"] = np.rint(np.array(model["coefficients_float"]) * (1 << precision)).astype(int).tolist()
    (args.output / "models.json").write_text(json.dumps(models, indent=2) + "\n")
    selected = next(m for m in models if m["name"] == "distance_enum_piecewise")
    (args.output / "selected.json").write_text(json.dumps({
        "model_id": "bec256-distance-enum-q12-20260909", "model": selected,
        "fraction_bits": 12,
        "selection": "Two-feature candidate retained for the cost/accuracy probe; Q12 has lower partial-tile validation MAE than Q8/Q16 for this candidate. This is not a target-timing decision.",
        "packed_features": {"distance": [0, 7], "enum_bits": [16, 23]},
    }, indent=2) + "\n")
    cases = oracle_cases()
    descriptions = describe(np.frombuffer(b"".join(cases), dtype=np.uint8).reshape(-1, 32))
    predictions = predict(selected, descriptions, 12)
    with (args.output / "vectors.bin").open("wb") as stream:
        for blob, row, prediction in zip(cases, descriptions, predictions):
            stream.write(blob + struct.pack("<QB", int(row[0]) | int(row[2]) << 16, int(prediction)))
    provenance = {"seed": SEED, "python": sys.version, "numpy": np.__version__, "platform": platform.platform(),
                  "script_sha256": digest(Path(__file__)), "oracle": oracle,
                  "cap_per_corpus_split_population_stratum": args.cap_per_stratum,
                  "input_sha256": {str(p): digest(p) for p in sorted({e[k] for e in entries for k in ("path", "index")})},
                  "target": "exact headless BEC256 bytes, no raw escape and no stored root count",
                  "split": "60/20/20 by source family, integer rounded with at least one validation and one test family; all descendants of each parent list and all related sorted/projection variants remain together",
                  "training_weights": "inverse reservoir inclusion, natural tile mixture of occupied 65536 windows",
                  "terminal_policy": "popcount 0 and 256 predict exactly 0; excluded from fitting",
                  "fit": "weighted numpy.linalg.lstsq on exact bytes; bins chosen before fitting",
                  "fixed_point": "quantize coefficients ties-to-even; integer dot product; add half and floor; clamp [0,47]",
                  "synthetic": "holdout stress only; never fitted or used to select coefficients",
                  "limits": "sampled Calico corpora, observed nonempty 65536 windows; no general accuracy guarantee"}
    (args.output / "training-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(f"{len(rows)} sampled tiles; wrote {len(results)} metric rows to {args.output}", flush=True)


if __name__ == "__main__":
    main()
