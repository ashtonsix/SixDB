#!/usr/bin/env python3
"""Network-backed join fault histories; completion and resource bounds matter."""

import argparse
import hashlib
import json
from pathlib import Path

from membership_scenario import run_membership


BASE = dict(count=80, rate=50000, size=256, join_us=90, snapshot_bytes=16384,
            snapshot_chunk_bytes=2048, snapshot_window=4, repair_window=8,
            repair_poll_us=40, application_retry_us=400, application_max_attempts=16,
            retry_us=100, max_retries=2, cross_az_us=25, retention_events=128,
            drain_us=6000, queue_bytes=65536)


def check_join_is_owed_tail_despite_old_route():
    result = run_membership(BASE)
    assert result["old_route"] == ["p", "c"]
    assert 0 < result["join_barrier"] < result["accepted_offers"]
    assert result["final"]["join_ready"]
    assert result["final"]["join_acked_through"] == result["accepted_offers"]
    assert result["final"]["retained_events"] == 0
    assert result["counters"]["application_repair_payload_bytes"] > 0
    assert result["counters"]["application_snapshot_payload_bytes"] >= BASE["snapshot_bytes"]
    assert result["foreground_durable_delivery"]["completed"] == BASE["count"]
    return result


def check_timed_partition_repairs_from_missing_ack():
    result = run_membership({**BASE, "faults": [dict(kind="partition", src="p", dst="j",
                              at_us=110, until_us=600)]})
    assert result["counters"]["lost_packets"] > 0
    assert result["counters"]["application_retries"] > 0
    assert result["final"]["join_ready"]
    assert result["foreground_durable_delivery"]["completed"] == BASE["count"]
    return result


def check_joiner_crash_preserves_only_completed_persistence():
    result = run_membership({**BASE, "faults": [dict(kind="crash", node="j",
                              at_us=180, until_us=700)]})
    assert result["counters"]["crashes"] == 1
    assert result["counters"]["application_retries"] > 0
    assert result["final"]["join_ready"]
    return result


def check_finite_retention_refuses_instead_of_silent_eviction():
    result = run_membership({**BASE, "retention_events": 12, "snapshot_bytes": 131072,
                             "snapshot_window": 1, "drain_us": 30000})
    assert result["refused_offers"] > 0
    assert result["highs"]["retained_events"] <= 12
    assert result["highs"]["source_credit_events"] <= 12
    assert result["accepted_offers"] + result["refused_offers"] == BASE["count"]
    assert result["foreground_durable_delivery"]["completed"] == result["accepted_offers"]
    assert result["final"]["join_ready"]
    return result


def check_permanent_reply_loss_keeps_unacknowledged_obligations():
    result = run_membership({**BASE, "retention_events": 16, "application_max_attempts": 3,
                             "faults": [dict(kind="partition", src="j", dst="p",
                                             at_us=0)]})
    assert not result["final"]["join_ready"]
    assert not result["final"]["snapshot_installed_ack"]
    assert result["final"]["retained_events"] > 0
    assert result["refused_offers"] > 0
    assert result["application_exhausted_obligations"] > 0
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = {}
    for name, check in sorted(globals().copy().items()):
        if name.startswith("check_") and callable(check):
            report = check()
            result[name.removeprefix("check_")] = {
                key: report[key] for key in ("accepted_offers", "refused_offers", "final",
                                             "highs", "application_exhausted_obligations")}
    print(f"{len(result)} network-backed membership checks passed")
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        directory = Path(__file__).resolve().parent
        sources = ("check_membership_scenario.py", "membership_scenario.py", "delivery.py",
                   "simulator.py", "experiments.py")
        output = dict(checks=result, source_sha256={
            name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
            for name in sources})
        args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(encoded, end="")


if __name__ == "__main__":
    main()
