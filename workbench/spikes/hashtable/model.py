#!/usr/bin/env python3
"""Deterministic hash/placement diagnostics. Counts and byte models, never timings."""
import argparse
import collections
import hashlib
import json
import pathlib
import random


def mask(bits):
    return (1 << bits) - 1


def undo_xor_right(x, shift, bits):
    result = x
    for s in range(shift, bits, shift):
        result ^= x >> s
    return result & mask(bits)


class Permutation:
    def __init__(self, bits, seed, mixed=False):
        self.bits, self.mask, self.mixed = bits, mask(bits), mixed
        rng = random.Random(seed)
        self.seed = rng.getrandbits(bits)
        self.m = rng.getrandbits(bits) | 1
        self.n = rng.getrandbits(bits) | 1
        self.mi, self.ni = pow(self.m, -1, 1 << bits), pow(self.n, -1, 1 << bits)

    def forward(self, x):
        x = ((x ^ self.seed) * self.m) & self.mask
        if self.mixed:
            x ^= x >> (self.bits // 2)
            x = (x * self.n) & self.mask
            x ^= x >> (self.bits // 2)
        return x

    def inverse(self, x):
        if self.mixed:
            x = undo_xor_right(x, self.bits // 2, self.bits)
            x = (x * self.ni) & self.mask
            x = undo_xor_right(x, self.bits // 2, self.bits)
        return ((x * self.mi) & self.mask) ^ self.seed


def split(x, bits, a):
    assert 0 <= a <= bits
    b = min(bits - a, 8)
    c = bits - a - b
    return x >> (bits - a), (x >> c) & mask(b), x & mask(c)


def reconstruct(parts, bits, a):
    b = min(bits - a, 8)
    c = bits - a - b
    bucket, fp, suffix = parts
    return (bucket << (b + c)) | (fp << c) | suffix


def checks():
    checked = 0
    for bits in range(8, 65, 8):
        rng = random.Random(0x48415348 + bits)
        keys = list(range(1 << bits)) if bits <= 16 else [
            0, mask(bits), 1 << (bits - 1), *[rng.getrandbits(bits) for _ in range(2048)]]
        for mixed in (False, True):
            p = Permutation(bits, bits * 1337, mixed)
            for key in keys:
                x = p.forward(key)
                assert p.inverse(x) == key
                # Exercise every split on small exhaustive domain; all boundaries
                # on wider random/boundary keys, not only byte-rounded suffixes.
                for a in range(bits + 1):
                    assert reconstruct(split(x, bits, a), bits, a) == x
                checked += 1
    return {"roundtrips": checked, "all_splits": True,
            "small_domains_exhaustive": [8, 16]}


def pair_diagnostic(trials):
    rng = random.Random(0x48415348)
    result = []
    for a in (2, 3, 4):
        counts = collections.Counter()
        for _ in range(trials):
            seed, multiplier = rng.getrandbits(64), rng.getrandbits(64) | 1
            for label, m in (("fixed_golden", 0x9e3779b97f4a7c15),
                             ("random_odd", multiplier)):
                x = ((seed * m) & mask(64)) >> (64 - a)
                y = (((seed ^ (1 << 55)) * m) & mask(64)) >> (64 - a)
                counts[label] += x == y
        result.append({"bucket_bits": a, "trials": trials,
                       "ideal_independent_pair_rate": 1 / (1 << a),
                       **{k: v / trials for k, v in counts.items()}})
    return result


def placement(bits, a, secondary_a, slots, size, seed, distribution, mixed):
    p, q = Permutation(bits, seed, mixed), Permutation(bits, seed ^ 0x736563, mixed)
    rng = random.Random(seed)
    if distribution == "uniform":
        keys = rng.sample(range(1 << bits), size)
    elif distribution == "sequential":
        keys = range(size)
    elif distribution == "stride":
        step = 1 << max(0, bits - (size - 1).bit_length())
        keys = range(0, size * step, step)
    else:
        raise ValueError(distribution)
    primary = [0] * (1 << a)
    secondary = [0] * (1 << secondary_a)
    conditional = [0] * len(secondary)
    chains = collections.Counter()
    for key in keys:
        first = p.forward(key) >> (bits - a)
        if primary[first] < slots:
            primary[first] += 1
            continue
        second = q.forward(key) >> (bits - secondary_a)
        conditional[second] += 1
        if secondary[second] < slots:
            secondary[second] += 1
        else:
            chains[second] += 1
    return {"bits": bits, "primary_bits": a, "secondary_bits": secondary_a,
            "slots": slots, "size": size, "seed": seed, "distribution": distribution,
            "permutation": "xor_multiply_mix" if mixed else "xor_random_odd",
            "primary_keys": sum(primary), "secondary_keys": sum(secondary),
            "chain_keys": sum(chains.values()), "longest_chain": max(chains.values(), default=0),
            "max_conditional_secondary": max(conditional, default=0),
            "secondary_nonempty": sum(bool(x) for x in conditional),
            "array_slot_capacity": slots * (len(primary) + len(secondary))}


def geometry():
    rows = []
    # This first census models rounded-byte TuplePack entries. Tight microtiles
    # need actual plans/read footprints; these counts do not pretend to model them.
    for bits in range(8, 65, 8):
        for a in sorted({0, min(bits, 8), min(bits, 12), min(bits, 20), bits}):
            b, c = min(bits - a, 8), max(0, bits - a - 8)
            for slots in (8, 14, 16, 32):
                for v in (0, 16, 32, 64):
                    fp = ((slots + 7) // 8) * b
                    entries = slots * ((c + 7) // 8 + (v + 7) // 8)
                    # Illustrative exact count32 + occupancy8; no filter/link ID.
                    useful = 5 + fp + entries
                    for alignment in (1, 32, 64):
                        stride = ((useful + alignment - 1) // alignment) * alignment
                        phases = {i * stride % 64 for i in range(64)}
                        rows.append({"bits": bits, "a": a, "b": b, "c": c,
                                     "slots": slots, "value_bits": v, "header_bytes": 5,
                                     "fingerprint_bytes": fp, "entry_bytes": entries,
                                     "stride": stride, "alignment_rounding": alignment,
                                     "line_union_max": max((phase + useful + 63) // 64 for phase in phases),
                                     "full_stride_line_union_max": max((phase + stride + 63) // 64 for phase in phases)})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--trials", type=int, default=100000)
    args = parser.parse_args()
    if args.trials <= 0:
        parser.error("trials must be positive")
    placements = []
    for mixed in (False, True):
        for slots in (8, 16, 32):
            for pct in (50, 80, 95, 110):
                for distribution in ("uniform", "sequential", "stride"):
                    for seed in (29, 731):
                        placements.append(placement(32, 10, 6, slots,
                            1024 * slots * pct // 100, seed, distribution, mixed))
    output = {"source_sha256": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
              "scope": "deterministic logical model; no timing, hardware, cryptographic or table-memory claim",
              "checks": checks(), "fixed_pair": pair_diagnostic(args.trials),
              "placement": placements, "geometry": geometry()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps({"output": str(args.output), "checks": output["checks"],
                      "fixed_pair": output["fixed_pair"], "placement_cases": len(placements),
                      "geometry_cases": len(output["geometry"])}))


if __name__ == "__main__":
    main()
