#!/usr/bin/env python3
"""Validate both orders and retain timings with stable footprint context."""
import argparse
import csv
from functools import lru_cache
import hashlib
import math
from pathlib import Path
import statistics

LAYOUTS = ("grid_major", "class_major", "class_major_reordered", "class_aligned")
GROUPED_KEYS = ["mapping_granule_bytes", "trampolines_per_group", "layout",
                "allocations", "payload_bytes", "path", "workload", "unit"]
GROUPED_CONTEXT = ["native_page_bytes", "mapping_granule_mode",
                   "reserved_va_bytes", "control_granules", "payload_granules",
                   "mapped_page_bytes", "mapped_native_pages", "owner_record_bytes",
                   "active_head_array_bytes", "total_head_array_bytes",
                   "order_array_bytes", "live_allocations",
                   "cached_empty_granules", "map_calls", "released_granules",
                   "source_context", "compiler", "architecture"]
OCCUPANCY_POLICIES = {
    "occupied_bitmap": (240, 30),
    "occupied_sorted_240": (240, 241),
    "occupied_unordered_240": (240, 241),
    "occupied_sorted_16": (16, 17),
    "occupied_unordered_16": (16, 17),
    "occupied_sorted_64": (64, 65),
    "occupied_unordered_64": (64, 65),
}
OCCUPANCY_KEYS = ["policy", "live", "distribution", "workload"]
TUNING_KEYS = ["allocator", "path", "history", "accounting", "bytes", "live",
               "replace_percent", "pairs_per_batch", "prepared_slots"]
TUNING_CONTEXT = ["persistent_live_requested_bytes", "peak_timed_requested_live_bytes",
                  "footprint_measured", "policy_metadata_bytes", "reserved_va_bytes",
                  "control_bytes", "report_trim_scratch_bytes"]
TUNING_VARIABLE = ["backing_bytes", "mapped_payload_bytes", "cached_empty_payload_bytes",
                   "timed_map_calls", "timed_discarded_granules", "report_sink"]
UINT64_MASK = 2**64 - 1


def require(condition, message):
    if not condition:
        raise ValueError(message)


def positive_float(row, field):
    value = float(row[field])
    require(math.isfinite(value) and value > 0, f"invalid {field}: {row}")
    return value


def integer(row, field, minimum=0):
    value = int(row[field])
    require(value >= minimum, f"invalid {field}: {row}")
    return value


def expected_grouped():
    cases = set()
    for granule in (4096, 16384):
        for width in (16, 64, 256, 1024):
            for layout in LAYOUTS:
                for count in (64, 4096):
                    for path in ("direct", "grouped_resolve"):
                        for workload in ("scan64", "random_point8", "dependent_chase16"):
                            cases.add(tuple(map(str, (granule, width, layout, count, 64,
                                                      path, workload, "allocation_access"))))
                for size in (16, 64, 96):
                    cases.add(tuple(map(str, (granule, width, layout, 1, size,
                                              "prepared_grid", "allocate_touch8_release",
                                              "allocation_free_pair"))))
    return cases


def expected_occupancy():
    histories = [(live, "mixed_classes", workload)
                 for live in (1, 4, 16, 64, 128, 240)
                 for workload in ("same_class_reuse", "enumerate")]
    histories += [(16, "one_class", workload)
                  for workload in ("same_class_reuse", "enumerate")]
    return {(policy, str(live), distribution, workload)
            for policy, (capacity, _) in OCCUPANCY_POLICIES.items()
            for live, distribution, workload in histories if live <= capacity}


def expected_tuning(libraries=True):
    cases = set()
    histories = ((1, 0), (64, 0), (1024, 0), (64, 10), (64, 90),
                 (1024, 10), (1024, 90))
    descriptors = ("descriptor_frozen_v1", "descriptor_tuned")
    peers = ("libc", "local_freelist", "local_bitmap")
    if libraries:
        peers += ("mimalloc_3.5.3", "snmalloc_0.7.5")
    for allocator in descriptors + peers:
        policies = ("none", "counters_every_batch", "trim_every8_batches") \
            if allocator in descriptors else ("none",)
        for size in (16, 64, 96):
            for live, percent in histories:
                pairs = (live * percent + 99) // 100 if percent else live
                history = "rolling" if percent else "allocate_all_free_permuted"
                for policy in policies:
                    cases.add(tuple(map(str, (allocator, "ordinary_allocate", history,
                                              policy, size, live, percent, pairs, 1024))))
            if allocator in descriptors:
                cases.add(tuple(map(str, (allocator, "selected_grid",
                                          "allocate_all_free_permuted", "none", size,
                                          1, 0, 1, 16))))
    return cases


def load_orders(directory, prefix, keys, rep_field, expected, validate, repetitions=3):
    groups = {}
    for order in ("forward", "reverse"):
        path = directory / f"{prefix}-{order}.csv"
        with path.open(newline="") as handle:
            reader = csv.DictReader(handle)
            require(reader.fieldnames and len(set(reader.fieldnames)) == len(reader.fieldnames),
                    f"missing/duplicate CSV columns: {path}")
            rows = list(reader)
        seen = {}
        for row in rows:
            require(None not in row and all(value is not None for value in row.values()),
                    f"malformed CSV row: {path}")
            key = tuple(row[field] for field in keys)
            require(key in expected, f"unexpected case {key}: {path}")
            seen.setdefault(key, []).append(integer(row, rep_field))
            validate(row)
            groups.setdefault(key, {}).setdefault(order, []).append(row)
        require(set(seen) == expected and all(sorted(reps) == list(range(repetitions))
                                              for reps in seen.values()),
                f"missing/duplicate cases or repetitions: {path}")
        print(f"{path.name}: {len(rows)} samples sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
    return groups


def stable(rows, field, key):
    values = {row[field] for row in rows}
    require(len(values) == 1, f"changing context {field}: {key}")
    return values.pop()


def aggregate(key, orders, keys, metric, context, operation_field):
    rows = orders["forward"] + orders["reverse"]
    values = [float(row[metric]) for row in rows]
    result = dict(zip(keys, key))
    result.update(samples=len(rows), ns_median=statistics.median(values),
                  ns_min=min(values), ns_max=max(values))
    for order in ("forward", "reverse"):
        result[f"{order}_ns_median"] = statistics.median(float(row[metric])
                                                       for row in orders[order])
    for field in context:
        result[field] = stable(rows, field, key)
    operations = [int(row[operation_field]) for row in rows]
    result[operation_field + "_min"] = min(operations)
    result[operation_field + "_max"] = max(operations)
    return result


def write_summary(directory, name, rows, keys, context, operation_field, extra=()):
    fields = keys + ["samples", "ns_median", "ns_min", "ns_max",
                     "forward_ns_median", "reverse_ns_median"] + context
    fields += [operation_field + "_min", operation_field + "_max"] + list(extra)
    output = directory / name
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    counts = sorted({row["samples"] for row in rows})
    print(f"{name}: {len(rows)} cases, both orders, samples/case={counts}")


def validate_grouped(row):
    positive_float(row, "ns_per_op")
    iterations = integer(row, "iterations", 1)
    checksum = integer(row, "checksum")
    require(checksum < 2**64, "grouped checksum exceeds uint64")
    native = integer(row, "native_page_bytes", 1)
    granule = integer(row, "mapping_granule_bytes", 1)
    require(native == 4096, "this comparison expects a 4KiB host")
    require(row["mapping_granule_mode"] == ("native" if granule == native
                                          else "modeled_mapping_granule"),
            "wrong native/modeled mapping label")
    control = integer(row, "control_granules", 1)
    payload = integer(row, "payload_granules", 1)
    mapped = integer(row, "mapped_page_bytes", 1)
    require(mapped == (control + payload) * granule, "wrong mapped byte accounting")
    require(integer(row, "mapped_native_pages", 1) * native == mapped,
            "wrong native page accounting")
    require(integer(row, "reserved_va_bytes", 1) >= mapped, "mapped exceeds reserved VA")
    for field in ("owner_record_bytes", "active_head_array_bytes", "total_head_array_bytes",
                  "order_array_bytes", "live_allocations", "cached_empty_granules",
                  "map_calls", "released_granules"):
        integer(row, field)
    allocations = integer(row, "allocations", 1)
    width = integer(row, "trampolines_per_group", 1)
    paired = row["unit"] == "allocation_free_pair"
    require(control == (1 if paired else (allocations + width - 1) // width),
            "wrong control group count")
    require(integer(row, "live_allocations") == (0 if paired else allocations),
            "wrong final logical allocation count")
    require(integer(row, "active_head_array_bytes") == (0 if paired else allocations * 8),
            "wrong active descriptor array bytes")
    require(integer(row, "total_head_array_bytes") == (128 if paired else allocations * 16),
            "wrong total descriptor array bytes")
    require(integer(row, "order_array_bytes") == (0 if paired else allocations * 8),
            "wrong access-order array bytes")
    if paired:
        require(row["ns_per_pair"] == row["ns_per_op"], "pair timing disagrees")
        require(iterations % 4096 == 0, "partial allocation-pair batch")
    else:
        require(row["ns_per_pair"] == "", "read row contains pair timing")
        require(iterations % allocations == 0, "partial access cohort")
    require(row["source_context"] and row["compiler"] and row["architecture"],
            "missing source/compiler/architecture context")
    expected = grouped_kernel_checksum(allocations, row["workload"])
    batch_size = 4096 if paired else allocations
    require(checksum == expected * (iterations // batch_size) & UINT64_MASK,
            "wrong grouped logical checksum")


def mix(value):
    value = (value + 0x9e3779b97f4a7c15) & UINT64_MASK
    value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & UINT64_MASK
    value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & UINT64_MASK
    return value ^ (value >> 31)


@lru_cache(maxsize=None)
def grouped_kernel_checksum(count, workload):
    if workload == "allocate_touch8_release":
        return (0x0123456789abcdef * 4096 + 4096 * 4095 // 2) & UINT64_MASK
    if workload == "scan64":
        return (count * (count - 1) // 2 +
                sum(mix(index * 8 + position) for index in range(count)
                    for position in range(1, 8))) & UINT64_MASK
    result = sum(mix(index * 8 + 1) for index in range(count))
    if workload == "dependent_chase16":
        order = list(range(count))
        entropy = 0x243f6a8885a308d3
        for remaining in range(count, 1, -1):
            entropy = mix(entropy)
            index = entropy % remaining
            order[remaining - 1], order[index] = order[index], order[remaining - 1]
        result += order[0]
    return result & UINT64_MASK


def summarize_grouped(directory, read_confirmation=False):
    all_groups = {}
    rows = []
    expected = expected_grouped()
    if read_confirmation:
        expected = {key for key in expected
                    if key[:3] == ("4096", "1024", "class_major_reordered")}
    for implementation in ("frozen", "tuned"):
        groups = load_orders(directory, f"grouped-{implementation}", GROUPED_KEYS,
                             "repetition", expected, validate_grouped,
                             5 if read_confirmation else 3)
        all_groups[implementation] = groups
        for key, orders in sorted(groups.items()):
            result = aggregate(key, orders, GROUPED_KEYS, "ns_per_op", GROUPED_CONTEXT,
                               "iterations")
            result["implementation"] = implementation
            rows.append(result)
    # Geometry and logical results should survive allocator tuning. The owner
    # record cost is retained separately because the optimization may change it.
    comparable = [field for field in GROUPED_CONTEXT
                  if field not in ("owner_record_bytes", "source_context")]
    for key in expected:
        frozen = all_groups["frozen"][key]["forward"] + all_groups["frozen"][key]["reverse"]
        tuned = all_groups["tuned"][key]["forward"] + all_groups["tuned"][key]["reverse"]
        for field in comparable:
            require(stable(frozen, field, key) == stable(tuned, field, key),
                    f"baseline/tuned mismatch {field}: {key}")
    write_summary(directory, "grouped-summary.csv", rows,
                  ["implementation"] + GROUPED_KEYS, GROUPED_CONTEXT, "iterations")


def validate_occupancy(row):
    ns = positive_float(row, "ns_per_operation")
    per_item = positive_float(row, "ns_per_item")
    operations = integer(row, "operations", 1)
    live = integer(row, "live", 1)
    capacity, metadata = OCCUPANCY_POLICIES[row["policy"]]
    require(integer(row, "prepared_capacity") == capacity and
            integer(row, "metadata_bytes") == metadata, "wrong occupied-set storage charge")
    coordinates = [(i << 4) if row["distribution"] == "one_class"
                   else ((i // 15) << 4) | (i % 15) for i in range(live)]
    total = sum(coordinate + 1 for coordinate in coordinates)
    if row["workload"] == "enumerate":
        expected_checksum = operations * total * 3
        require(operations % 64 == 0, "partial occupied enumeration batch")
        require(math.isclose(per_item, ns / live, rel_tol=1e-5), "wrong enumeration item timing")
    else:
        require(operations % (64 * live) == 0, "partial release/claim batch")
        expected_checksum = operations // live * total
        require(math.isclose(per_item, ns, rel_tol=1e-5), "wrong release/claim item timing")
    require(integer(row, "checksum") == expected_checksum, "wrong occupied-set logical checksum")


def summarize_occupancy(directory):
    groups = load_orders(directory, "occupancy", OCCUPANCY_KEYS, "rep",
                         expected_occupancy(), validate_occupancy)
    context = ["metadata_bytes", "prepared_capacity"]
    rows = []
    for key, orders in sorted(groups.items()):
        result = aggregate(key, orders, OCCUPANCY_KEYS, "ns_per_operation", context,
                           "operations")
        item_values = [float(row["ns_per_item"]) for order in orders.values() for row in order]
        result.update(ns_per_item_median=statistics.median(item_values),
                      ns_per_item_min=min(item_values), ns_per_item_max=max(item_values))
        rows.append(result)
    write_summary(directory, "occupancy-summary.csv", rows, OCCUPANCY_KEYS, context,
                  "operations", ["ns_per_item_median", "ns_per_item_min", "ns_per_item_max"])


def validate_tuning(row):
    positive_float(row, "ns_per_pair")
    pairs = integer(row, "pairs", 1)
    batches = integer(row, "batches", 1)
    per_batch = integer(row, "pairs_per_batch", 1)
    require(pairs == batches * per_batch, "wrong matched history pair count")
    require(batches % max(1, 4096 // per_batch) == 0,
            "partial matched history timing batch")
    require(integer(row, "checksum") <= UINT64_MASK, "matched checksum exceeds uint64")
    live = integer(row, "live", 1)
    size = integer(row, "bytes", 1)
    rolling = row["history"] == "rolling"
    require(integer(row, "persistent_live_requested_bytes") == (live * size if rolling else 0),
            "wrong persistent current-live requested bytes")
    require(integer(row, "peak_timed_requested_live_bytes") == live * size,
            "wrong peak requested live bytes")
    for field in TUNING_CONTEXT[2:] + TUNING_VARIABLE:
        integer(row, field)
    descriptor = row["allocator"] in ("descriptor_frozen_v1", "descriptor_tuned")
    measured = integer(row, "footprint_measured")
    require(measured in (0, 1), "wrong footprint measurement label")
    if descriptor:
        require(measured == 1, "missing descriptor footprint accounting")
        control = integer(row, "control_bytes", 1)
        mapped = integer(row, "mapped_payload_bytes")
        require(control == 4096 and mapped % control == 0,
                "this matched comparison expects 4KiB control and payload granules")
        require(integer(row, "cached_empty_payload_bytes") <= mapped and
                integer(row, "cached_empty_payload_bytes") % control == 0,
                "wrong descriptor cached payload accounting")
        require(integer(row, "backing_bytes") == control + mapped,
                "wrong descriptor backing byte accounting")
        require(integer(row, "reserved_va_bytes") >= control + mapped,
                "descriptor backing exceeds reserved VA")
        require(integer(row, "policy_metadata_bytes", 1), "missing descriptor owner charge")
        scratch = integer(row, "report_trim_scratch_bytes")
        require(scratch == ((integer(row, "reserved_va_bytes") - control) // control
                            if row["allocator"] == "descriptor_tuned" else 0),
                "wrong deferred report/trim scratch charge")
        if row["accounting"] != "trim_every8_batches":
            require(integer(row, "timed_map_calls") == 0 and
                    integer(row, "timed_discarded_granules") == 0,
                    "prepared descriptor history mapped/discarded timed backing")
    else:
        require(integer(row, "timed_map_calls") == 0 and
                integer(row, "timed_discarded_granules") == 0 and
                integer(row, "report_trim_scratch_bytes") == 0,
                "peer row claims unavailable descriptor counters")
        if row["allocator"] in ("local_freelist", "local_bitmap"):
            require(measured == 1 and integer(row, "backing_bytes") == size * 1024,
                    "wrong prepared pool backing charge")
            minimum_metadata = 4096 if row["allocator"] == "local_freelist" else 136
            require(integer(row, "policy_metadata_bytes") >= minimum_metadata,
                    "pool metadata omits prepared index storage")
            for field in ("reserved_va_bytes", "control_bytes", "mapped_payload_bytes",
                          "cached_empty_payload_bytes"):
                require(integer(row, field) == 0, "peer row claims unavailable mapping counters")
        else:
            require(measured == 0, "vendor/libc footprint should remain unknown")
            for field in ("backing_bytes", "policy_metadata_bytes", "reserved_va_bytes",
                          "control_bytes", "mapped_payload_bytes", "cached_empty_payload_bytes"):
                require(integer(row, field) == 0, "unknown vendor footprint contains a measured charge")
    if row["accounting"] == "counters_every_batch":
        require(integer(row, "report_sink", 1), "unconsumed counter reporting")
    else:
        require(integer(row, "report_sink") == 0, "report sink outside reporting cadence")


@lru_cache(maxsize=None)
def rolling_prefix_weights(live):
    order = list(range(live))
    entropy = 0x243f6a8885a308d3
    for remaining in range(live, 1, -1):
        entropy ^= entropy << 13
        entropy &= UINT64_MASK
        entropy ^= entropy >> 7
        entropy ^= entropy << 17
        entropy &= UINT64_MASK
        index = entropy % remaining
        order[remaining - 1], order[index] = order[index], order[remaining - 1]
    prefix = [0]
    for index in order:
        prefix.append(prefix[-1] + index + 1)
    return prefix


def rolling_checksum_prefix(steps, live):
    full, partial = divmod(steps, live)
    weights = rolling_prefix_weights(live)
    initial_values = full * weights[-1] + weights[partial]
    increments = live * (live * full * (full + 1) // 2 + partial * (full + 1))
    return initial_values + increments


def summarize_tuning(directory, libraries):
    groups = load_orders(directory, "tuning", TUNING_KEYS, "rep",
                         expected_tuning(libraries), validate_tuning)
    rows = []
    for key, orders in sorted(groups.items()):
        for order, samples in orders.items():
            per_batch = int(samples[0]["pairs_per_batch"])
            live = int(samples[0]["live"])
            start = 8 * per_batch  # Warmup advances the rolling cursor/values.
            previous_sink = 0
            for row in sorted(samples, key=lambda sample: int(sample["rep"])):
                steps = int(row["pairs"])
                if row["history"] == "rolling":
                    expected = (rolling_checksum_prefix(start + steps, live) -
                                rolling_checksum_prefix(start, live)) & UINT64_MASK
                else:
                    expected = int(row["batches"]) * live * (live + 1) // 2 & UINT64_MASK
                require(int(row["checksum"]) == expected,
                        f"wrong matched history logical checksum: {key}/{order}/rep{row['rep']}")
                start += steps
                if row["accounting"] == "counters_every_batch":
                    require(int(row["report_sink"]) > previous_sink,
                            "counter reporting sink did not advance")
                    previous_sink = int(row["report_sink"])
        result = aggregate(key, orders, TUNING_KEYS, "ns_per_pair", TUNING_CONTEXT, "pairs")
        samples = orders["forward"] + orders["reverse"]
        for field in TUNING_VARIABLE:
            values = [int(row[field]) for row in samples]
            result[field + "_median"] = statistics.median(values)
            result[field + "_min"] = min(values)
            result[field + "_max"] = max(values)
        rows.append(result)
    # Matched descriptor histories retain the same aperture and logical size;
    # dynamic cached/mapped backing depends on where periodic trim stops.
    indexed = {tuple(result[field] for field in TUNING_KEYS): result for result in rows}
    for key, result in indexed.items():
        if key[0] != "descriptor_frozen_v1":
            continue
        tuned = indexed[("descriptor_tuned",) + key[1:]]
        for field in ("persistent_live_requested_bytes", "peak_timed_requested_live_bytes",
                      "reserved_va_bytes", "control_bytes", "footprint_measured"):
            require(result[field] == tuned[field], f"matched descriptor mismatch {field}: {key}")
    extra = [field + suffix for field in TUNING_VARIABLE for suffix in ("_median", "_min", "_max")]
    write_summary(directory, "tuning-summary.csv", rows, TUNING_KEYS, TUNING_CONTEXT,
                  "pairs", extra)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--without-libraries", action="store_true",
                        help="explicitly validate the 195-case local comparison")
    parser.add_argument("--read-confirmation", action="store_true",
                        help="validate the focused fifteen-case, five-repetition comparison")
    arguments = parser.parse_args()
    directory = arguments.directory
    summarize_grouped(directory, arguments.read_confirmation)
    if not arguments.read_confirmation:
        summarize_occupancy(directory)
        summarize_tuning(directory, not arguments.without_libraries)


if __name__ == "__main__":
    main()
