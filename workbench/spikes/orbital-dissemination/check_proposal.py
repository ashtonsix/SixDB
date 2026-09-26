#!/usr/bin/env python3
"""Causal observations, frozen proposal choices, retained gaps and receipt cost."""

import argparse
import hashlib
import json
import math
from pathlib import Path

from experiments import network, run_writes
from proposal_study import Timing, run
from simulator import Sim


def policy(config=None):
    sim = Sim()
    net = network(sim, {"p0": "a", "l0": "a", "p1": "a"}, {})
    return sim, Timing(sim, net, config or {})


def check_fixed_modes_equal_unobserved_baselines():
    # This compares all outcomes, traces, bytes and resource queues. Fixed modes
    # must not silently add the adaptive policy's payload-receipt traffic.
    for incident in ({}, {"faults": [dict(kind="partition", src="p0_p0", dst=w,
                                        at_us=0, until_us=300)
                                    for w in ("f0", "s0")]}):
        for mode, admission in (("strict", "strict"), ("early", "pipelined_candidate")):
            config = dict(count=12, rate=100000, producers=2, consumers=2,
                          timing_policy=mode, admission=admission, **incident)
            observed_wrapper = run(config)
            baseline = run_writes(config)
            assert observed_wrapper == baseline
            assert "proposal_observations" not in observed_wrapper


def check_cooldown_does_not_erase_an_unanswered_sample():
    sim, timing = policy(dict(observe_after_us=30, cooldown_us=50))
    assert timing.begin("p0", 0)
    sim.run(81)  # Timeout's cooldown has expired; no receipt has arrived.
    assert not timing.begin("p0", 1)
    timing.receipt("p0", 0)  # Late receipt is a slow sample, not instant recovery.
    assert timing.strict_until["p0"] == 131
    sim.run(82)
    timing.receipt("p0", 1)
    sim.run(132)
    assert timing.begin("p0", 2)


def check_duplicate_backup_receipt_cannot_refresh_a_slow_alarm():
    sim, timing = policy(dict(observe_after_us=30, cooldown_us=50))
    assert timing.begin("p0", 0)
    sim.run(5)
    timing.receipt("p0", 0)
    sim.run(100)
    timing.receipt("p0", 0)  # Slower follower reports the already seen operation.
    assert timing.count["observed_first_receipts"] == 1
    assert timing.count["missing_payload_receipt"] == 0
    assert timing.count["observed_slow_receipt"] == 0
    assert timing.begin("p0", 1)


def check_unanswered_window_and_producer_isolation():
    sim, timing = policy(dict(early_window=1, observe_after_us=300))
    assert timing.begin("p0", 0)
    assert not timing.begin("p0", 1)
    assert timing.begin("p1", 2)  # Another producer's window is independent.
    timing.receipt("p0", 0)
    timing.receipt("p0", 1)
    assert timing.begin("p0", 3)
    timing.alarm("p0", "authored_local_observation")
    timing.receipt("p1", 2)
    assert timing.begin("p1", 4)


def check_fault_declarations_are_not_local_observations():
    class UnreadableRemoteState:
        def __iter__(self):
            raise AssertionError("policy inspected unobserved fault declarations")

    # The network is deliberately identical. Only controller configuration has
    # inaccessible future/remote information; the controller may not inspect it.
    sim, timing = policy(dict(faults=UnreadableRemoteState(), observe_after_us=30))
    assert timing.begin("p0", 0)
    sim.run(29)
    assert not timing.events
    timing.receipt("p0", 0)
    sim.run(31)
    assert timing.begin("p0", 1)


def check_observation_capacity_refuses_samples_without_refusing_work():
    sim, timing = policy(dict(sample_limit=2, observe_after_us=30))
    choices = [timing.begin("p0", key) for key in range(100)]
    assert choices[:2] == [True, True] and not any(choices[2:])
    assert len(timing.pending["p0"]) == 2
    assert timing.count["watch_sample_refused"] == 98
    sim.run(31)
    assert timing.count["missing_payload_receipt"] == 2
    timing.receipt("p0", 99)  # An untracked reply cannot invent an observation.
    assert len(timing.pending["p0"]) == 2
    assert timing.count["observed_first_receipts"] == 0
    result = run(dict(count=3, rate=5000, consumers=2, sample_limit=1,
                      observe_after_us=1000000, max_retries=0))
    assert result["completed"] == 3
    assert result["proposal_mode_counts"] == {"pipelined_candidate": 2, "strict": 1}
    counts = result["proposal_observations"]["counters"]
    assert counts["watch_sample_refused"] == 1 and counts["observed_first_receipts"] == 2
    assert result["proposal_observations"]["pending_receipts"] == {"p0": 0}


def check_warning_requires_network_delivery_and_reaches_single_producer():
    for lost in (False, True):
        sim = Sim()
        net = network(sim, {"l0": "a", "p0": "a"}, dict(
            max_retries=0,
            faults=[dict(kind="partition", src="l0", dst="p0", at_us=0)] if lost else []))
        timing = Timing(sim, net, dict(warnings=[0], cooldown_us=100))
        assert timing.begin("p0", 0)
        sim.run(5)
        assert timing.count["received_warning"] == 0
        sim.run(20)
        assert timing.count["received_warning"] == (0 if lost else 1)
        assert timing.begin("p0", 1) is lost
        assert sim.count["wire_bytes"] > 0  # Even a lost warning costs traffic.


def check_warning_targets_active_multi_producers_only():
    sim = Sim()
    names = {"l0": "a", "p0": "a", "p1": "a"}
    names.update({f"p{s}_p{i}": "a" for s in range(2) for i in range(2)})
    net = network(sim, names, {})
    timing = Timing(sim, net, dict(warnings=[0], producers=2, shards=2))
    sim.run(30)
    assert timing.count["received_warning"] == 4
    assert set(timing.strict_until) == {f"p{s}_p{i}" for s in range(2) for i in range(2)}


def check_switch_to_strict_does_not_retract_existing_early_work():
    result = run(dict(count=3, rate=20000, consumers=2, observe_after_us=25,
                      cooldown_us=200, max_retries=0))
    first, second = result["traces"]["0"], result["traces"]["1"]
    first_alarm = result["proposal_observations"]["events"][0]["at_us"]
    assert result["proposal_mode_counts"] == {"pipelined_candidate": 1, "strict": 2}
    assert first["leader_journal"] < first_alarm < first["payload:f0"]
    assert first["effect"] > first_alarm
    assert "proposal_mode:strict" not in first
    assert second["leader_request"] > second["payload:f0"]
    assert result["completed"] == result["all_consumers"]["completed"] == 3


def check_inflight_strict_request_stays_strict_after_return_to_early():
    # The first request is strict after an actually delivered warning. Its
    # payload receipt is still in flight when the second operation begins early.
    result = run(dict(count=2, rate=20000, consumers=2, warnings=[0],
                      cooldown_us=20, observe_after_us=1000000, max_retries=0))
    first, second = result["traces"]["0"], result["traces"]["1"]
    assert "proposal_mode:strict" in first
    assert "proposal_mode:pipelined_candidate" in second
    assert second["leader_request"] < first["payload:f0"] < first["leader_request"]
    assert first["leader_request"] - first["payload:f0"] >= 100
    assert result["completed"] == 2


def check_lost_early_payload_keeps_prefix_hole_after_switch():
    # Partition covers arrival of the first operation's two payloads. Later
    # traffic is healthy and strict. No retry supplies the missing predecessor.
    result = run(dict(count=3, rate=4000, consumers=2, observe_after_us=25,
                      cooldown_us=200, max_retries=0,
                      faults=[dict(kind="partition", src="p0", dst=w,
                                   at_us=0, until_us=200) for w in ("f0", "s0")]))
    assert result["proposal_mode_counts"] == {"pipelined_candidate": 1, "strict": 2}
    assert result["payload_ready"]["completed"] == 2
    assert result["completed"] == result["all_consumers"]["completed"] == 0
    assert result["journal_ready_behind_gap"] == 4  # Two entries at two witnesses.
    assert result["pending_leader_journal_bytes"] == 3 * 128
    assert "journal:f0" in result["traces"]["0"]
    assert "payload:f0" not in result["traces"]["0"]
    for key in ("1", "2"):
        assert "eligible:f0" in result["traces"][key]
        assert "admitted:f0" not in result["traces"][key]


def check_remote_persistence_does_not_replace_lost_receipts():
    result = run(dict(count=2, rate=2000, consumers=2, observe_after_us=300,
                      cooldown_us=50, max_retries=0,
                      faults=[dict(kind="partition", src=w, dst="p0", at_us=0)
                              for w in ("f0", "s0")]))
    assert result["payload_ready"]["completed"] == 2
    assert result["completed"] == 1  # Existing early work may still take effect.
    assert result["proposal_mode_counts"] == {"pipelined_candidate": 1, "strict": 1}
    assert result["proposal_observations"]["counters"].get("observed_first_receipts", 0) == 0
    assert result["proposal_observations"]["pending_receipts"] == {"p0": 2}
    assert "leader_request" not in result["traces"]["1"]
    first_alarm = result["proposal_observations"]["events"][0]["at_us"]
    assert result["traces"]["0"]["effect"] < first_alarm


def check_adaptive_receipts_and_their_acks_are_charged():
    config = dict(count=3, rate=5000, consumers=2, observe_after_us=1000000,
                  max_retries=0)
    early = run(dict(config, timing_policy="early"))
    adaptive = run(dict(config, timing_policy="adaptive"))
    assert adaptive["proposal_mode_counts"] == {"pipelined_candidate": 3}
    assert early["completed"] == adaptive["completed"] == 3
    assert adaptive["proposal_observations"]["counters"]["observed_first_receipts"] == 3
    # Each of two followers sends 40-byte application receipt + 16-byte framing
    # + 80-byte packet header, then a 24+16+80-byte transport ACK. Count slower
    # duplicate observations' full traffic even though only the first is used.
    extra = 3 * 2 * ((40 + 16 + 80) + (24 + 16 + 80))
    assert math.isclose(adaptive["counters"]["wire_bytes"] - early["counters"]["wire_bytes"],
                        extra, abs_tol=1e-6)
    assert adaptive["counters"]["packets"] - early["counters"]["packets"] == 12
    assert adaptive["counters"]["persisted_bytes"] == early["counters"]["persisted_bytes"]
    cpu = lambda row: sum(r["service_us"] for name, r in row["resources"].items()
                          if name.endswith(":cpu"))
    assert cpu(adaptive) > cpu(early)


def source_hashes():
    root = Path(__file__).resolve().parent
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in ("check_proposal.py", "proposal_study.py", "experiments.py",
                         "simulator.py", "routing.py")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    before = source_hashes()
    passed = []
    for name, check in sorted(globals().copy().items()):
        if name.startswith("check_") and callable(check):
            check()
            passed.append(name.removeprefix("check_"))
    if before != source_hashes():
        raise RuntimeError("source changed during proposal checks")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(dict(checks=passed, source_sha256=before), indent=2) + "\n")
    print(f"{len(passed)} proposal timing checks passed")


if __name__ == "__main__":
    main()
