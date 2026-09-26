#!/usr/bin/env python3
"""Execution at a position fixed after complete output admission.

fixed-known uses the workloads' predeclared output coverage. fixed-discovered
first executes privately to discover that coverage, then discards that pass.
Both wait for ALL canonical output gates before reserving position metadata.
Reserve/reply, exact-position publication/reply, and final-value staging/reply
are separate exchanges. Program inputs are read only after position publication.
The shared 19 workloads have fixed outputs; this is not general dynamic SQL.
"""

import argparse
from copy import deepcopy
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path

from comparison import digest, evaluate, shard
from comparison_inputs import cases
from fixed_execution import FixedStore
from output_policies import DRAIN_CASES, OutputSimulation, source_identity as output_identity


ROOT = Path(__file__).resolve().parent
POLICIES = ("fixed-known", "fixed-discovered")


@dataclass
class FixedWork:
    had_discovery: bool = False
    position_started: bool = False
    position_ready: bool = False
    staged: dict = field(default_factory=dict)


def evaluate_program(spec, inputs):
    # A small declared-coverage conditional program for semantic tests. Shared
    # workloads continue to use the unchanged comparison evaluator.
    if spec["op"] == "conditional-zero":
        return ({key: 0 for key in spec["writes"]}
                if inputs[spec["condition_key"]] == 1 else {})
    return evaluate(spec, inputs)


class FixedSimulation(OutputSimulation):
    def __init__(self, case, policy):
        assert policy in POLICIES
        super().__init__(case, "output-refresh")
        self.variant = policy
        self.store = FixedStore(self.data, self.scopes)
        self.fixed_work = {}

    def start(self, tx):
        self.fixed_work[tx.spec["id"]] = FixedWork()
        super().start(tx)

    def is_fixed(self, tx):
        return tx.attempt is not None and tx.attempt.owner in self.store.coverage

    def begin_evaluation(self, tx, fast=False):
        work = self.output_work[tx.spec["id"]]
        if (self.variant == "fixed-known" and tx.spec["writes"]
                and not fast and not work.refreshing):
            work.gate_keys = tuple(sorted(tx.spec["writes"]))
            work.gate_groups = self.group_keys(work.gate_keys)
            work.gate_part = 0
            tx.phase = "output_admission"
            self.next_output_group(tx)
            return
        super().begin_evaluation(tx, fast)

    def next_output_group(self, tx):
        work = self.output_work[tx.spec["id"]]
        if tx.state != "active" or tx.phase != "output_admission":
            return
        if work.gate_part == len(work.gate_groups):
            work.gates_complete = True
            self.refresh(tx)
            return
        super().next_output_group(tx)

    def refresh(self, tx):
        work = self.output_work[tx.spec["id"]]
        assert work.gates_complete and not work.refreshing
        assert all(self.admission.grants.get(key) == tx.spec["id"] for key in work.gate_keys)
        if tx.attempt is not None:
            self.fixed_work[tx.spec["id"]].had_discovery = True
            super().refresh(tx)
        else:
            # Known coverage has no discovery pass to discard or charge.
            work.refreshing = True
            self.begin_evaluation(tx)

    def begin_attempt(self, tx):
        work = self.output_work[tx.spec["id"]]
        if not (work.refreshing and work.gates_complete and work.gate_keys):
            return super().begin_attempt(tx)
        coord = tx.spec["coordinator"]
        owner = f"{tx.spec['id']}#{tx.attempts}/pass{work.pass_number}"
        local = [scope for node, scopes in self.groups(tx) if node == coord for scope in scopes]
        observed = [self.store.W[scope][0] for scope in local] + list(tx.bound_versions.values())
        floor = max([self.clocks[coord], tx.spec.get("causal_floor", 0)] + observed) + 1
        self.clocks[coord] = floor
        tx.attempt = self.store.begin_fixed(owner, (floor, owner), work.gate_keys,
                                             self.admission.grants, tx.spec["id"])
        self.counts["execution_passes_started"] += 1
        self.counts["fixed_passes_started"] += 1
        self.note("fixed_execution_floor", tx, core_owner=owner, floor=(floor, owner),
                  coverage=list(work.gate_keys))
        return True

    def next_read(self, tx):
        if tx.state != "active":
            return
        state = self.fixed_work[tx.spec["id"]]
        if self.is_fixed(tx) and not state.position_ready:
            if not state.position_started:
                state.position_started = True
                work = self.output_work[tx.spec["id"]]
                tx.phase = "position_reserve"
                tx.pending = {owner for owner, _ in work.gate_groups}
                for owner, keys in work.gate_groups:
                    self.request(tx, owner, self.reserve_position, keys, cost=1 + len(keys))
            return
        super().next_read(tx)

    def participant_active(self, tx, owner, phase):
        if owner in tx.aborted_participants:
            return False
        return ((tx.state == "active" and tx.phase == phase)
                or tx.state == "aborting")

    def reserve_position(self, tx, owner, keys):
        if not self.participant_active(tx, owner, "position_reserve"):
            return
        if not self.store.reserve_position(tx.attempt, keys, in_flight=True):
            self.reply(tx, owner, self.abort, tx.attempt.rejection_reason)
            return
        bound = self.store.reservation_bounds[tx.attempt.owner][tuple(sorted(keys))]
        self.clocks[owner] = max(self.clocks[owner], bound[0])
        self.note("position_reserved", tx, owner=owner, bound=bound, keys=list(keys))
        self.reply(tx, owner, self.position_reserved, owner, bound)

    def position_reserved(self, tx, owner, bound):
        if tx.state != "active" or tx.phase != "position_reserve":
            return
        self.clocks[tx.spec["coordinator"]] = max(self.clocks[tx.spec["coordinator"]], bound[0])
        tx.pending.discard(owner)
        if tx.pending:
            return
        position = self.store.fix_position(tx.attempt)
        self.clocks[tx.spec["coordinator"]] = max(self.clocks[tx.spec["coordinator"]], position[0])
        work = self.output_work[tx.spec["id"]]
        tx.phase = "position_publish"
        tx.pending = {node for node, _ in work.gate_groups}
        self.note("position_fixed", tx, position=position)
        for node, keys in work.gate_groups:
            self.request(tx, node, self.publish_position, keys)

    def publish_position(self, tx, owner, keys):
        if not self.participant_active(tx, owner, "position_publish"):
            return
        if not self.store.publish_position(tx.attempt, keys, in_flight=True):
            self.reply(tx, owner, self.abort, tx.attempt.rejection_reason)
            return
        self.clocks[owner] = max(self.clocks[owner], tx.attempt.c[0])
        self.note("position_published", tx, owner=owner, position=tx.attempt.c, keys=list(keys))
        self.reply(tx, owner, self.position_published, owner)

    def position_published(self, tx, owner):
        if tx.state != "active" or tx.phase != "position_publish":
            return
        tx.pending.discard(owner)
        if not tx.pending:
            self.fixed_work[tx.spec["id"]].position_ready = True
            tx.phase = "capture"
            self.note("fixed_capture_ready", tx, position=tx.attempt.s)
            self.next_read(tx)

    def compute_values(self, tx):
        if tx.spec["op"] != "conditional-zero":
            super().compute_values(tx)
        else:
            inputs = {key: value for values in tx.captured.values() for key, value in values.items()}
            tx.writes = evaluate_program(tx.spec, inputs)
            tx.computed = True
            tx.attempt.writes.update(tx.writes)
            self.executions[tx.spec["id"]] += 1
            self.counts["program_executions"] += 1
            self.counts["private_execution_delay_ticks"] += tx.spec.get("delay", 0)
            if self.output_work[tx.spec["id"]].refreshing:
                self.counts["refresh_executions"] += 1
        if self.is_fixed(tx):
            self.counts["fixed_program_executions"] += 1
            if not self.fixed_work[tx.spec["id"]].had_discovery:
                # A first execution under known gates is not a reexecution.
                self.counts["refresh_executions"] -= 1

    def prepare(self, tx):
        if not self.is_fixed(tx):
            if tx.state == "active" and tx.computed and not tx.writes:
                # An empty discovery outcome is already a complete read-only
                # execution. The declared coverage must not fabricate writes.
                assert not self.output_work[tx.spec["id"]].gate_keys
                assert not self.store.promises[tx.attempt.owner]
                tx.phase = "prepare"
                self.choose(tx)
                return
            return super().prepare(tx)
        if tx.state != "active":
            return
        coverage = self.store.coverage[tx.attempt.owner]
        if not set(tx.writes) <= coverage:
            self.abort(tx, "output_footprint_changed")
            return
        self.store.seal_values(tx.attempt)
        tx.phase = "values_stage"
        groups = self.output_work[tx.spec["id"]].gate_groups
        tx.pending = {owner for owner, _ in groups}
        self.note("final_values_sealed", tx, position=tx.attempt.c, writes=dict(tx.writes))
        for owner, keys in groups:
            values = {key: tx.writes[key] for key in keys if key in tx.writes}
            # Final bytes travel now, independently of earlier position RPCs.
            self.request(tx, owner, self.stage_values, keys, values, cost=1 + len(values))

    def stage_values(self, tx, owner, keys, values):
        if not self.participant_active(tx, owner, "values_stage"):
            return
        assert tx.attempt.owner in self.store.sealed
        assert set(keys) <= self.store.coverage[tx.attempt.owner]
        expected = {key: tx.attempt.manifest[key] for key in keys if key in tx.attempt.manifest}
        assert values == expected, "staged values must match the sealed manifest"
        staged = self.fixed_work[tx.spec["id"]].staged
        if owner in staged:
            assert staged[owner] == values
        else:
            staged[owner] = dict(values)
            self.counts["final_value_groups_staged"] += 1
            self.counts["final_values_staged"] += len(values)
        self.note("final_values_staged", tx, owner=owner, values=dict(values), position=tx.attempt.c)
        self.reply(tx, owner, self.values_staged, owner)

    def values_staged(self, tx, owner):
        if tx.state != "active" or tx.phase != "values_stage":
            return
        tx.pending.discard(owner)
        self.note("final_values_acknowledged", tx, owner=owner)
        if not tx.pending:
            tx.phase = "fixed_commit"
            self.submit(tx.spec["coordinator"], tx, self.commit)

    def commit(self, tx):
        if not self.is_fixed(tx):
            if tx.state == "active" and tx.phase == "renew" and not tx.writes:
                assert not self.store.promises[tx.attempt.owner]
                assert tx.attempt.c == tx.attempt.s
                self.store.decide(tx.attempt, True)
                self.record_decision(tx, tx.attempt.c)
                self.complete(tx)
                return
            return super().commit(tx)
        if tx.state != "active" or tx.phase != "fixed_commit":
            return
        groups = self.output_work[tx.spec["id"]].gate_groups
        assert set(self.fixed_work[tx.spec["id"]].staged) == {owner for owner, _ in groups}
        self.store.decide(tx.attempt, True)
        self.record_decision(tx, tx.attempt.c)
        tx.pending = {owner for owner, _ in groups}
        for owner, coverage in groups:
            # Resolve every promised key, including conditional no-write keys.
            self.request(tx, owner, self.install, coverage, cost=1 + len(coverage))

    def release_abort(self, tx, owner, keys):
        if tx.state != "aborting":
            return
        tx.aborted_participants.add(owner)
        self.admission.release(tx.spec["id"], keys)
        if tx.attempt is not None:
            permitted = (self.store.coverage[tx.attempt.owner] if self.is_fixed(tx)
                         else set(tx.attempt.writes))
            self.store.release(tx.attempt, tuple(key for key in keys if key in permitted))
        self.reply(tx, owner, self.aborted, owner)

    def check_serial(self):
        if not any(tx.spec["op"] == "conditional-zero" for tx in self.tx.values()):
            return super().check_serial()
        # Independent complete-program oracle for the no-write test program.
        state = dict(self.case["initial"])
        for record in sorted(self.decisions, key=lambda item: item["position"]):
            spec = self.tx[record["id"]].spec
            assert set(record["reads"]) == set(spec["reads"])
            for scope, observed in record["reads"].items():
                assert observed == {key: state[key] for key in self.scopes[scope]}
            inputs = {key: value for values in record["reads"].values() for key, value in values.items()}
            assert record["writes"] == evaluate_program(spec, inputs)
            assert set(record["writes"]) <= set(spec["writes"])
            state.update(record["writes"])
        pending = {key for key, claim in self.store.claims.items()
                   if self.store.attempts[claim.owner].decision == "commit"}
        assert all(self.store.head()[key] == value for key, value in state.items() if key not in pending)
        return digest(state)


def source_identity():
    result = output_identity()
    for name in ("fixed_simulation.py", "fixed_execution.py"):
        result[name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", default="7")
    parser.add_argument("--widths", default="64")
    parser.add_argument("--policies", default=",".join(POLICIES))
    parser.add_argument("--case", default="")
    parser.add_argument("--drain", action="store_true")
    parser.add_argument("--drain-horizon", type=int, default=20000)
    parser.add_argument("--read-wait-ticks", type=int)
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    selected = args.policies.split(",")
    if any(policy not in POLICIES for policy in selected):
        parser.error("unknown fixed policy")
    before = source_identity()
    rows = []
    for seed in map(int, args.seeds.split(",")):
        for width in map(int, args.widths.split(",")):
            for offered in cases(seed=seed, width=width):
                if args.case and args.case not in offered["name"]:
                    continue
                if args.read_wait_ticks is not None:
                    offered["read_wait_ticks"] = args.read_wait_ticks
                runs = [("offered", offered)]
                if args.drain and offered["name"] in DRAIN_CASES:
                    drained = deepcopy(offered)
                    drained["horizon"] = max(offered["horizon"], args.drain_horizon)
                    runs.append(("long-drain", drained))
                for run_kind, case in runs:
                    for policy in selected:
                        simulation = FixedSimulation(deepcopy(case), policy)
                        row = simulation.run()
                        row.update(seed=seed, width=width, run_kind=run_kind, horizon=case["horizon"])
                        if args.full:
                            row.update(input=case, trace=simulation.trace)
                        rows.append(row)
                        print(f"{seed}/{case['name']}/{run_kind}/{policy}: " + ", ".join(
                            f"{group}={cohort['complete']}/{cohort['offered']} "
                            f"failed={cohort['failed']} pending={cohort['pending']}"
                            for group, cohort in row["cohorts"].items()), flush=True)
    assert before == source_identity(), "sources changed during run; discard and rerun"
    result = {
        "source_sha256": before,
        "parameters": {"seeds": args.seeds, "widths": args.widths, "policies": selected,
                       "case_filter": args.case, "read_wait_ticks": args.read_wait_ticks,
                       "drain_horizon": args.drain_horizon,
                       "drain_cases": list(DRAIN_CASES) if args.drain else []},
        "assumptions": [
            "Synthetic service units and failure-free messages; no measured latency or throughput.",
            "The shared workloads have fixed outputs; fixed-known has an explicit predeclared-coverage advantage.",
            "No read-blocking position promise exists until all canonical output gates are held.",
            "Separate reserve/reply and publish/reply rounds precede captured inputs and computation.",
            "Separate final-value payload staging/reply follows computation and precedes the commit decision.",
            "Conditional no-write outputs resolve their full reserved coverage on commit or abort.",
            "The default shared capture deadline is retained; admission waiting is bounded only by the run horizon.",
            "This is an exploratory protocol model, not a recovery or general SQL correctness proof.",
        ],
        "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(f"Wrote {len(rows)} fixed-position comparisons to {args.output}")


if __name__ == "__main__":
    main()
