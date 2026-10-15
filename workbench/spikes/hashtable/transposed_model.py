#!/usr/bin/env python3
"""Analytic sizing for the proposed split-control/transposed-payload table.

This is an independent-Poisson large-domain model, not a benchmark. It solves
for 70% occupied primary controls, includes assigned keys spilling beyond N,
and excludes churn, skew, allocator rounding and all auxiliary memory from
payload-capacity ratios. Dense finite domains need sampling without replacement.
"""
import json
import math


def distribution(slots, mean):
    masses = [math.exp(-mean)]
    for count in range(1, 512):
        masses.append(masses[-1] * mean / count)
    assert abs(sum(masses) - 1) < 1e-12
    primary = sum(min(count, slots) * p for count, p in enumerate(masses))
    return masses, primary


def estimate(slots, buckets_per_array, base_rows, occupied=0.7):
    lower, upper = 0.0, 2.0 * slots
    for _ in range(70):
        mean = (lower + upper) / 2
        if distribution(slots, mean)[1] < occupied * slots:
            lower = mean
        else:
            upper = mean
    mean = (lower + upper) / 2
    masses, primary = distribution(slots, mean)
    spill = sum(max(count - slots, 0) * p for count, p in enumerate(masses))
    assert abs(primary + spill - mean) < 1e-11
    cdf, height = 0.0, 0.0
    for count in range(slots):
        cdf += masses[count]
        height += 1 - cdf ** buckets_per_array
    base_live = sum(min(count, base_rows) * p for count, p in enumerate(masses))
    return {
        "slots": slots,
        "buckets_per_array": buckets_per_array,
        "base_rows": base_rows,
        "primary_occupancy": primary / slots,
        "assigned_keys_per_bucket": mean,
        "full_bucket_probability": sum(masses[slots:]),
        "overflow_bucket_probability": sum(masses[slots + 1:]),
        "overflow_key_fraction": spill / mean,
        "miss_primary_fingerprint_match_probability": sum(
            p * (1 - (127 / 128) ** min(count, slots))
            for count, p in enumerate(masses)
        ),
        "expected_rectangle_height": height,
        "rectangle_capacity_per_primary_entry": height / primary,
        "base_primary_entry_fraction": base_live / primary,
        "hybrid_capacity_per_primary_entry": (base_rows + primary - base_live) / primary,
        "control_bytes_per_distinct_key": slots / mean,
        "four_byte_overflow_directory_bytes_per_distinct_key": 4 / mean,
        "tail_bitmap_bytes_per_distinct_key": (slots - base_rows) / (8 * mean),
    }


if __name__ == "__main__":
    rows = [estimate(n, m, base) for n in (8, 16, 32, 64)
            for m in (1, 4, 16, 64) for base in (n // 2, 3 * n // 4)]
    for row in rows:
        if row["buckets_per_array"] == 1:
            assert abs(row["rectangle_capacity_per_primary_entry"] - 1) < 1e-11
    print(json.dumps({
        "scope": __doc__,
        "rows": rows,
    }, indent=2))
