#!/usr/bin/env python3
"""Bounded additive microbatch experiment on the shared queues and protocol.

Eligible programs add one to an identical complete read/write footprint, return
only completion, have one coordinator and no private delay. A deterministic
arrival window chooses a group before execution; this is not opportunistic
physical scheduling. Each group owns one serialization position and is replayed
as a deterministic serial block of its original members. No intermediate member
versions or individual value results are promised.

Group work costs one shared read/write execution plus one synthetic service unit
for each extra contribution. Window waiting and group outcomes are charged to
every original request. This is not general SQL batching or a native executor
benchmark; there is no new locking, timestamp or recovery implementation here.
"""

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path

from comparison import Simulation, digest, evaluate, source_identity
from comparison_inputs import cases


CASES = {"ordinary_distributed_hot", "ordinary_distributed_cold",
         "ordinary_hot_local", "narrow_conflicting_wan", "rare_wan_saturated_local"}


def identity():
    return {**source_identity(),
            "fold_comparison.py": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}


def eligible(spec):
    # Unknown program options may encode results or conditions. They must not
    # silently enter the completion-only additive algebra.
    allowed = {"id", "arrival", "group", "coordinator", "reads", "writes",
               "op", "delay", "cpu"}
    return (set(spec) <= allowed and spec["op"] == "increment"
            and spec.get("delay", 0) == 0 and bool(spec["writes"]))


def group_case(original, window, cap):
    """Seal each window at its end; split by cap in (arrival, id) order."""
    assert window is None or window > 0
    assert cap > 0
    case = deepcopy(original)
    originals = {spec["id"]: deepcopy(spec) for spec in original["transactions"]}
    assert len(originals) == len(original["transactions"])
    batches, members, buckets = [], {}, defaultdict(list)
    for spec in originals.values():
        if window is None or not eligible(spec):
            batches.append(deepcopy(spec))
            members[spec["id"]] = [spec["id"]]
            continue
        signature = (spec["arrival"] // window, spec["coordinator"], spec["group"],
                     tuple(sorted(spec["reads"])), tuple(sorted(spec["writes"])),
                     spec.get("cpu"))
        buckets[signature].append(spec)
    for signature, entries in buckets.items():
        entries.sort(key=lambda spec: (spec["arrival"], spec["id"]))
        for start in range(0, len(entries), cap):
            chunk = entries[start:start + cap]
            spec = deepcopy(chunk[0])
            spec["id"] = "fold/" + chunk[0]["id"]
            assert spec["id"] not in originals and spec["id"] not in members
            spec["arrival"] = (signature[0] + 1) * window
            assert all(member["arrival"] < spec["arrival"] for member in chunk)
            keys = set().union(*(set(case["scopes"].get(scope, (scope,)))
                                 for scope in spec["reads"]))
            assert set(spec["writes"]) <= keys
            base_work = spec.get("cpu", 1 + len(keys) + len(spec["writes"]))
            spec["cpu"] = base_work + len(chunk) - 1
            spec["fold_count"] = len(chunk)
            batches.append(spec)
            members[spec["id"]] = [member["id"] for member in chunk]
    case["transactions"] = sorted(batches, key=lambda spec: (spec["arrival"], spec["id"]))
    flattened = [member for group in members.values() for member in group]
    assert len(flattened) == len(set(flattened)) == len(originals)
    assert set(flattened) == set(originals)
    return case, originals, members


class FoldSimulation(Simulation):
    def __init__(self, original, policy="snapshot-wait", window=None, cap=1):
        assert policy in ("certification", "snapshot-wait")
        case, self.originals, self.members = group_case(original, window, cap)
        self.original_case = deepcopy(original)
        self.window, self.cap = window, cap
        self.member_replay = []
        super().__init__(case, policy)

    def compute_values(self, tx):
        count = tx.spec.get("fold_count", 1)
        if count == 1:
            return super().compute_values(tx)
        assert tx.spec["op"] == "increment" and tx.spec.get("delay", 0) == 0
        inputs = {key: value for values in tx.captured.values() for key, value in values.items()}
        tx.writes = {key: inputs[key] + count for key in tx.spec["writes"]}
        tx.computed = True
        tx.attempt.writes.update(tx.writes)

    def check_serial(self):
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
            inputs = {key: value for values in record["reads"].values()
                      for key, value in values.items()}
            count = spec.get("fold_count", 1)
            expected_group = ({key: inputs[key] + count for key in spec["writes"]}
                              if count > 1 else evaluate(spec, inputs))
            assert record["writes"] == expected_group
            for ordinal, member in enumerate(self.members[record["id"]]):
                assert member not in committed_originals
                original = self.originals[member]
                assert set(original["reads"]) == set(spec["reads"])
                assert set(original["writes"]) == set(spec["writes"])
                reads = {scope: {key: state[key] for key in self.scopes[scope]}
                         for scope in original["reads"]}
                member_inputs = {key: value for values in reads.values()
                                 for key, value in values.items()}
                writes = evaluate(original, member_inputs)
                state.update(writes)
                committed_originals.add(member)
                self.member_replay.append({"id": member, "batch": record["id"],
                                           "ordinal": ordinal, "reads": reads, "writes": writes})
            assert record["writes"] == {key: state[key] for key in spec["writes"]}

        # Check logical contribution totals separately from aggregate +count
        # evaluation. Exclude any key whose program can overwrite a sum.
        overwritten = {key for spec in self.originals.values() if spec["op"] != "increment"
                       for key in spec["writes"]}
        contributions = Counter(key for member in committed_originals
                                if self.originals[member]["op"] == "increment"
                                for key in self.originals[member]["writes"])
        for key, count in contributions.items():
            if key not in overwritten:
                assert state[key] == self.case["initial"][key] + count
        if self.case["name"] == "ordinary_distributed_hot":
            count = sum(self.originals[member]["group"] == "distributed"
                        for member in committed_originals)
            assert state["eu/distributed/000"] == state["us/distributed/000"] == count
        elif self.case["name"] == "ordinary_distributed_cold":
            for key in state:
                if key.startswith("eu/distributed/"):
                    assert state[key] == state[key.replace("eu/", "us/", 1)]

        pending = {key for key, promise in self.store.claims.items()
                   if self.store.attempts[promise.owner].decision == "commit"}
        actual = {key: self.store.versions[key][-1].value for key in state}
        assert all(actual[key] == state[key] for key in state if key not in pending)
        return digest(state)

    def run(self):
        result = super().run()
        request_rows = []
        for batch_id, members in self.members.items():
            tx = self.tx[batch_id]
            for member in members:
                original = self.originals[member]
                outcome = ("future" if original["arrival"] > self.time else
                           tx.state if tx.state in ("complete", "failed") else "pending")
                request_rows.append({"id": member, "group": original["group"],
                                     "arrival": original["arrival"], "batch": batch_id,
                                     "batch_size": len(members), "outcome": outcome,
                                     "completion": tx.completion, "attempts": tx.attempts,
                                     "latency": None if tx.completion is None else
                                     tx.completion - original["arrival"]})
        cohorts = {}
        for group in sorted({spec["group"] for spec in self.originals.values()}):
            members = [row for row in request_rows if row["group"] == group and row["outcome"] != "future"]
            completed = [row for row in members if row["outcome"] == "complete"]
            latencies = sorted(row["latency"] for row in completed)
            pending = [row for row in members if row["outcome"] == "pending"]
            cohorts[group] = {
                "offered": len(members), "complete": len(completed),
                "failed": sum(row["outcome"] == "failed" for row in members),
                "pending": len(pending), "attempts": sum(row["attempts"] for row in members),
                "p50_ticks": latencies[(len(latencies) - 1) // 2] if latencies else None,
                "p99_ticks": latencies[math.ceil(.99 * len(latencies)) - 1] if latencies else None,
                "oldest_pending_ticks": max((self.time - row["arrival"] for row in pending), default=0),
            }
            assert cohorts[group]["offered"] == sum(cohorts[group][key]
                                                     for key in ("complete", "failed", "pending"))
        result["execution_cohorts"] = result["cohorts"]
        result["cohorts"] = cohorts
        result.update(variant="ungrouped" if self.window is None else "additive-fold",
                      window_ticks=self.window, cap=self.cap, original_input_sha256=digest(self.original_case),
                      original_requests=len(self.originals), execution_groups=len(self.members),
                      multi_member_groups=sum(len(members) > 1 for members in self.members.values()),
                      max_group_size=max(map(len, self.members.values()), default=0),
                      original_serial_check=True, original_serial_sha256=digest(self.member_replay),
                      committed_original_requests=len(self.member_replay),
                      group_manifest_sha256=digest(self.members),
                      requests=sorted(request_rows, key=lambda row: (row["arrival"], row["id"])))
        return result


def probe():
    fixture = next(case for case in cases(seed=7, width=4)
                   if case["name"] == "ordinary_distributed_hot")
    grouped = FoldSimulation(fixture, window=5, cap=8)
    assert len(grouped.members) < len(fixture["transactions"])
    for batch_id, members in grouped.members.items():
        originals = [grouped.originals[member] for member in members]
        assert len(members) <= 8
        assert len({spec["coordinator"] for spec in originals}) == 1
    conditional = deepcopy(fixture["transactions"][0])
    conditional["returns_value"] = True
    assert not eligible(conditional)
    conditional.pop("returns_value")
    conditional["condition"] = "value < 10"
    assert not eligible(conditional)
    delayed = deepcopy(conditional)
    delayed.pop("condition")
    delayed["delay"] = 1
    assert not eligible(delayed)

    # Requests arriving before a seal still count as pending if the horizon
    # precedes that seal, even though their synthetic execution is future work.
    pending = deepcopy(fixture)
    pending["horizon"] = 1
    pending["transactions"] = [spec for spec in pending["transactions"] if spec["arrival"] == 0]
    result = FoldSimulation(pending, window=5, cap=8).run()
    assert sum(row["pending"] for row in result["cohorts"].values()) == len(pending["transactions"])

    # Without grouping, the subclass must reproduce the frozen common harness.
    baseline = Simulation(fixture, "snapshot-wait").run()
    replay = FoldSimulation(fixture).run()
    for key in ("trace_sha256", "logical_state_sha256", "counts"):
        assert baseline[key] == replay[key]
    assert all(baseline["cohorts"][group][key] == replay["cohorts"][group][key]
               for group in baseline["cohorts"]
               for key in ("offered", "complete", "failed", "pending", "p99_ticks"))
    return {"passed": True, "checks": 5}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", default="7")
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--windows", default="1,5")
    parser.add_argument("--caps", default="8,32")
    parser.add_argument("--case", default="")
    parser.add_argument("--policy", choices=("snapshot-wait", "certification"), default="snapshot-wait")
    args = parser.parse_args()
    before = identity()
    checks = probe()
    rows = []
    for seed in map(int, args.seeds.split(",")):
        for case in cases(seed=seed, width=args.width):
            if case["name"] not in CASES or args.case not in case["name"]:
                continue
            configurations = [(None, 1)] + [(window, cap) for window in map(int, args.windows.split(","))
                                             for cap in map(int, args.caps.split(","))]
            for window, cap in configurations:
                row = FoldSimulation(case, args.policy, window, cap).run()
                row.update(seed=seed, width=args.width)
                rows.append(row)
                print(f'{case["name"]} window={window} cap={cap}: ' + ", ".join(
                    f'{group} {stats["complete"]}/{stats["offered"]} done, {stats["failed"]} failed, '
                    f'{stats["pending"]} pending, p99={stats["p99_ticks"]}'
                    for group, stats in row["cohorts"].items()), flush=True)
    assert before == identity(), "sources changed during run; discard and rerun"
    result = {"source_sha256": before, "checks": checks,
              "contract": "Completion-only, unconditional integer increments with identical declared reads, writes and coordinator; zero private delay. Fixed agreed-input windows; one group-owned serialization position with a deterministic serial block of original requests. No intermediate member versions.",
              "cost": "One shared read/write execution plus one synthetic service unit per additional contribution; every member includes window and protocol waiting in its latency. Oversized local groups use the existing non-fast path.",
              "limits": "Synthetic shared queues/protocol, not a native fold executor benchmark. Group outcome is shared; all original failures/pending requests remain counted. Counterexamples include per-request value results, conditional effects and distinct footprints.",
              "parameters": vars(args) | {"output": str(args.output)}, "rows": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(f"Wrote {len(rows)} fold comparisons to {args.output}")


if __name__ == "__main__":
    main()
