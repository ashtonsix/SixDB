#!/usr/bin/env python3
"""Apply the same bounded additive grouping to arbitration and wait-die.

This is the fairness control for fixed_fold_comparison.py: identical grouping,
original arrival accounting and original-member serial-block semantics, with
the frozen protection/arbitration backend. Only eligible cross-shard programs
enter windows; local work retains its original specification and arrival.
"""

import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from check_comparison import drive_until
from comparison import Simulation, digest, evaluate
from comparison_inputs import cases
from fixed_fold_comparison import identity as fixed_fold_identity, scoped_group_case
from fold_comparison import FoldSimulation, eligible


POLICIES = ("arbitration", "wait-die")
CASES = {"ordinary_distributed_hot", "ordinary_distributed_cold", "rare_wan_saturated_local"}
CONFIGURATIONS = ((None, 1), (25, 8), (128, 32))


def identity():
    root = Path(__file__).resolve().parent
    return {**fixed_fold_identity(), **{
        name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ("arbitration_fold_comparison.py", "check_comparison.py")}}


class ArbitrationFoldSimulation(FoldSimulation):
    def __init__(self, original, policy, window=None, cap=1):
        assert policy in POLICIES
        case, self.originals, self.members = scoped_group_case(
            original, window, cap, "cross-shard-only")
        self.original_case = deepcopy(original)
        self.window, self.cap = window, cap
        self.member_replay = []
        self.group_executions = Counter()
        Simulation.__init__(self, case, policy)

    def compute_values(self, tx):
        # The old backend has no Store attempt; effects remain private in
        # tx.writes until its existing decision/install path publishes them.
        Simulation.compute_values(self, tx)
        count = tx.spec.get("fold_count", 1)
        if count > 1:
            assert tx.spec["op"] == "increment" and tx.spec.get("delay", 0) == 0
            inputs = {key: value for observed in tx.captured.values() for key, value in observed.items()}
            tx.writes = {key: inputs[key] + count for key in tx.spec["writes"]}
        self.group_executions[tx.spec["id"]] += 1

    def check_serial(self):
        # Same member oracle as FoldSimulation, with the old backend's actual
        # data and outstanding committed write protection for the final check.
        state = dict(self.case["initial"])
        committed_originals = set()
        self.member_replay = []
        for record in sorted(self.decisions, key=lambda record: record["position"]):
            spec = self.tx[record["id"]].spec
            assert set(record["reads"]) == set(spec["reads"])
            assert set(record["writes"]) == set(spec["writes"])
            for scope, observed in record["reads"].items():
                assert observed == {key: state[key] for key in self.scopes[scope]}, (
                    self.case["name"], record["id"], scope)
            inputs = {key: value for observed in record["reads"].values() for key, value in observed.items()}
            count = spec.get("fold_count", 1)
            expected = ({key: inputs[key] + count for key in spec["writes"]}
                        if count > 1 else evaluate(spec, inputs))
            assert record["writes"] == expected
            for ordinal, member in enumerate(self.members[record["id"]]):
                assert member not in committed_originals
                original = self.originals[member]
                assert set(original["reads"]) == set(spec["reads"])
                assert set(original["writes"]) == set(spec["writes"])
                reads = {scope: {key: state[key] for key in self.scopes[scope]}
                         for scope in original["reads"]}
                member_inputs = {key: value for observed in reads.values() for key, value in observed.items()}
                writes = evaluate(original, member_inputs)
                state.update(writes)
                committed_originals.add(member)
                self.member_replay.append({"id": member, "batch": record["id"],
                                           "ordinal": ordinal, "reads": reads, "writes": writes})
            assert record["writes"] == {key: state[key] for key in spec["writes"]}

        overwritten = {key for spec in self.originals.values() if spec["op"] != "increment"
                       for key in spec["writes"]}
        contributions = Counter(key for member in committed_originals
                                if self.originals[member]["op"] == "increment"
                                for key in self.originals[member]["writes"])
        for key, count in contributions.items():
            if key not in overwritten:
                assert state[key] == self.case["initial"][key] + count
        if self.case["name"] == "ordinary_distributed_hot":
            count = sum(self.originals[member]["group"] == "distributed" for member in committed_originals)
            assert state["eu/distributed/000"] == state["us/distributed/000"] == count
        elif self.case["name"] == "ordinary_distributed_cold":
            for key in state:
                if key.startswith("eu/distributed/"):
                    assert state[key] == state[key.replace("eu/", "us/", 1)]

        pending = {key for claim in self.arb.grants.values()
                   if self.tx[claim.owner].state == "committed" for key in claim.writes}
        assert all(self.data[key] == value for key, value in state.items() if key not in pending)
        return digest(state)

    def run(self):
        result = super().run()
        result["grouping_scope"] = "cross-shard-only"
        result["program_execution_counts"] = dict(self.group_executions)
        return result


def check_ungrouped(case, policy, adapted):
    baseline = Simulation(deepcopy(case), policy).run()
    for key in ("trace_sha256", "logical_state_sha256", "counts", "arbitration",
                "certification", "write_admission", "time_ticks", "input_sha256"):
        assert baseline[key] == adapted[key], (case["name"], policy, key)
    assert baseline["cohorts"] == adapted["execution_cohorts"]
    for group, values in baseline["cohorts"].items():
        for key in ("offered", "complete", "failed", "pending", "attempts",
                    "p50_ticks", "p99_ticks", "oldest_pending_ticks"):
            assert values[key] == adapted["cohorts"][group][key], (case["name"], policy, group, key)


def probe():
    initial = {"eu/x": 0, "us/y": 0, "eu/local": 0}
    distributed = [{"id": f"member-{index}", "arrival": index,
                    "group": "increments", "coordinator": "eu",
                    "reads": ["eu/x", "us/y"], "writes": ["eu/x", "us/y"], "op": "increment"}
                   for index in range(4)]
    local = {"id": "local", "arrival": 1, "group": "regional", "coordinator": "eu",
             "reads": ["eu/local"], "writes": ["eu/local"], "op": "increment"}
    case = {"name": "old-fold-counter-check", "description": "Exact additive members and local control",
            "initial": initial, "scopes": {key: [key] for key in initial},
            "transactions": sorted(distributed + [local], key=lambda spec: (spec["arrival"], spec["id"])),
            "horizon": 500, "capacity": 16, "link_delay": 3,
            "retry_delay": 3, "retry_limit": 1, "arbitration_delay": 4, "collect_period": 5}
    for policy in POLICIES:
        simulation = ArbitrationFoldSimulation(case, policy, window=5, cap=8)
        self_spec = simulation.tx[local["id"]].spec
        assert self_spec == local
        drive_until(simulation, lambda: any(tx.state == "committed" for tx in simulation.tx.values()))
        assert simulation.check_serial() == digest({"eu/x": 4, "us/y": 4, "eu/local": 1})
        assert simulation.data["eu/x"] == simulation.data["us/y"] == 0
        drive_until(simulation, lambda: simulation.data["eu/x"] == 4 and simulation.data["us/y"] == 0)
        simulation.check_serial()  # One committed participant is still protected, not installed.
        record = next(item for item in simulation.decisions if item["id"] != local["id"])
        record["reads"]["eu/x"]["eu/x"] = 99
        try:
            simulation.check_serial()
        except AssertionError:
            pass
        else:
            raise AssertionError("oracle accepted a corrupted observed payload")
        record["reads"]["eu/x"]["eu/x"] = 0
        result = simulation.run()
        assert simulation.data == {"eu/x": 4, "us/y": 4, "eu/local": 1}
        assert result["cohorts"]["increments"]["complete"] == 4
        assert result["counts"]["compute_units"] == 11  # shared distributed 8 + local 3.
        assert sum(simulation.group_executions.values()) == 2
        assert result["committed_original_requests"] == 5
        assert not simulation.arb.grants
        for row in result["requests"]:
            assert row["latency"] == row["completion"] - row["arrival"]
        check_ungrouped(case, policy, ArbitrationFoldSimulation(case, policy).run())
    assert not eligible(distributed[0] | {"returns_value": True})
    assert not eligible(distributed[0] | {"condition": "value < 10"})
    return {"passed": True, "checks": ["additive payload without Store attempts",
            "independent original-member replay and contribution totals", "observed-payload corruption rejected",
            "committed but partly uninstalled outputs", "original arrival and local control preserved",
            "exact ungrouped backend equivalence", "conditional/value-result grouping exclusion"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--width", type=int, default=64)
    args = parser.parse_args()
    before = identity()
    checks = probe()
    rows = []
    for case in cases(seed=args.seed, width=args.width):
        if case["name"] not in CASES:
            continue
        for policy in POLICIES:
            baseline = None
            for window, cap in CONFIGURATIONS:
                row = ArbitrationFoldSimulation(case, policy, window, cap).run()
                if window is None:
                    check_ungrouped(case, policy, row)
                    baseline = row
                if case["name"] == "rare_wan_saturated_local":
                    for key in ("trace_sha256", "counts", "cohorts", "requests", "logical_state_sha256"):
                        assert row[key] == baseline[key], (policy, window, key)
                row.update(seed=args.seed, width=args.width, ungrouped_backend_equivalence=window is None)
                rows.append(row)
                print(f'{case["name"]}/{policy} window={window} cap={cap}: ' + ", ".join(
                    f'{name}={cohort["complete"]}/{cohort["offered"]}, failed={cohort["failed"]}, '
                    f'pending={cohort["pending"]}, p99={cohort["p99_ticks"]}'
                    for name, cohort in row["cohorts"].items()), flush=True)
    assert before == identity(), "sources changed during run; discard and rerun"
    result = {"source_sha256": before, "checks": checks,
              "parameters": vars(args) | {"output": str(args.output)},
              "contract": "The same completion-only, unconditional, identical-footprint additive groups as fixed_fold_comparison.py. Only cross-shard participants enter fixed arrival windows; local work remains unchanged. Original-member serial blocks and all original outcomes are checked in old backend decision order.",
              "cost": "One shared computation plus one synthetic service unit per extra contribution; original latency includes window and protocol waits. Existing arbitration/wait-die transport, control, restarts, protection and queue costs are unchanged.",
              "limits": "Synthetic fixed-window grouping, not native/adaptive folds or measured throughput. Group admission age starts at window seal. No per-member value results, conditions or changing footprints. Pending originals remain visible; completed-only latency and horizon-limited cost must be read with completion counts. The frozen old backend idealizes complete collectors and atomic verdict application; wait-die verdict restarts do not enforce the new backend's finite retry budget.",
              "rows": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(f"Wrote {len(rows)} arbitration fold controls to {args.output}")


if __name__ == "__main__":
    main()
