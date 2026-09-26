#!/usr/bin/env python3
"""Fixed arrival-window additive groups on fixed-position known-output execution.

Reuse the existing grouping contract, original-request accounting and independent
serial-block replay. Only completion-only increments with identical footprints
and coordinator can combine. This adapter is not an adaptive/native fold executor.
"""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from comparison import digest, shard
from comparison_inputs import cases
from fixed_simulation import FixedSimulation, source_identity as fixed_identity
from fold_comparison import CASES, FoldSimulation, eligible, group_case, identity as fold_identity


def identity():
    return {**fold_identity(), **fixed_identity(),
            "fixed_fold_comparison.py": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}


def cross_shard(spec, scopes):
    keys = set(spec["writes"])
    for scope in spec["reads"]:
        keys.update(scopes.get(scope, (scope,)))
    return len({spec["coordinator"], *(shard(key) for key in keys)}) > 1


def scoped_group_case(original, window, cap, scope):
    assert scope in ("all-eligible", "cross-shard-only")
    if scope == "all-eligible" or window is None:
        return group_case(original, window, cap)
    candidates = deepcopy(original)
    candidates["transactions"] = [spec for spec in candidates["transactions"]
                                  if eligible(spec) and cross_shard(spec, original["scopes"])]
    grouped, _, members = group_case(candidates, window, cap)
    selected = {spec["id"] for spec in candidates["transactions"]}
    for spec in original["transactions"]:
        if spec["id"] not in selected:
            grouped["transactions"].append(deepcopy(spec))
            members[spec["id"]] = [spec["id"]]
    grouped["transactions"].sort(key=lambda spec: (spec["arrival"], spec["id"]))
    originals = {spec["id"]: deepcopy(spec) for spec in original["transactions"]}
    assert sorted(member for group in members.values() for member in group) == sorted(originals)
    return grouped, originals, members


class FixedFoldSimulation(FoldSimulation, FixedSimulation):
    """FoldSimulation supplies replay/accounting; FixedSimulation runs the protocol.

The explicit initializer avoids FoldSimulation's snapshot-wait constructor.
Its run() calls through this MRO into the fixed backend's OutputSimulation.run,
which keeps program-execution accounting as well as original-member outcomes.
"""

    def __init__(self, original, window=None, cap=1, scope="all-eligible"):
        case, self.originals, self.members = scoped_group_case(original, window, cap, scope)
        self.original_case = deepcopy(original)
        self.window, self.cap = window, cap
        self.grouping_scope = scope
        self.member_replay = []
        FixedSimulation.__init__(self, case, "fixed-known")

    def compute_values(self, tx):
        # Execute the backend's instrumentation once for the combined program,
        # then substitute its additive result. Per-contribution synthetic work
        # is already included in group_case's CPU charge; member outcomes are
        # expanded by the inherited original-request oracle and run accounting.
        FixedSimulation.compute_values(self, tx)
        count = tx.spec.get("fold_count", 1)
        if count > 1:
            assert tx.spec["op"] == "increment" and tx.spec.get("delay", 0) == 0
            inputs = {key: value for values in tx.captured.values() for key, value in values.items()}
            tx.writes = {key: inputs[key] + count for key in tx.spec["writes"]}
            tx.attempt.writes.update(tx.writes)

    def run(self):
        result = super().run()
        result["grouping_scope"] = self.grouping_scope
        return result


def check_ungrouped(case, adapted):
    baseline = FixedSimulation(deepcopy(case), "fixed-known").run()
    for key in ("trace_sha256", "logical_state_sha256", "counts", "certification",
                "write_admission", "time_ticks", "retained_versions_no_gc", "input_sha256"):
        assert baseline[key] == adapted[key], (case["name"], key)
    assert baseline["cohorts"] == adapted["execution_cohorts"]
    for cohort, values in baseline["cohorts"].items():
        for key in ("offered", "complete", "failed", "pending", "attempts",
                    "p50_ticks", "p99_ticks", "oldest_pending_ticks"):
            assert values[key] == adapted["cohorts"][cohort][key], (case["name"], cohort, key)


def configurations(case, windows, caps, scope="all-eligible", pairs=None):
    yield None, 1, [1]
    selected = pairs or [(window, cap) for window in windows for cap in caps]
    for window in dict.fromkeys(window for window, _ in selected):
        distinct = {}
        for cap in dict.fromkeys(cap for chosen, cap in selected if chosen == window):
            grouped, _, _ = scoped_group_case(case, window, cap, scope)
            signature = digest(grouped)
            if signature not in distinct:
                distinct[signature] = (cap, [])
            distinct[signature][1].append(cap)
        for cap, equivalent in distinct.values():
            yield window, cap, equivalent


def probe():
    initial = {"eu/x": 0, "us/y": 0}
    transactions = [{"id": f"member-{index}", "arrival": index,
                     "group": "increments", "coordinator": "eu",
                     "reads": list(initial), "writes": list(initial), "op": "increment"}
                    for index in range(4)]
    case = {"name": "fixed-fold-counter-check", "description": "Four exact additive members",
            "initial": initial, "scopes": {key: [key] for key in initial},
            "transactions": transactions, "horizon": 500, "capacity": 16,
            "link_delay": 2, "retry_delay": 3, "retry_limit": 1}
    simulation = FixedFoldSimulation(case, window=5, cap=8)
    result = simulation.run()
    assert simulation.store.check_serial() == {"eu/x": 4, "us/y": 4}
    assert result["execution_groups"] == 1 and result["committed_original_requests"] == 4
    assert result["cohorts"]["increments"]["complete"] == 4
    assert result["execution_cohorts"]["increments"]["program_executions"] == 1
    assert result["counts"]["program_executions"] == 1
    assert result["counts"]["fixed_program_executions"] == 1
    assert result["counts"]["compute_units"] == 8  # 1 + 2 reads + 2 writes + 3 contributions.
    assert result["counts"]["final_values_staged"] == 2
    assert not result["certification"].get("renew_calls", 0)
    for row in result["requests"]:
        assert row["latency"] == row["completion"] - row["arrival"]
    pending = deepcopy(case)
    pending["horizon"] = 1
    pending_result = FixedFoldSimulation(pending, window=5, cap=8).run()
    assert pending_result["cohorts"]["increments"]["offered"] == 2
    assert pending_result["cohorts"]["increments"]["pending"] == 2
    for option, value in (("returns_value", True), ("condition", "value < 10")):
        assert not eligible(transactions[0] | {option: value})
    assert not eligible(transactions[0] | {"delay": 1})
    assert len(list(configurations(case, [5], [8, 32]))) == 2
    ungrouped = FixedFoldSimulation(case).run()
    check_ungrouped(case, ungrouped)
    local = {"id": "local-control", "arrival": 1, "group": "increments", "coordinator": "eu",
             "reads": ["eu/x"], "writes": ["eu/x"], "op": "increment"}
    scoped_case = deepcopy(case)
    scoped_case["transactions"].append(local)
    scoped, _, members = scoped_group_case(scoped_case, 5, 8, "cross-shard-only")
    assert next(spec for spec in scoped["transactions"] if spec["id"] == local["id"]) == local
    assert members[local["id"]] == [local["id"]]
    assert any(len(group) > 1 for group in members.values())
    check_ungrouped(case, FixedFoldSimulation(case, scope="cross-shard-only").run())
    return {"passed": True, "checks": ["combined results and original serial replay",
            "backend execution counters and contribution cost", "original arrival latency",
            "pre-window pending requests", "conditional/value-result eligibility exclusion",
            "identical-cap deduplication", "exact ungrouped backend equivalence",
            "cross-shard scope derives from participants and leaves local requests unchanged"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--windows", default="1,5,25,128")
    parser.add_argument("--caps", default="8,32")
    parser.add_argument("--case", default="")
    parser.add_argument("--pairs", default="", help="Explicit window:cap pairs instead of the Cartesian product")
    parser.add_argument("--grouping-scope", choices=("all-eligible", "cross-shard-only"), default="all-eligible")
    args = parser.parse_args()
    before = identity()
    checks = probe()
    rows = []
    pairs = [tuple(map(int, pair.split(":"))) for pair in args.pairs.split(",")] if args.pairs else None
    for case in cases(seed=args.seed, width=args.width):
        if case["name"] not in CASES or not any(part in case["name"] for part in args.case.split(",")):
            continue
        for window, cap, equivalent in configurations(
                case, list(map(int, args.windows.split(","))), list(map(int, args.caps.split(","))),
                args.grouping_scope, pairs):
            row = FixedFoldSimulation(case, window, cap, args.grouping_scope).run()
            if window is None:
                check_ungrouped(case, row)
            row.update(seed=args.seed, width=args.width, equivalent_caps=equivalent,
                       ungrouped_backend_equivalence=window is None)
            rows.append(row)
            print(f'{case["name"]} window={window} caps={equivalent}: ' + ", ".join(
                f'{name}={cohort["complete"]}/{cohort["offered"]}, '
                f'failed={cohort["failed"]}, pending={cohort["pending"]}, p99={cohort["p99_ticks"]}'
                for name, cohort in row["cohorts"].items()), flush=True)
    assert before == identity(), "sources changed during run; discard and rerun"
    result = {"source_sha256": before, "checks": checks,
              "parameters": vars(args) | {"output": str(args.output)},
              "contract": "Completion-only unconditional integer increments with identical declared reads, writes, coordinator and cohort, and no private delay. Each agreed arrival window produces a deterministic serial block at one fixed group position; all original request outcomes remain counted.",
              "cost": "One shared read/write computation plus one synthetic service unit per extra contribution. Reserve, exact-position publication, final-value staging and installation use the fixed-known backend. Every member latency includes its original arrival, window seal and all protocol waiting. Oversized local groups use the non-fast path.",
              "limits": "Fixed arrival windows, not adaptive batching, native associative-fold implementation, measured latency, or a general SQL result adapter. Groups have one shared outcome and no intermediate member versions. Conditional programs, per-member value results and changed footprints are ineligible. Group admission age starts at the window seal, so relative queue order can change. Cross-shard-only grouping selects by the union of read/write owners and coordinator, never by cohort. The inherited blocked-capture timeout uses attempt age, including gate and metadata waits; admission itself has no deadline.",
              "rows": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(f"Wrote {len(rows)} fixed-position fold comparisons to {args.output}")


if __name__ == "__main__":
    main()
