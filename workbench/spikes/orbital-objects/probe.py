#!/usr/bin/env python3
"""Small, deterministic counterexamples and cost counts; no machine timings."""

import argparse
import hashlib
import itertools
import json
from collections import OrderedDict, deque
from pathlib import Path


def serial_value(base, effects, committed, cut):
    result = dict(base)
    for pos, patch in sorted(effects.items()):
        if pos <= cut and pos in committed:
            result.update(patch)
    return result


def page_histories():
    """Two fixed-position, disjoint logical effects sharing one physical page."""
    base = {"x": 0, "y": 0}
    effects = {10: {"x": 1}, 20: {"y": 2}}
    cases = []
    for order in itertools.permutations(("write10", "write20", "resolve10", "resolve20")):
        if any(order.index(f"write{p}") > order.index(f"resolve{p}") for p in effects):
            continue
        for committed in (set(), {10}, {20}, {10, 20}):
            physical = dict(base)
            views = {}
            resolved = set()
            errors = []
            for event in order:
                pos = int(event.removeprefix("write").removeprefix("resolve"))
                if event.startswith("write"):
                    # A private page starts from this transaction's logical snapshot.
                    views[pos] = serial_value(base, effects, committed & resolved, pos)
                    views[pos].update(effects[pos])
                else:
                    resolved.add(pos)
                    if pos in committed:
                        physical = dict(views[pos])  # Deliberately unsafe whole-page install.
                    expected = serial_value(base, effects, committed & resolved, 100)
                    if physical != expected:
                        errors.append(event)
            cases.append({"order": order, "committed": sorted(committed),
                          "reference_final_value": serial_value(base, effects, committed, 100),
                          "whole_page_install_errors": errors})

    working = {"x": 1, "y": 0}
    old_read = serial_value(base, effects, set(), 5)
    assert working != old_read
    # Rollback of an old whole-page preimage erases another writer's later result.
    assert dict(base) != serial_value(base, effects, {20}, 100)
    return {"histories": len(cases),
            "histories_with_bad_whole_page_install": sum(bool(c["whole_page_install_errors"]) for c in cases),
            "late_old_reader_naive": working, "late_old_reader_reconstructed": old_read,
            "unsafe_cases": [c for c in cases if c["whole_page_install_errors"]]}


def obligations():
    """Finite transition systems showing waits unrelated to transaction cycles."""
    # All ordinary workers parked in faults; only ordinary workers can service them.
    fault_pool = {"workers": 4, "parked": 4, "ready_handlers": 4}
    runnable_shared = fault_pool["workers"] - fault_pool["parked"]
    assert runnable_shared == 0
    # Independent service capacity fixes this worker dependency, but NOT full memory.
    memory_pages = 4
    held_input_pages = 4
    assert memory_pages - held_input_pages == 0

    # A two-stage acyclic graph: outputs retained until parent commit.
    def chunks(retire_consumed):
        retained = 0
        consumed = 0
        for _ in range(3):
            if retained == 2:
                break
            retained += 1
            consumed += 1
            if retire_consumed:
                retained -= 1
        return {"consumed": consumed, "retained": retained, "committed": consumed == 3}

    stuck, reconstructed = chunks(False), chunks(True)
    assert not stuck["committed"] and reconstructed["committed"]
    # Cancellation does not stop a submitted device write into its buffer.
    buffer_generation = 7
    backend_target_generation = buffer_generation
    cancel_requested = True
    assert cancel_requested and backend_target_generation == buffer_generation
    return {"faults_on_shared_workers": {"runnable_workers": runnable_shared, "stalled": True},
            "dedicated_handler_with_no_memory": {"free_pages": 0, "stalled": True},
            "retention_until_commit": stuck, "consumed_reconstructible_chunks": reconstructed,
            "cancelled_io_buffer_reusable_before_backend_retirement": False,
            "stale_completion_rejected_by_generation": (8 != backend_target_generation)}


def fault_progress():
    """Exhaust reachable schedules in a tiny resource-and-event transition model.

    Three callers already hold worker slots and one input page each. Each awaits
    its own page resolution. A handler needs a service execution slot and one
    temporary page, signals the caller, then releases those resources. A caller
    finishes and releases its held resources after that signal. A held application
    lock can additionally prevent the handler from starting. These dependencies
    are explicit; an omniscient scheduler cannot invent free capacity.
    """
    rows = []
    for separate_service in (False, True):
        for spare_page in (0, 1):
            for lock_cycle in (False, True):
                # Resources: caller CPU, service CPU, memory, application lock.
                available = (0, 1 if separate_service else 0, spare_page, 0 if lock_cycle else 1)
                jobs = []
                n = 3
                zero = (0, 0, 0, 0)
                for i in range(n):
                    release = (1, 0, 1, int(lock_cycle and i == 0))
                    jobs.append([(zero, release, 1 << i, 0)])
                for i in range(n):
                    need = (int(not separate_service), int(separate_service), 1, int(lock_cycle and i == 0))
                    jobs.append([(need, zero, 0, 0), (zero, zero, 0, 1 << i), (zero, need, 0, 0)])
                initial = ((0,) * len(jobs), available, 0)
                pending = deque([initial])
                seen = {initial}
                deadlocks = complete = 0
                while pending:
                    pcs, free, facts = pending.popleft()
                    if all(pc == len(job) for pc, job in zip(pcs, jobs)):
                        complete += 1
                        continue
                    advanced = False
                    for actor, job in enumerate(jobs):
                        if pcs[actor] == len(job):
                            continue
                        need, release, wait_for, signal = job[pcs[actor]]
                        if wait_for & facts != wait_for or any(a < b for a, b in zip(free, need)):
                            continue
                        advanced = True
                        next_pcs = list(pcs)
                        next_pcs[actor] += 1
                        next_free = tuple(a - b + c for a, b, c in zip(free, need, release))
                        successor = (tuple(next_pcs), next_free, facts | signal)
                        if successor not in seen:
                            seen.add(successor)
                            pending.append(successor)
                    if not advanced:
                        deadlocks += 1
                expected_progress = separate_service and bool(spare_page) and not lock_cycle
                assert bool(complete) == expected_progress
                assert bool(deadlocks) != expected_progress
                rows.append({"independent_service_slot": separate_service,
                             "spare_resolution_pages": spare_page,
                             "faulting_caller_holds_resolver_lock": lock_cycle,
                             "reachable_states": len(seen), "complete_states": complete,
                             "deadlock_states": deadlocks})
    return rows


def mapping_trace(trace, pages, capacity, policy, window=8):
    """One serial reader, LRU cache and synchronous loads; counts, no overlap model.

    'known' uses exact knowledge of the next bounded execution region. The owner
    supplied that access description; it is not available for pointer-chase cases.
    'window' guesses neighboring pages at each miss. 'eager' requires a full pin.
    """
    if policy == "eager" and pages > capacity:
        return {"status": "refused", "loads": 0, "batches": 0, "faults": 0, "peak_pages": 0}
    cache = OrderedDict()
    loads = batches = faults = peak = 0
    touched = set()
    loaded = set()

    def load(batch):
        nonlocal loads, batches, peak
        missing = [p for p in dict.fromkeys(batch) if p not in cache]
        if missing:
            batches += 1
        for p in missing:
            while len(cache) >= capacity:
                cache.popitem(last=False)
            cache[p] = None
            loads += 1
            loaded.add(p)
            peak = max(peak, len(cache))

    if policy == "eager":
        load(range(pages))
    for i, p in enumerate(trace):
        if policy == "known" and i % window == 0:
            load(trace[i:i + window])
        if p not in cache:
            faults += 1
            if policy == "window":
                start = p // window * window
                load(range(start, min(start + window, pages)))
            else:
                load([p])
        cache.move_to_end(p)
        touched.add(p)
    return {"status": "complete", "loads": loads, "batches": batches, "faults": faults,
            "peak_pages": peak, "distinct_untouched_loaded_pages": len(loaded - touched),
            "useful_distinct_pages": len(touched)}


def mappings():
    traces = {
        "small_sequential": (16, list(range(16)), True),
        "large_scan": (256, list(range(256)), True),
        "sparse_known": (256, [i * 17 % 256 for i in range(16)], True),
        "clustered_known": (256, list(range(32, 48)) + list(range(128, 144)), True),
        "dependent_pointer_chase": (256, [i * 17 % 256 for i in range(64)], False),
        "repeated_hot_set": (256, list(range(8)) * 16, True),
        "wrong_prefetch": (256, [i * 8 for i in range(32)], False),
        "snapshot_wider_than_budget": (256, list(range(256)) * 2, True),
    }
    rows = []
    for name, (pages, trace, known) in traces.items():
        for capacity in (16, 64, 256):
            for policy in ("eager", "demand", "window", "known"):
                if policy == "known" and not known:
                    continue
                result = mapping_trace(trace, pages, capacity, policy)
                assert result["peak_pages"] <= capacity
                rows.append({"scenario": name, "capacity_pages": capacity, "policy": policy, **result})
    assert mapping_trace(list(range(256)), 256, 16, "known")["faults"] == 0
    assert mapping_trace([i * 8 for i in range(32)], 256, 16, "window")["loads"] == 256
    return rows


def elision_counts():
    """Charge old-view reconstruction to real reads/aborts; no probability oracle."""
    cases = []
    for writes in (16, 256):
        for replay_ops in (1, 16, 256):
            for old_read_fraction in (0, 0.0625, 0.5, 1):
                for aborted_fraction in (0, 0.0625):
                    reads = int(writes * old_read_fraction)
                    aborted = int(writes * aborted_fraction)
                    # Reconstruct each requested old view once; no sharing assumed.
                    cases.append({"written_pages": writes, "old_views": reads,
                                  "aborted_pages": aborted, "replay_ops_per_page": replay_ops,
                                  "cow_copied_pages": writes,
                                  "elision_reconstructed_pages": reads + aborted,
                                  "elision_replayed_ops": (reads + aborted) * replay_ops,
                                  "retained_base_and_replay_inputs_required": True})
    return cases


def cache_hints():
    """Ownership transfers for two 32-byte allocations in a 64-byte line.

    This models coherence ownership, not a particular CPU's cache hash or cost.
    A wrong hint can reproduce the compact layout's false sharing.
    """
    rows = []
    for layout, lines in (("compact_shared_line", (0, 0)),
                          ("owner_separated", (0, 1)),
                          ("wrong_same_owner_hint", (0, 0))):
        owner = {}
        transfers = first_touches = 0
        for _ in range(100):
            for core in (0, 1):
                line = lines[core]
                if line not in owner:
                    first_touches += 1
                elif owner[line] != core:
                    transfers += 1
                owner[line] = core
        rows.append({"layout": layout, "line_bytes": len(set(lines)) * 64,
                     "first_touches": first_touches, "ownership_transfers": transfers})
    assert rows[0]["ownership_transfers"] == 199 and rows[1]["ownership_transfers"] == 0
    return rows


def run():
    return {"model": "orbital-object-design-probe-v1", "units": "logical counts, not measured latency",
            "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "page_histories": page_histories(), "resource_histories": obligations(),
            "fault_progress_cases": fault_progress(),
            "mapping_cases": mappings(), "elision_cases": elision_counts(),
            "scratch_layout_cases": cache_hints()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"histories": result["page_histories"]["histories"],
                      "unsafe_whole_page_histories": result["page_histories"]["histories_with_bad_whole_page_install"],
                      "mapping_cases": len(result["mapping_cases"]),
                      "elision_cases": len(result["elision_cases"]),
                      "source_sha256": result["source_sha256"]}, indent=2))
