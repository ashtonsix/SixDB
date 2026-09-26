#!/usr/bin/env python3
"""Charge conservative output coverage through the existing fixed protocol.

The programs and actual writes stay identical while only admitted potential
outputs change. Finite authored keys represent envelopes; this does not price a
compressed range implementation or discover SQL outputs. It isolates the cost
of overclaiming and keeps source-only and outside-target traffic distinguishable.
"""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from comparison_inputs import _case, _transaction
from fixed_simulation import FixedSimulation, source_identity as fixed_identity


class EnvelopeSimulation(FixedSimulation):
    def __init__(self, case):
        super().__init__(case, "fixed-known")

    def begin_evaluation(self, tx, fast=False):
        work = self.output_work[tx.spec["id"]]
        if tx.spec["writes"] and not work.refreshing:
            coverage = tuple(sorted(tx.spec.get("envelope", tx.spec["writes"])))
            assert set(tx.spec["writes"]) <= set(coverage)
            if not fast or set(coverage) != set(tx.spec["writes"]):
                work.gate_keys = coverage
                work.gate_groups = self.group_keys(coverage)
                work.gate_part = 0
                tx.phase = "output_admission"
                self.next_output_group(tx)
                return
        super().begin_evaluation(tx, fast)

    def run(self):
        result = super().run()
        result["policy"] = "fixed-envelope"
        return result


def workload(mode, width=16):
    assert mode in ("exact", "target", "shard-negative-control")
    source = [f"eu/source/{n:03d}" for n in range(width)]
    target = [f"eu/target/{n:03d}" for n in range(width)]
    outside = [f"eu/outside/{n:03d}" for n in range(width)]
    initial = {key: 0 for key in source + target + outside}
    initial["us/input"] = 1
    coverage = ([target[0]] if mode == "exact" else target if mode == "target"
                else source + target + outside)
    txs = [_transaction("slow", 0, "slow", "eu", ["eu/sources", "us/input"],
                        [target[0]], "max", delay=150, envelope=coverage)]
    for number in range(96):
        for group, keys in (("source_writers", source), ("other_target_writers", target[1:]),
                            ("outside_writers", outside)):
            key = keys[number % len(keys)]
            txs.append(_transaction(f"{group}-{number:03d}", 35 + 2 * number,
                                    group, "eu", [key], [key], "increment"))
    for number in range(24):
        key = target[1 + number % (width - 1)]
        txs.append(_transaction(f"lookup-{number:03d}", 40 + 5 * number,
                                "other_target_readers", "eu", [key], [], "report"))
    return _case("conservative-output-coverage", "Same max/source program and actual target; varied envelope.",
                 initial, {"eu/sources": source, "eu/targets": target}, txs,
                 horizon=1600, read_wait_ticks=4000, link_delay=20, capacity=32)


def checks():
    case = workload("exact", 4)
    adapted = EnvelopeSimulation(deepcopy(case)).run()
    direct = FixedSimulation(deepcopy(case), "fixed-known").run()
    for field in ("trace_sha256", "logical_state_sha256", "cohorts", "counts"):
        assert adapted[field] == direct[field], field
    broad = EnvelopeSimulation(workload("target", 4))
    result = broad.run()
    assert all(row["complete"] == row["offered"] for row in result["cohorts"].values())
    assert not broad.store.claims and not broad.admission.grants
    assert not result["certification"].get("renew_calls", 0)
    owner = broad.tx["slow"].attempt.owner
    assert len(broad.store.coverage[owner]) == 4
    assert set(broad.store.attempts[owner].writes) == {"eu/target/000"}
    assert broad.store.resolved[owner] == broad.store.coverage[owner]
    return {"passed": True, "checks": ["exact-envelope backend equivalence", "serial program replay",
            "excess coverage creates no fake writes", "all coverage resolved", "no renewal"]}


def identity():
    return {**fixed_identity(), "envelope_locality.py": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before = identity()
    validation = checks()
    rows = []
    for width in (4, 16, 64):
        for mode in ("exact", "target", "shard-negative-control"):
            case = workload(mode, width)
            sim = EnvelopeSimulation(case)
            row = sim.run()
            row.update(width=width, variant=mode, input=case)
            rows.append(row)
            assert all(c["complete"] == c["offered"] for c in row["cohorts"].values())
            print(width, mode, {g: c["p99_ticks"] for g, c in row["cohorts"].items()}, flush=True)
    assert before == identity()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"source_sha256": before, "checks": validation,
        "limits": ["Synthetic ticks and service units, not measured latency.",
                   "Finite envelopes include all potential keys; dynamic program semantics are probed separately.",
                   "Actual writes and traffic identical per width; only slow transaction coverage changes.",
                   "Long capture cutoff separates exclusion costs from deadline failures.",
                   "A real interval representation need not charge per-covered-key metadata as this adapter does."],
        "rows": rows}, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
