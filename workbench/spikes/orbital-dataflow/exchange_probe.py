#!/usr/bin/env python3
"""Exact small exchange algorithms and a finite queue counterexample.

This is a logical/cost probe, not a calibrated network or query executor.
The independent nested-loop oracle is used only by callers/checks.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import asdict, dataclass
from hashlib import sha256
import json
from pathlib import Path
import platform


@dataclass(frozen=True)
class Row:
    occurrence: int
    key: int | None
    home: int
    width: int = 64


def equal(left: Row, right: Row) -> bool:
    """SQL equality-join semantics; NULL does not match NULL."""
    return left.key is not None and right.key is not None and left.key == right.key


def reference_join(left: list[Row], right: list[Row], kind: str = "inner") -> Counter:
    result = Counter()
    for lrow in left:
        matches = [rrow for rrow in right if equal(lrow, rrow)]
        if kind in ("inner", "left"):
            result.update((lrow.occurrence, rrow.occurrence) for rrow in matches)
        if not matches and kind in ("left", "anti"):
            result[(lrow.occurrence, None)] += 1
    if kind not in ("inner", "left", "anti"):
        raise ValueError(kind)
    return result


def occurrence_hash(result: Counter) -> str:
    # JSON encodes NULL; sorting by JSON avoids comparing None with an integer.
    entries = sorted([[*pair, count] for pair, count in result.items()], key=json.dumps)
    return sha256(json.dumps(entries, separators=(",", ":")).encode()).hexdigest()


def join_exchange(
    left: list[Row], right: list[Row], strategy: str,
    workers: int = 4, build_limit: int = 1 << 20,
    hot_keys: frozenset[int] = frozenset({0}),
) -> tuple[dict, Counter]:
    """Materialize an exact partitioned inner join after an oracle memory check.

    Build limit includes row width + a declared 32-byte hash entry. Result rows
    stream into a counter outside the modeled memory; output retention is not
    modeled. Viability checks know complete inputs, unlike a live controller.
    """
    if workers < 1 or build_limit < 0:
        raise ValueError("workers and build limit")
    if strategy not in {"repartition", "broadcast", "semijoin", "colocated", "salt", "grid"}:
        raise ValueError(strategy)
    if strategy == "grid" and workers != 4:
        raise ValueError("the focused grid probe uses a 2 x 2 worker grid")
    for rows in (left, right):
        if len({r.occurrence for r in rows}) != len(rows):
            raise ValueError("one stable ID per input occurrence is required")
        if any(r.width <= 0 or not 0 <= r.home < workers for r in rows):
            raise ValueError("invalid width or source host")
    lparts = [[] for _ in range(workers)]
    rparts = [[] for _ in range(workers)]
    wire = 0
    summary_wire = 0
    filter_ops = 0
    dropped_right = 0
    filtered_right = right
    if strategy == "semijoin":
        # Gather exact source key sets to worker 0, then distribute the complete
        # set to every source of right rows. No fingerprint false positives.
        per_source = [set() for _ in range(workers)]
        for row in left:
            if row.key is not None:
                per_source[row.home].add(row.key)
        keys = set().union(*per_source)
        summary_wire = 8 * (sum(len(s) for s in per_source[1:]) + (workers - 1) * len(keys))
        filter_ops = len(left) + len(right)
        filtered_right = [row for row in right if row.key in keys]
        dropped_right = len(right) - len(filtered_right)
    for side, rows, parts in (("left", left, lparts), ("right", filtered_right, rparts)):
        for row in rows:
            natural = 0 if row.key is None else row.key % workers
            if strategy == "broadcast":
                destinations = [row.home] if side == "left" else list(range(workers))
            elif strategy == "colocated":
                if row.home != natural:
                    raise ValueError("colocated join requires compatible complete key placement")
                destinations = [row.home]
            elif strategy == "salt" and row.key in hot_keys:
                destinations = [row.occurrence % workers] if side == "left" else list(range(workers))
            elif strategy == "grid" and row.key in hot_keys:
                lane = row.occurrence % 2
                destinations = [2 * lane, 2 * lane + 1] if side == "left" else [lane, lane + 2]
            else:
                destinations = [natural]
            for destination in destinations:
                parts[destination].append(row)
                if destination != row.home:
                    wire += row.width
    planned_build = [sum(row.width + 32 for row in part) for part in rparts]
    build_peak = max(planned_build, default=0)
    accepted = build_peak <= build_limit
    output = Counter()
    worker_stats = []
    for worker, (lpart, rpart) in enumerate(zip(lparts, rparts)):
        pairs = 0
        if accepted:
            table = defaultdict(list)
            for row in rpart:
                if row.key is not None:
                    table[row.key].append(row.occurrence)
            for row in lpart:
                for rid in table.get(row.key, ()):
                    output[(row.occurrence, rid)] += 1
                    pairs += 1
        worker_stats.append({
            "worker": worker, "planned_build_rows": len(rpart),
            "planned_build_bytes": planned_build[worker], "planned_probe_rows": len(lpart),
            "output_rows": pairs,
            "work_units": len(rpart) + len(lpart) + pairs if accepted else 0,
        })
    summary = {
        "strategy": strategy, "workers": workers, "build_limit_bytes": build_limit,
        "status": "complete" if accepted else "plan_rejected_build_memory",
        "offered_left_rows": len(left), "offered_right_rows": len(right),
        "completed_input_rows": len(left) + len(right) if accepted else 0,
        "not_admitted_input_rows": 0 if accepted else len(left) + len(right),
        "planned_left_copies": sum(map(len, lparts)), "planned_right_copies": sum(map(len, rparts)),
        "planned_data_wire_bytes": wire, "planned_summary_wire_bytes": summary_wire,
        "actual_input_wire_bytes": wire + summary_wire if accepted else 0,
        "planned_filter_work_units": filter_ops, "filtered_right_rows": dropped_right,
        "max_build_bytes": build_peak, "worker_stats": worker_stats,
        "critical_worker_work_units": max((s["work_units"] for s in worker_stats), default=0),
        "total_work_units": sum(s["work_units"] for s in worker_stats) + (filter_ops if accepted else 0),
        "output_rows": output.total(), "output_bag_sha256": occurrence_hash(output),
        "output_gather_wire_bytes": 32 * sum(s["output_rows"] for s in worker_stats[1:]),
    }
    return summary, output


def filter_probe_side(left: list[Row], right: list[Row], kind: str, preserve_absent: bool) -> Counter:
    """Runtime-filter branch: absent probe keys must survive outer/anti joins."""
    keys = {row.key for row in right if row.key is not None}
    potential = [row for row in left if row.key is not None and row.key in keys]
    absent = [row for row in left if row.key is None or row.key not in keys]
    output = reference_join(potential, right, kind)
    if preserve_absent and kind in {"left", "anti"}:
        output.update((row.occurrence, None) for row in absent)
    return output


def count_join(left: list[Row], right: list[Row], workers: int = 4) -> dict:
    """Count a bag equijoin via per-key multiplicities, without emitting pairs.

    This represents a different requested result, not a replacement for a
    row-producing join with an arbitrary per-pair extension callback.
    """
    partitions = [[Counter() for _ in range(workers)] for _ in range(2)]
    wire = 0
    shipped_records = 0
    merge_additions = 0
    for side, rows in enumerate((left, right)):
        sources = [Counter() for _ in range(workers)]
        for row in rows:
            if row.key is not None:
                sources[row.home][row.key] += 1
        for home, counts in enumerate(sources):
            for key, count in counts.items():
                destination = key % workers
                partitions[side][destination][key] += count
                merge_additions += 1
                if destination != home:
                    wire += 16  # Declared 8-byte integer key and 8-byte count.
                    shipped_records += 1
    products = [sum(count * partitions[1][w][key] for key, count in partitions[0][w].items())
                for w in range(workers)]
    return {"count": sum(products), "wire_bytes": wire + 8 * (workers - 1),
            "shipped_key_count_records": shipped_records,
            "local_count_updates": len(left) + len(right), "merge_additions": merge_additions,
            "key_products": sum(len(p) for p in partitions[0]), "partial_counts": products,
            "row_pairs_enumerated": 0}


@dataclass(frozen=True)
class Offer:
    identity: int
    arrival: int
    destinations: tuple[int, ...]
    width: int = 64


def queue_probe(offers: list[Offer], policy: str, stall_until: int, deadline: int,
                total_bytes: int = 1024, per_destination_bytes: int = 256,
                destinations: int = 4, nic_messages_per_tick: int = 2) -> dict:
    """Discrete slots; finite payload buffers, one stalled consumer, no losses.

    A parent's fanout is admitted atomically. Rejected offers remain caller
    obligations; they are counted, never treated as completed/dropped inputs.
    The input offer log and delivered-ID log are observation state, not buffers.
    Every accepted child consumes a buffer until delivered at slot end.
    """
    if policy not in {"fifo", "per_destination_shared", "per_destination_reserved"}:
        raise ValueError(policy)
    if total_bytes < 0 or per_destination_bytes < 0 or destinations < 1 or nic_messages_per_tick < 1:
        raise ValueError("queue configuration")
    if len({o.identity for o in offers}) != len(offers):
        raise ValueError("offer identities must be unique")
    if any(o.width <= 0 or o.arrival < 0 or
           len(set(o.destinations)) != len(o.destinations) or
           any(not 0 <= d < destinations for d in o.destinations) for o in offers):
        raise ValueError("offer geometry")
    queues = [deque() for _ in range(destinations)]
    fifo = deque()
    queued_bytes = [0] * destinations
    by_time = defaultdict(list)
    for offer in offers:
        by_time[offer.arrival].append(offer)
    accepted = set()
    rejected = set()
    delivered = {}
    peak = 0
    scans = 0
    nic_bytes = 0
    next_destination = 0
    for tick in range(deadline):
        for offer in by_time[tick]:
            bytes_needed = offer.width * len(offer.destinations)
            fits = sum(queued_bytes) + bytes_needed <= total_bytes
            if policy == "per_destination_reserved":
                fits &= all(queued_bytes[d] + offer.width <= per_destination_bytes for d in offer.destinations)
            if not fits:
                rejected.add(offer.identity)
                continue
            accepted.add(offer.identity)
            for destination in offer.destinations:
                child = (offer.identity, destination, offer.width)
                fifo.append(child) if policy == "fifo" else queues[destination].append(child)
                queued_bytes[destination] += offer.width
            peak = max(peak, sum(queued_bytes))
        used_destinations = set()
        for _ in range(nic_messages_per_tick):
            selected = None
            if policy == "fifo":
                if fifo:
                    scans += 1
                    child = fifo[0]
                    if child[1] not in used_destinations and (child[1] != 0 or tick >= stall_until):
                        selected = fifo.popleft()
            else:
                for offset in range(destinations):
                    destination = (next_destination + offset) % destinations
                    scans += 1
                    if queues[destination] and destination not in used_destinations and (destination != 0 or tick >= stall_until):
                        selected = queues[destination].popleft()
                        next_destination = (destination + 1) % destinations
                        break
            if selected is None:
                break
            identity, destination, width = selected
            delivered[(identity, destination)] = tick + 1
            queued_bytes[destination] -= width
            used_destinations.add(destination)
            nic_bytes += width
    completed = [o for o in offers if o.identity in accepted and all((o.identity, d) in delivered for d in o.destinations)]
    eligible_offers = [o for o in offers if o.arrival < deadline]
    pending = accepted - {o.identity for o in completed}
    finish = lambda o: max((delivered[(o.identity, d)] for d in o.destinations), default=o.arrival)
    latencies = sorted(finish(o) - o.arrival for o in completed)
    p99 = latencies[min(len(latencies) - 1, (99 * len(latencies) + 99) // 100 - 1)] if latencies else None
    offered_children = sum(len(o.destinations) for o in eligible_offers)
    rejected_children = sum(len(o.destinations) for o in eligible_offers if o.identity in rejected)
    queued_children = sum(len(q) for q in queues) + len(fifo)
    assert offered_children == len(delivered) + rejected_children + queued_children
    assert len(eligible_offers) == len(completed) + len(pending) + len(rejected)
    assert sum(queued_bytes) <= total_bytes
    return {
        "policy": policy, "stall_until_tick": stall_until, "deadline_tick": deadline,
        "total_buffer_bytes": total_bytes, "per_destination_limit_bytes":
            per_destination_bytes if policy == "per_destination_reserved" else None,
        "offered_parents": len(eligible_offers), "offered_children": offered_children,
        "complete_parents": len(completed), "not_admitted_parents": len(rejected),
        "accepted_unfinished_parents": len(pending),
        "delivered_children": len(delivered), "not_admitted_children": rejected_children,
        "queued_children": queued_children, "queued_bytes": sum(queued_bytes),
        "peak_buffer_bytes": peak, "nic_bytes": nic_bytes, "dispatch_eligibility_checks": scans,
        "completed_parent_p99_ticks": p99,
        "all_offered_p99_ticks": latencies[(99 * len(eligible_offers) + 99) // 100 - 1]
            if eligible_offers and len(completed) >= (99 * len(eligible_offers) + 99) // 100 else None,
        "healthy_only_parents": sum(0 not in o.destinations for o in eligible_offers),
        "healthy_only_complete": sum(0 not in o.destinations for o in completed),
        "completed_by_stall_end": sum(finish(o) <= stall_until for o in completed),
    }


def algebra_examples() -> dict:
    groups = [{"A": 6, "B": 5}, {"C": 6, "B": 5}]
    sums = Counter()
    for group in groups:
        sums.update(group)
    local_candidates = {max(group, key=group.get) for group in groups}
    true_top = max(sums, key=sums.get)
    wrong_top = max(local_candidates, key=lambda key: (sums[key], key))
    left = [Row(0, 1, 0), Row(1, 2, 0), Row(2, None, 0)]
    right = [Row(0, 1, 0), Row(1, 1, 0), Row(2, None, 0)]
    outer = reference_join(left, right, "left")
    anti = reference_join(left, right, "anti")
    # A SQL NOT IN with a NULL in the subquery has no true result here;
    # NOT EXISTS still emits both the key-2 and NULL probe rows.
    rows = [(3, 0), (5, 1), (5, 2), (1, 3), (4, 4)]
    ranked = lambda xs: sorted(xs, key=lambda row: (-row[0], row[1]))
    parts = [rows[::2], rows[1::2]]
    local_top2 = ranked([row for part in parts for row in ranked(part)[:2]])[:2]
    r, s, t = {(0, 0), (1, 1)}, {(0, 0), (1, 1)}, {(0, 1), (1, 0)}
    triangle = [(a, b, c) for a, b in r for b2, c in s if b == b2 and (a, c) in t]
    return {
        "grouped_sum_top1": {"partitions": groups, "true_winner": true_top,
            "true_score": sums[true_top], "local_candidate_winner": wrong_top,
            "local_candidates": sorted(local_candidates), "valid_transform": False},
        "row_top2": {"global": ranked(rows)[:2], "merged_local": local_top2, "total_order": "score DESC, occurrence ASC"},
        "top1_with_ties": {"partitions": [[10, 10], [10]], "true_rows": 3, "bounded_local_top1_rows": 2},
        "cyclic_semijoin": {"R_ab": sorted(r), "S_bc": sorted(s), "T_ac": sorted(t),
            "every_pairwise_semijoin_preserves_every_row":
                all(any(a == a2 for a2, _ in t) and any(b == b2 for b2, _ in s) for a, b in r)
                and all(any(b == b2 for _, b2 in r) and any(c == c2 for _, c2 in t) for b, c in s)
                and all(any(a == a2 for a2, _ in r) and any(c == c2 for _, c2 in s) for a, c in t),
            "true_triangle_rows": len(triangle)},
        "outer_filter": {"true_rows": outer.total(), "blind_filter_rows": filter_probe_side(left, right, "left", False).total(),
            "preserve_absent_rows": filter_probe_side(left, right, "left", True).total()},
        "anti_filter": {"true_rows": anti.total(), "blind_filter_rows": filter_probe_side(left, right, "anti", False).total(),
            "preserve_absent_rows": filter_probe_side(left, right, "anti", True).total(), "sql_not_in_rows": 0},
        "float_regrouping": {"left_associated": (1e16 + 1.0) + -1e16,
            "permuted_group": (1e16 + -1e16) + 1.0},
        "replay_sum": {"once": sum([4, 7]), "duplicate_message": sum([4, 7, 7]),
            "deduplicated_delivery": sum({"a": 4, "b": 7}.values())},
    }


def run() -> dict:
    fixtures = {
        "small_build": ([Row(i, i % 32, (i + 1) % 4) for i in range(1024)],
                        [Row(i, i, (i + 2) % 4) for i in range(32)], 1 << 20),
        "wide_build": ([Row(i, i % 32, (i + 1) % 4) for i in range(512)],
                       [Row(i, i, (i + 2) % 4, 4096) for i in range(256)], 2 << 20),
        "selective_memory": ([Row(i, i % 16, (i + 1) % 4) for i in range(256)],
                             [Row(i, i, (i + 2) % 4, 256) for i in range(4096)], 64 << 10),
        "one_sided_hot": ([Row(i, 0 if i < 900 else i % 31 + 1, (i + 1) % 4) for i in range(1024)],
                          [Row(i, i, (i + 2) % 4) for i in range(32)], 1 << 20),
        "two_sided_hot": ([Row(i, 0 if i < 256 else i % 31 + 1, (i + 1) % 4) for i in range(320)],
                          [Row(i, 0 if i < 256 else i % 31 + 1, (i + 2) % 4) for i in range(320)], 20 << 10),
    }
    cases = []
    for name, (left, right, memory) in fixtures.items():
        reference = reference_join(left, right)
        for strategy in ("repartition", "broadcast", "semijoin", "salt", "grid"):
            stats, output = join_exchange(left, right, strategy, build_limit=memory)
            if stats["status"] == "complete":
                assert output == reference, (name, strategy)
            stats.update({"case": name, "expected_output_rows": reference.total(),
                          "expected_output_bag_sha256": occurrence_hash(reference)})
            cases.append(stats)
    # A compatible layout is a separate premise, not a free rewrite of inputs.
    left, right, memory = fixtures["small_build"]
    colocated = [[Row(r.occurrence, r.key, r.key % 4, r.width) for r in rows] for rows in (left, right)]
    stats, output = join_exchange(*colocated, "colocated", build_limit=memory)
    assert output == reference_join(left, right)
    stats.update({"case": "small_build_already_colocated", "expected_output_rows": output.total(),
                  "expected_output_bag_sha256": occurrence_hash(output)})
    cases.append(stats)
    smooth = [Offer(i, i, (0, 1, 2, 3) if i % 16 == 0 else (i % 4,)) for i in range(80)]
    burst = [Offer(i, i, (0,) if i < 16 else (1 + i % 3,)) for i in range(80)]
    queues = []
    for name, offers, stall, deadline in (("smooth", smooth, 60, 96), ("burst", burst, 60, 96),
                                           ("unfinished", smooth, 100, 80)):
        for policy in ("fifo", "per_destination_shared", "per_destination_reserved"):
            queues.append({"case": name, **queue_probe(offers, policy, stall, deadline)})
    hot_left, hot_right, _ = fixtures["two_sided_hot"]
    counted = count_join(hot_left, hot_right)
    assert counted["count"] == reference_join(hot_left, hot_right).total()
    return {"model": "exact bag algorithms and synthetic logical resource counts; ticks are not microseconds",
            "python": platform.python_version(),
            "source_sha256": sha256(Path(__file__).read_bytes()).hexdigest(),
            "hash_entry_bytes": 32, "summary_key_bytes": 8, "gathered_pair_bytes": 32,
            "selection": "all 26 exchange comparisons, nine finite queue comparisons, and algebra counterexamples",
            "joins": cases, "queues": queues, "algebra": algebra_examples(), "factorized_count": counted}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    encoded = json.dumps(run(), indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(encoded)
    else:
        print(encoded, end="")
