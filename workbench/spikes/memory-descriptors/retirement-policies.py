#!/usr/bin/env python3
"""Deterministic retirement alternatives: logical work and byte-ticks, no ns.

The matrix keeps owner affinity, protection width and page coverage separate.
Small scripted histories inject consequential faults and check independent
backend-use state.  This is finite evidence, not a production protocol or ABI.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path


PAGE = 4096
GRID_WIDTH = 16
GRID_COUNT = 64
CELLS = tuple(grid * GRID_WIDTH + slot for grid in range(GRID_COUNT) for slot in range(7))


def matrix_case(group: int, protection: str, retirement: str, batch_size=8) -> dict:
    """One identical inventory/history, with distinct retirement authorities.

    Seven current identities/grid leave legal destinations; growth moves each
    64-byte old home to a 128-byte class.  Class-major old ranges use a 1024-byte
    grid pitch; new ranges use a 2048-byte pitch.  All notifications are remote
    to isolate routing rather than inventing a core-placement distribution.
    """
    owner = lambda cell: cell // group
    owner_count = GRID_COUNT * GRID_WIDTH // group
    old_page = lambda cell: (cell // GRID_WIDTH) // 4
    new_pages = set(range(16, 48))
    if protection == "home":
        domain = {0}
    elif protection == "grid":
        domain = {cell for cell in CELLS if cell // GRID_WIDTH == 0}
    else:
        domain = {cell for cell in CELLS if owner(cell) == owner(0)}
    outstanding = set(CELLS)
    protected = True
    queues = defaultdict(deque)
    work = Counter()
    age = None if retirement == "owner_size_only" else 3
    pressure = retirement == "owner_size_age_pressure"
    queue_capacity = 32
    current_bytes = len(CELLS) * 128
    retained_byte_ticks = 0
    retained_page_byte_ticks = 0
    peak_retained = 0
    peak_total_capacity = 0
    peak_mapped_page_bytes = 0
    peak_queue_records = 0
    last_old_pages = set(range(16))
    timeline = []

    def retained():
        return outstanding | (domain if protected else set())

    def apply(record):
        nonlocal protected, last_old_pages
        kind, cell = record
        if kind == "short":
            outstanding.remove(cell)
        else:
            protected = False
        work["shared_retirement_mutations" if retirement == "direct_shared" else "owner_retirement_mutations"] += 1
        pages = {old_page(cell) for cell in retained()}
        work["old_page_release_opportunities"] += len(last_old_pages - pages)
        last_old_pages = pages

    def flush(which, tick, reason, count=None):
        queue = queues[which]
        if not queue:
            return
        work["owner_service_batches"] += 1
        work["service_" + reason] += 1
        for _ in range(len(queue) if count is None else min(count, len(queue))):
            _, record = queue.popleft()
            apply(record)

    def notify(record, tick):
        nonlocal peak_queue_records
        work["completion_notifications"] += 1
        if retirement == "direct_shared":
            apply(record)
            return
        queue = queues[owner(record[1])]
        if len(queue) == queue_capacity:
            # Capacity service is a modeled progress obligation, not an
            # assumption that the remote producer mutates the owner's state.
            flush(owner(record[1]), tick, "queue_capacity")
        queue.append((tick, record))
        work["queue_record_enqueues"] += 1
        peak_queue_records = max(peak_queue_records, sum(map(len, queues.values())))
        if len(queue) >= batch_size:
            flush(owner(record[1]), tick, "size", batch_size)

    for tick in range(21):
        if 1 <= tick <= 4:
            for cell in CELLS:
                if 1 + (cell % GRID_WIDTH) % 4 == tick:
                    notify(("short", cell), tick)
        if tick == 12:
            notify(("domain", 0), tick)
        if retirement != "direct_shared":
            if age is not None:
                for which, queue in list(queues.items()):
                    if queue and tick - queue[0][0] >= age:
                        flush(which, tick, "age")
            if pressure and tick in (2, 6, 13):
                for which in list(queues):
                    flush(which, tick, "allocation_pressure")

        held = retained()
        # Independent backend oracle: retire accounting may lag completions,
        # but must never get ahead of an unfinished raw use.
        backend = {cell for cell in CELLS if tick < 1 + (cell % GRID_WIDTH) % 4}
        if tick < 12:
            backend.add(0)
        assert backend <= held, (group, protection, retirement, tick)
        pages = {old_page(cell) for cell in held}
        retained_bytes = len(held) * 64
        if tick < 20:
            retained_byte_ticks += retained_bytes
            retained_page_byte_ticks += len(pages) * PAGE
        peak_retained = max(peak_retained, retained_bytes)
        peak_total_capacity = max(peak_total_capacity, current_bytes + retained_bytes)
        peak_mapped_page_bytes = max(peak_mapped_page_bytes, (len(new_pages) + len(pages)) * PAGE)
        timeline.append({"tick": tick, "retained_capacity_bytes": retained_bytes,
                         "retained_old_page_bytes": len(pages) * PAGE,
                         "queued_records": sum(map(len, queues.values()))})

    idle_stuck = bool(outstanding or protected)
    if retirement == "owner_size_only":
        assert idle_stuck == (batch_size > 1)
        assert all(len(q) < batch_size for q in queues.values())
    else:
        assert not idle_stuck
    return {
        "owner_group_trampolines": group,
        "owner_groups": owner_count,
        "protection": protection,
        "protection_current_identities": len(domain),
        "retirement": retirement,
        "batch_size": batch_size if retirement != "direct_shared" else None,
        "batch_age_ticks": age if retirement != "direct_shared" else None,
        "configured_queue_record_capacity": queue_capacity * owner_count if retirement != "direct_shared" else 0,
        "current_capacity_bytes": current_bytes,
        "peak_retained_capacity_bytes": peak_retained,
        "peak_total_capacity_bytes": peak_total_capacity,
        "peak_mapped_page_coverage_bytes": peak_mapped_page_bytes,
        "retained_capacity_byte_ticks": retained_byte_ticks,
        "retained_old_page_byte_ticks": retained_page_byte_ticks,
        "final_retained_capacity_bytes": timeline[-1]["retained_capacity_bytes"],
        "final_queued_records": timeline[-1]["queued_records"],
        "outcome": "progress failure" if idle_stuck else "safe completion",
        "idle_progress": "no enabled size trigger remains" if idle_stuck else "all retired old homes released",
        "logical_work": dict(sorted(work.items())),
        "peak_queued_records": peak_queue_records,
        "selected_timeline": [timeline[t] for t in (0, 1, 4, 8, 12, 13, 15, 20)],
    }


@dataclass
class Home:
    capacity: int
    current: bool = True
    claims: int = 0
    backend_users: set[str] = field(default_factory=set)
    reclaimed: bool = False


@dataclass
class Ticket:
    home: str
    done: bool = False
    consumed: bool = False


class Ledger:
    """Authority's counts checked against independently tracked backend use."""
    def __init__(self, name):
        self.name = name
        self.homes = {}
        self.tickets = {}
        self.binding = {}
        self.trace = []
        self.work = Counter()
        self.errors = []
        self.peak_retained = 0
        self.peak_capacity = 0

    def mark(self, event):
        self.trace.append(event)
        for name, home in self.homes.items():
            if home.reclaimed and home.backend_users:
                error = f"reclaimed {name} while raw backend users remain"
                if error not in self.errors:
                    self.errors.append(error)
            if home.claims < 0:
                error = f"retirement accounting underflow for {name}"
                if error not in self.errors:
                    self.errors.append(error)
        self.peak_retained = max(self.peak_retained, self.retained())
        self.peak_capacity = max(self.peak_capacity, sum(
            h.capacity for h in self.homes.values() if not h.reclaimed))

    def add(self, name, capacity=64, head=None):
        assert name not in self.homes
        self.homes[name] = Home(capacity)
        if head is not None:
            self.binding[head] = name
        self.mark(f"allocate {name}" + (f"; head {head} binds it" if head else ""))

    def grant(self, token, home):
        assert not self.homes[home].reclaimed
        self.tickets[token] = Ticket(home)
        self.homes[home].claims += 1
        self.homes[home].backend_users.add(token)
        self.mark(f"establish {token} protection before publishing its raw reference")

    def retire(self, home):
        self.homes[home].current = False
        self.collect()
        self.mark(f"end usable descriptor aliases for {home}; raw grants are separate")

    def finish_backend(self, token):
        ticket = self.tickets[token]
        assert not ticket.done
        ticket.done = True
        self.homes[ticket.home].backend_users.remove(token)
        self.mark(f"backend {token} finishes all byte access")

    def collect(self):
        for home in self.homes.values():
            if not home.current and home.claims == 0:
                home.reclaimed = True

    def apply(self, token, *, deduplicate=True, current_head=None, require_done=True):
        ticket = self.tickets[token]
        if deduplicate and ticket.consumed:
            self.work["duplicate_records_ignored"] += 1
            self.mark(f"ignore duplicate {token} completion")
            return
        if require_done:
            assert ticket.done, f"{token}: completion needs actual byte-use proof"
        home = self.binding[current_head] if current_head is not None else ticket.home
        self.homes[home].claims -= 1
        ticket.consumed = True
        self.work["retirement_mutations"] += 1
        self.collect()
        self.mark(f"apply {token} completion to {home}")

    def service(self, queue):
        self.work["owner_service_batches"] += 1
        self.mark(f"owner service drains {len(queue)} completion records")
        while queue:
            self.apply(queue.popleft())

    def retained(self):
        return sum(h.capacity for h in self.homes.values() if not h.current and not h.reclaimed)

    def result(self, failure=None):
        if failure is None:
            assert not self.errors, (self.name, self.errors)
            assert all(h.reclaimed for h in self.homes.values()), self.name
        elif failure == "safety":
            assert self.errors, self.name
        else:
            assert not self.errors and self.retained() > 0, self.name
            assert not any(h.backend_users for h in self.homes.values()), self.name
        return {"case": self.name, "outcome": failure or "safe completion",
                "trace": self.trace, "errors": self.errors,
                "final_retained_capacity_bytes": self.retained(),
                "peak_retained_capacity_bytes": self.peak_retained,
                "peak_capacity_bytes": self.peak_capacity,
                "logical_work": dict(self.work)}


def identity_history(stable):
    l = Ledger("late_completion_bound_ticket" if stable else "bad_late_completion_resolves_reused_head")
    l.add("old", head="slot")
    l.grant("old-send", "old")
    l.retire("old")
    l.add("new", head="slot")
    l.grant("new-send", "new")
    l.retire("new")
    l.finish_backend("old-send")
    l.apply("old-send", current_head=None if stable else "slot")
    if not stable:
        return l.result("safety")
    l.finish_backend("new-send")
    l.apply("new-send")
    return l.result()


def cancellation_history(completion_proof):
    l = Ledger("cancellation_waits_for_backend_completion" if completion_proof else "bad_cancel_is_completion")
    l.add("payload")
    l.grant("send", "payload")
    l.retire("payload")
    l.mark("request cancellation; backend may still access payload")
    if not completion_proof:
        l.apply("send", require_done=False)
        return l.result("safety")
    l.finish_backend("send")
    l.apply("send")
    return l.result()


def duplicate_history(deduplicate):
    l = Ledger("duplicate_ticket_consumed_once" if deduplicate else "bad_duplicate_consumes_another_grant")
    l.add("payload")
    l.grant("done-reader", "payload")
    l.grant("slow-reader", "payload")
    l.retire("payload")
    l.finish_backend("done-reader")
    l.apply("done-reader")
    l.apply("done-reader", deduplicate=deduplicate)
    if not deduplicate:
        return l.result("safety")
    l.finish_backend("slow-reader")
    l.apply("slow-reader")
    return l.result()


def lost_history(reconcile):
    l = Ledger("lost_notification_reconciled_from_completion_authority" if reconcile else "lost_notification_strands_retirement")
    l.add("payload")
    l.grant("send", "payload")
    l.retire("payload")
    l.finish_backend("send")
    l.mark("drop queue notification after backend completion")
    if not reconcile:
        l.mark("owner idle; no retry, reliable notification or reconciliation remains")
        return l.result("progress")
    # This comparator explicitly assumes a separately observable completion
    # authority.  A timeout/absence of notification supplies no such proof.
    for token, ticket in l.tickets.items():
        l.work["completion_authority_checks"] += 1
        if ticket.done and not ticket.consumed:
            l.apply(token)
    return l.result()


def unsafe_timeout_history():
    l = Ledger("bad_missing_notification_timeout_assumes_completion")
    l.add("payload")
    l.grant("send", "payload")
    l.retire("payload")
    l.mark("no completion notification arrives; backend remains live")
    l.mark("timeout expires without backend completion authority")
    l.apply("send", require_done=False)
    return l.result("safety")


def idle_history(service):
    l = Ledger("partial_batch_idle_owner_has_age_service" if service else "bad_partial_batch_size_only_idle_owner")
    l.add("payload")
    l.grant("send", "payload")
    l.retire("payload")
    l.finish_backend("send")
    queue = deque(["send"])
    l.mark("enqueue one record; batch threshold is four")
    l.mark("initiator and owner become idle; no more allocation or arrivals")
    if not service:
        l.mark("size threshold cannot fire; no enabled drain remains")
        return l.result("progress")
    l.mark("bounded batch age requests owner service; owner is scheduled")
    l.service(queue)
    return l.result()


def pressure_history(service):
    l = Ledger("full_queue_pressure_services_retirements" if service else "bad_full_queue_allocation_wait_cycle")
    queue = deque()
    for i in range(3):
        home, token = f"home-{i}", f"use-{i}"
        l.add(home)
        l.grant(token, home)
        l.retire(home)
        l.finish_backend(token)
        if len(queue) == 2:
            l.mark("retirement queue full; next allocation needs one of these homes")
            if not service:
                l.mark("producer waits for queue space; owner waits for allocation before draining")
                return l.result("progress")
            l.mark("capacity/pressure path schedules owner drain without allocating more storage")
            l.service(queue)
            assert any(h.reclaimed for h in l.homes.values())
            l.mark("allocation can now reuse a released home")
        queue.append(token)
        l.mark(f"enqueue {token}")
    l.service(queue)
    return l.result()


def handoff_history(mode):
    good = mode != "abandon_old_owner"
    l = Ledger("handoff_" + mode)
    old_queue, new_queue = deque(), deque()
    for i in range(2):
        l.add(f"home-{i}")
        l.grant(f"use-{i}", f"home-{i}")
        l.retire(f"home-{i}")
    l.finish_backend("use-0")
    if mode == "direct_shared":
        l.apply("use-0")
    else:
        old_queue.append("use-0")
        l.mark("first completion is queued at old owner")
    l.mark("handoff allocation authority to new owner")
    if mode == "transfer_and_route":
        new_queue.extend(old_queue)
        old_queue.clear()
        l.mark("transfer pending records and publish route for late completions")
    l.finish_backend("use-1")
    if mode == "direct_shared":
        l.apply("use-1")
    elif good:
        new_queue.append("use-1")
        l.service(new_queue)
    else:
        old_queue.append("use-1")
        l.mark("late completion follows stale old-owner route; old owner is never serviced")
        l.service(new_queue)
    return l.result(None if good else "progress")


def region_history(mode):
    l = Ledger("scratch_region_" + mode)
    l.add("region", 512)
    if mode == "promote_before_handoff":
        l.add("escape", 64)
        l.work["copied_bytes"] += 64
        l.mark("copy escaping bytes before publishing any raw reference")
        l.grant("callback", "escape")
        l.retire("region")
        l.retire("escape")
    else:
        l.grant("callback", "region")
        l.retire("region")
        if mode == "reset_on_task_end":
            l.homes["region"].reclaimed = True
            l.mark("reset region at initiating task completion despite live callback")
            return l.result("safety")
        if mode == "copy_after_escape_and_reset":
            l.add("escape", 64)
            l.work["copied_bytes"] += 64
            l.mark("copy bytes after callback already holds original raw address")
            l.homes["region"].reclaimed = True
            l.mark("reset original region; copying did not redirect existing raw grant")
            return l.result("safety")
    retained = l.retained()
    l.work["retained_capacity_byte_ticks"] = retained * 3
    l.mark("three logical ticks pass while asynchronous callback retains its original grant")
    l.finish_backend("callback")
    l.apply("callback")
    return l.result()


def run() -> dict:
    matrix = []
    for group in (16, 64, 256, 1024):
        for protection in ("home", "grid", "group"):
            matrix.append(matrix_case(group, protection, "direct_shared"))
            for batch_size in (1, 8, 32):
                for retirement in ("owner_size_only", "owner_size_age", "owner_size_age_pressure"):
                    matrix.append(matrix_case(group, protection, retirement, batch_size))
    histories = [
        identity_history(True), identity_history(False),
        cancellation_history(True), cancellation_history(False),
        duplicate_history(True), duplicate_history(False),
        lost_history(False), lost_history(True), unsafe_timeout_history(),
        idle_history(False), idle_history(True),
        pressure_history(False), pressure_history(True),
        handoff_history("direct_shared"), handoff_history("transfer_and_route"), handoff_history("abandon_old_owner"),
        region_history("retain_region"), region_history("promote_before_handoff"),
        region_history("reset_on_task_end"), region_history("copy_after_escape_and_reset"),
    ]
    return {
        "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "kind": "deterministic bounded policy histories; logical counters and modeled capacity/page coverage",
        "matrix_inputs": {
            "grids": GRID_COUNT, "trampolines_per_grid": GRID_WIDTH,
            "current_identities_per_grid": 7, "current_identities": len(CELLS),
            "old_capacity": 64, "new_capacity": 128, "page_size": PAGE,
            "packing": "class-major old/new ranges; grid pitches 1024/2048 bytes; no relocation across grids",
            "short_completion_ticks": "1+(slot%4)", "long_completion_tick": 12,
            "last_observation_tick": 20, "batch_sizes": [1, 8, 32], "queue_capacity": 32,
            "age_ticks": 3, "allocation_pressure_ticks": [2, 6, 13],
            "notification_locality": "all remote; no assumed frequency or core cost",
        },
        "fault_bounds": {
            "pressure": "three reusable homes under a three-home capacity budget; queue capacity two",
            "idle": "one completion; batch threshold four; no future allocations/arrivals",
            "handoff": "two grants, one queued before handoff and one completed afterward",
            "region": "512-byte region, 64-byte escaping value, three logical hold ticks",
            "other": "one/two allocation identities and one/two raw users",
        },
        "minimum_obligations": [
            "establish protection before publishing remote byte access",
            "completion releases its retained home/domain, independent of reused descriptor identity",
            "backend completion proof is separate from cancellation, timeout or absent notification",
            "each completion is applied once, or duplicates are harmless",
            "loss needs proved reliable delivery or an authoritative completion/reconciliation path",
            "partial batches and idle owners need bounded service independent of future allocation",
            "queue-pressure service must not allocate resources released by the blocked completions",
            "handoff transfers outstanding authority and late-completion routing together",
            "page arbitration covers homes from every overlapping owner and protection domain",
            "scratch reset follows all escaped users; pre-handoff promotion cannot be replaced by late copying",
        ],
        "limits": [
            "not exhaustive interleavings, not an unbounded proof; scripted faults are independently checked against backend-use state",
            "logical ticks model event/age ordering, not seconds; work counts do not imply CPU cost rankings",
            "page coverage follows the stated inventory; not RSS, committed backing, COW amplification or syscall timings",
            "capacity totals exclude metadata; queue/ticket resources are counted as records and groups without assuming their byte layout",
            "queue enqueues count logical records, not queue publications or cache-line transfers; transport batching remains unpriced",
            "initial move/copy inventory is identical; allocation lookup, refill and access costs remain unmeasured",
            "direct shared retirement is modeled as serialized safe shared mutation, not an implemented lock-free protocol",
            "batched owners receive scheduled service when a modeled trigger fires; the runtime must actually supply that progress",
            "protection widths are independent alternatives; group affinity does not require group-wide protection",
            "reconciliation comparator assumes separately observable actual completion authority; it does not infer completion from silence",
            "logical/MVCC/recovery retention is assumed discharged for these retired homes; actual reuse also needs that authority",
            "virtual placement and production interfaces remain open",
        ],
        "matrix": matrix,
        "fault_histories": histories,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    if args.output:
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({
        "matrix_cases": len(result["matrix"]),
        "scripted_histories": len(result["fault_histories"]),
        "safety_counterexamples": sum(h["outcome"] == "safety" for h in result["fault_histories"]),
        "progress_counterexamples": sum(h["outcome"] == "progress" for h in result["fault_histories"]),
        "safe_histories": sum(h["outcome"] == "safe completion" for h in result["fault_histories"]),
        "output": str(args.output) if args.output else None,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
