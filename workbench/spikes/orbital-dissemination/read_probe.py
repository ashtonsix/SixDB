#!/usr/bin/env python3
"""Small analytical read/relay cases; no transport or latency simulator.

Times, rates and link prices below are authored inputs, not measured estimates.
The network simulator owns queues, packets, faults and the durable write path.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path


@dataclass(frozen=True)
class Worker:
    name: str
    ready_us: float
    bytes_per_us: float


def read_split(data_bytes: int, workers: list[Worker]) -> dict:
    """Compare equal pieces with an optimistic divisible-work completion bound.

    Workers have independent constant service rates and retain the same cut.
    The bound permits fractional bytes, instantaneous dispatch and no overhead.
    """
    if data_bytes <= 0 or not workers:
        raise ValueError("positive data and at least one worker required")
    if len({w.name for w in workers}) != len(workers):
        raise ValueError("worker names must be distinct")
    if any(w.ready_us < 0 or w.bytes_per_us <= 0 for w in workers):
        raise ValueError("invalid worker availability or rate")
    whole = min(w.ready_us + data_bytes / w.bytes_per_us for w in workers)
    equal = max(w.ready_us + data_bytes / len(workers) / w.bytes_per_us for w in workers)
    low, high = 0.0, whole
    for _ in range(80):
        middle = (low + high) / 2
        done = sum(max(0, middle - w.ready_us) * w.bytes_per_us for w in workers)
        if done >= data_bytes:
            high = middle
        else:
            low = middle
    allocation = {
        w.name: max(0, high - w.ready_us) * w.bytes_per_us for w in workers
    }
    return {
        "data_bytes": data_bytes,
        "workers": [asdict(w) for w in workers],
        "best_whole_replica_us": whole,
        "equal_partition_us": equal,
        "divisible_work_lower_bound_us": high,
        "bound_allocation_bytes": allocation,
        "partition_scan_bytes": data_bytes,
        "all_replicas_full_scan_bytes": data_bytes * len(workers),
    }


def fanout_tail(recipients: int, single_success: float, target: float) -> dict:
    if recipients <= 0 or not 0 <= single_success <= 1 or not 0 < target <= 1:
        raise ValueError("invalid fanout or probability")
    return {
        "recipients": recipients,
        "single_success_probability": single_success,
        "independent_all_success_probability": single_success**recipients,
        "target_all_success_probability": target,
        "required_independent_single_success_probability": target ** (1 / recipients),
        "union_bound_single_failure_budget": (1 - target) / recipients,
    }


def hedge_work(primary_us: float, secondary_us: float, launch_us: float, cancel_delay_us: float) -> dict:
    """Two dedicated workers with fixed CPU demands, no queues or preemption cost.

    Delayed cancellation keeps consuming service after the first answer.
    Demand is elapsed CPU service here; queue or network delay is not CPU work.
    """
    if min(primary_us, secondary_us) <= 0 or min(launch_us, cancel_delay_us) < 0:
        raise ValueError("invalid hedge work or delay")
    if primary_us <= launch_us:
        return {"completion_us": primary_us, "cpu_us": primary_us, "launched": False}
    finish = min(primary_us, launch_us + secondary_us)
    primary_spent = min(primary_us, finish + cancel_delay_us)
    secondary_spent = min(secondary_us, finish + cancel_delay_us - launch_us)
    return {
        "completion_us": finish,
        "cpu_us": primary_spent + secondary_spent,
        "launched": True,
        "primary_cpu_us": primary_spent,
        "secondary_cpu_us": secondary_spent,
    }


@dataclass(frozen=True)
class Recipient:
    name: str
    epoch_ready_us: float
    payload_ready_us: float
    relay_to_receiver_us: float
    edge_bytes_per_us: float
    edge_price_weight: float


def relay_analysis(
    recipients: list[Recipient],
    relay_epoch_us: float,
    relay_payload_us: float,
    analysis_us: float,
    verification_us: float,
    hint_bytes: int,
) -> dict:
    """Raw input and independently propagated hint arrival comparison.

    Raw arrivals are specified per recipient, not inferred from edge latency.
    Hint serialization is an incremental lower bound: no framing, queuing,
    memory movement, output expansion or shared sender resource is included.
    """
    if not recipients or min(relay_epoch_us, relay_payload_us, analysis_us, verification_us, hint_bytes) < 0:
        raise ValueError("nonnegative costs and recipients required")
    if any(min(r.epoch_ready_us, r.payload_ready_us, r.relay_to_receiver_us, r.edge_price_weight) < 0 or r.edge_bytes_per_us <= 0 for r in recipients):
        raise ValueError("invalid recipient")
    hint_ready = max(relay_epoch_us, relay_payload_us) + analysis_us
    rows = []
    for recipient in recipients:
        raw_ready = max(recipient.epoch_ready_us, recipient.payload_ready_us)
        local_finish = raw_ready + analysis_us
        hint_arrival = hint_ready + recipient.relay_to_receiver_us + hint_bytes / recipient.edge_bytes_per_us
        hinted_finish = max(raw_ready, hint_arrival) + verification_us
        rows.append({
            **asdict(recipient),
            "local_analysis_finish_us": local_finish,
            "wait_for_hint_finish_us": hinted_finish,
            "optional_hint_completion_lower_bound_us": min(local_finish, hinted_finish),
            "hint_extra_serialization_us": hint_bytes / recipient.edge_bytes_per_us,
            "hint_weighted_byte_cost": hint_bytes * recipient.edge_price_weight,
        })
    return {
        "inputs": {
            "relay_epoch_us": relay_epoch_us,
            "relay_payload_us": relay_payload_us,
            "analysis_us": analysis_us,
            "verification_us": verification_us,
            "hint_bytes": hint_bytes,
        },
        "local_analysis_cpu_us": len(recipients) * analysis_us,
        "shared_analysis_cpu_us": analysis_us + len(recipients) * verification_us,
        "rows": rows,
    }


class Coverage:
    """A finite logical query domain; scheduling attempts are not new chunks."""

    def __init__(self, snapshot: str, plan: str, chunks: set[str]):
        if not chunks:
            raise ValueError("nonempty domain required")
        self.snapshot, self.plan, self.expected = snapshot, plan, frozenset(chunks)
        self.accepted: dict[str, str] = {}

    def accept(self, snapshot: str, plan: str, chunk: str, result_digest: str) -> bool:
        if (snapshot, plan) != (self.snapshot, self.plan) or chunk not in self.expected:
            raise ValueError("result does not belong to this query domain")
        if chunk in self.accepted:
            if self.accepted[chunk] != result_digest:
                raise ValueError("attempts disagree; duplicate suppression cannot choose truth")
            return False
        self.accepted[chunk] = result_digest
        return True

    @property
    def complete(self) -> bool:
        return self.accepted.keys() == self.expected


def examples() -> dict:
    workers = [Worker(f"replica-{i}", 0, 1000) for i in range(4)]
    slow_cut = workers[:3] + [Worker("replica-3", 400, 1000)]
    recipients = [
        Recipient("fast-AZ", 22, 12, 2, 12500, 1),
        Recipient("epoch-late-AZ", 60, 20, 20, 12500, 1),
        Recipient("public-edge", 60, 20, 20, 125, 100),
    ]
    coverage = Coverage("cut-17", "plan-4", {"shard-A/0", "shard-A/1", "shard-B/0"})
    first = coverage.accept("cut-17", "plan-4", "shard-A/0", "digest-0")
    duplicate = coverage.accept("cut-17", "plan-4", "shard-A/0", "digest-0")
    coverage.accept("cut-17", "plan-4", "shard-B/0", "digest-2")
    missing_chunk_blocks = not coverage.complete
    coverage.accept("cut-17", "plan-4", "shard-A/1", "digest-1")
    return {
        "kind": "authored_analytical_cases_not_measured_performance",
        "read_splits": [read_split(1_000_000, workers), read_split(1_000_000, slow_cut)],
        "fanout": [fanout_tail(n, .99, .999) for n in (1, 8, 32, 128, 1024)],
        "hedges": {
            "unnecessary_equal_workers": hedge_work(100, 100, 0, 0),
            "slow_primary_immediate_cancel": hedge_work(1000, 100, 50, 0),
            "slow_primary_late_cancel": hedge_work(1000, 100, 50, 200),
        },
        "relay": relay_analysis(recipients, 20, 10, 8, 1, 4096),
        "coverage": {
            "first_attempt_added": first,
            "duplicate_attempt_added": duplicate,
            "missing_chunk_blocks": missing_chunk_blocks,
            "complete_after_missing_chunk": coverage.complete,
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    content = json.dumps(examples(), indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content)
    else:
        print(content, end="")


if __name__ == "__main__":
    main()
