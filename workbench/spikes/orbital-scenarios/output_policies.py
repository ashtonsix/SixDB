#!/usr/bin/env python3
"""Compute-first output waiting and discovery/refresh comparisons.

Both candidates use snapshot-wait capture and the shared certification core.
output-wait takes canonical output gates after computation, makes the final
promise at each granted participant, then certifies the original computation.
output-refresh takes gates without final promises, discards the discovery pass,
and executes the whole program again at a fresh snapshot before certification.

The shared 19 workloads have fixed, authored output sets. Admission is delayed
until those outputs are computed, but that alone does not test dynamic SQL.
discovery_probe.py is paired with the sweep to expose changing output identity.
Neither candidate claims bounded admission waiting, recovery, or guaranteed
completion when sources outside its output gates keep changing.
"""

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path

from comparison import Simulation, shard, source_identity as shared_identity
from comparison_inputs import cases


ROOT = Path(__file__).resolve().parent
POLICIES = ("output-wait", "output-refresh")
DRAIN_CASES = ("ordinary_distributed_hot", "narrow_conflicting_wan")


@dataclass
class OutputWork:
    pass_number: int = 0
    refreshing: bool = False
    gate_keys: tuple[str, ...] = ()
    gate_groups: list = field(default_factory=list)
    gate_part: int = 0
    gates_complete: bool = False
    fast: bool = False


class OutputSimulation(Simulation):
    """Add output admission around the frozen harness's snapshot-wait behavior."""

    def __init__(self, case, policy):
        assert policy in POLICIES
        self.variant = policy
        # Keep the shared capture/renewal semantics instead of copying them.
        super().__init__(case, "snapshot-wait")
        self.output_work = {}
        self.executions = Counter()

    def start(self, tx):
        self.output_work[tx.spec["id"]] = OutputWork()
        super().start(tx)

    def begin_evaluation(self, tx, fast=False):
        self.output_work[tx.spec["id"]].fast = fast
        super().begin_evaluation(tx, fast)

    def begin_attempt(self, tx):
        # A discovery pass and its replacement are different core attempts but
        # consume one whole-attempt retry budget. This otherwise mirrors the
        # shared snapshot selection, using delivered remote bounds only.
        work = self.output_work[tx.spec["id"]]
        coord = tx.spec["coordinator"]
        owner = f"{tx.spec['id']}#{tx.attempts}/pass{work.pass_number}"
        local = [scope for node, scopes in self.groups(tx) if node == coord for scope in scopes]
        observed = [self.store.W[scope][0] for scope in local] + list(tx.bound_versions.values())
        floor = max([self.clocks[coord], tx.spec.get("causal_floor", 0)] + observed) + 1
        self.clocks[coord] = floor
        position = (floor, owner)
        if tx.spec.get("allow_old"):
            assert not tx.spec["writes"]
            covered = set().union(*(set(self.scopes[scope]) for scope in local)) if local else set()
            bounds = [promise.minimum for promises in self.store.promises.values()
                      for promise in promises if covered.intersection(promise.remaining)]
            bounds += tx.bound_promises
            if bounds:
                upper = min(bounds)
                candidate = (upper[0] if owner < upper[1] else upper[0] - 1, owner)
                position = min(position, candidate)
            if position < (tx.spec.get("causal_floor", 0), ""):
                return False
        tx.attempt = self.store.begin(owner, position)
        self.counts["execution_passes_started"] += 1
        self.note("execution_pass", tx, core_owner=owner, snapshot=position,
                  refreshing=work.refreshing)
        return True

    def compute_values(self, tx):
        super().compute_values(tx)
        work = self.output_work[tx.spec["id"]]
        self.executions[tx.spec["id"]] += 1
        self.counts["program_executions"] += 1
        self.counts["private_execution_delay_ticks"] += tx.spec.get("delay", 0)
        if work.refreshing:
            self.counts["refresh_executions"] += 1

    def local(self, tx):
        if tx.state != "active":
            return
        work = self.output_work[tx.spec["id"]]
        if tx.spec["writes"]:
            # Fixed-output short local operations can combine admission,
            # capture, computation and publication in the same logical step.
            # This elides a redundant discovery execution, not a WAN phase.
            keys = tuple(sorted(tx.spec["writes"]))
            if not self.admission.request(tx.spec["id"], keys):
                self.counts["admission_wait_probes"] += 1
                self.schedule(self.time + self.retry_delay, self.retry_local, tx, tx.generation)
                return
            work.gate_keys = keys
            work.gate_groups = [(tx.spec["coordinator"], keys)]
            work.gates_complete = True
        super().local(tx)
        if tx.state == "complete":
            self.admission.release_all(tx.spec["id"])

    def capture_wait(self, tx, owner, scopes=None, part=None):
        work = self.output_work[tx.spec["id"]]
        if scopes is None and work.fast and work.gate_keys:
            # A blocked source means the local operation cannot finish in one
            # step. Release its preliminary gate before waiting, retain its
            # sealed reads, and continue through the ordinary compute-first
            # path. Do not hold an output gate while a local shortcut sleeps.
            self.admission.release_all(tx.spec["id"])
            work.gate_keys, work.gate_groups = (), []
            work.gates_complete, work.fast = False, False
            self.counts["local_shortcuts_demoted"] += 1
            self.note("local_gate_released_for_capture", tx)
            tx.part = 0
            node, input_scopes = self.groups(tx)[0]
            assert node == owner
            super().capture_wait(tx, owner, tuple(input_scopes), 0)
            return
        super().capture_wait(tx, owner, scopes, part)

    @staticmethod
    def group_keys(keys):
        groups = defaultdict(list)
        for key in sorted(keys):
            groups[shard(key)].append(key)
        return [(owner, tuple(values)) for owner, values in sorted(groups.items())]

    def prepare(self, tx):
        if tx.state != "active":
            return
        work = self.output_work[tx.spec["id"]]
        actual = tuple(sorted(tx.writes))
        if work.refreshing:
            if actual != work.gate_keys:
                self.abort(tx, "output_footprint_changed")
                return
            # No discovery-pass promise survives into this execution.
            assert not self.store.promises[tx.attempt.owner]
            super().prepare(tx)
            return
        if not actual:
            super().prepare(tx)
            return
        work.gate_keys = actual
        work.gate_groups = self.group_keys(actual)
        work.gate_part = 0
        tx.phase = "output_admission"
        self.next_output_group(tx)

    def next_output_group(self, tx):
        if tx.state != "active" or tx.phase != "output_admission":
            return
        work = self.output_work[tx.spec["id"]]
        if work.gate_part == len(work.gate_groups):
            work.gates_complete = True
            if self.variant == "output-refresh":
                self.refresh(tx)
            else:
                self.choose(tx)
            return
        owner, keys = work.gate_groups[work.gate_part]
        self.request(tx, owner, self.output_group, keys, work.gate_part, cost=1 + len(keys))

    def output_group(self, tx, owner, keys, part):
        work = self.output_work[tx.spec["id"]]
        if tx.state != "active" or tx.phase != "output_admission" or work.gate_part != part:
            return
        if not self.admission.request(tx.spec["id"], keys):
            self.counts["admission_wait_probes"] += 1
            generation = tx.generation
            def poll():
                if self.live(tx, generation) and tx.state == "active":
                    self.submit(owner, tx, self.output_group, owner, keys, part)
            self.schedule(self.time + self.retry_delay, poll)
            return
        if self.variant == "output-wait":
            # Every writer respects gates, so a prior writer has installed or
            # aborted before this grant. Freeze the actual promise now.
            assert not any(key in self.store.claims and self.store.claims[key].owner != tx.attempt.owner
                           for key in keys), "gate released before its final promise"
            assert self.store.promise(tx.attempt, keys)
            self.clocks[owner] = max(self.clocks[owner], self.store.claims[keys[0]].minimum[0])
        self.note("computed_output_admission", tx, owner=owner, keys=list(keys),
                  final_promise=self.variant == "output-wait")
        self.reply(tx, owner, self.output_granted, part)

    def output_granted(self, tx, part):
        work = self.output_work[tx.spec["id"]]
        if tx.state == "active" and tx.phase == "output_admission" and work.gate_part == part:
            work.gate_part += 1
            self.next_output_group(tx)

    def refresh(self, tx):
        work = self.output_work[tx.spec["id"]]
        assert work.gates_complete and not work.refreshing
        assert not self.store.promises[tx.attempt.owner]
        self.store.decide(tx.attempt, False)
        self.counts["discovery_passes_discarded"] += 1
        self.counts["discovery_compute_units"] += tx.compute_spent
        self.counts["discarded_compute_units"] += tx.compute_spent
        self.counts["discarded_private_delay_ticks"] += tx.spec.get("delay", 0)
        self.note("discard_discovery", tx, core_owner=tx.attempt.owner, keys=list(work.gate_keys))
        # Retire messages from the finished pass; the transaction/age and whole
        # attempt count stay fixed. Discovery read bounds remain in Store.
        tx.generation += 1
        work.pass_number += 1
        work.refreshing = True
        tx.part, tx.attempt = 0, None
        tx.captured, tx.writes = {}, {}
        tx.computed, tx.compute_spent = False, 0
        tx.pending = set()
        tx.bound_versions, tx.bound_promises = {}, []
        self.begin_evaluation(tx)

    def install(self, tx, owner, keys):
        if tx.state != "committed":
            return
        super().install(tx, owner, keys)
        work = self.output_work[tx.spec["id"]]
        self.admission.release(tx.spec["id"], [key for key in work.gate_keys if shard(key) == owner])

    def abort(self, tx, reason):
        if tx.state != "active":
            return
        tx.state, tx.phase = "aborting", "abort"
        tx.rejections[str(reason)] += 1
        self.counts["aborts"] += 1
        self.counts["discarded_compute_units"] += tx.compute_spent
        self.note("abort", tx, reason=reason)
        if tx.attempt:
            self.store.decide(tx.attempt, False)
        work = self.output_work[tx.spec["id"]]
        # Admission coverage belongs to the discovery pass. A changed final
        # footprint must not cause old gates to be omitted from cleanup.
        keys = set(work.gate_keys) | set(tx.writes)
        owners = {shard(key) for key in keys} or {tx.spec["coordinator"]}
        tx.pending = owners
        for owner in sorted(owners):
            local_keys = tuple(sorted(key for key in keys if shard(key) == owner))
            source, generation = tx.spec["coordinator"], tx.generation
            if source != owner:
                self.counts["network_messages"] += 1
            def arrive(owner=owner, keys=local_keys, generation=generation):
                if self.live(tx, generation):
                    self.submit(owner, tx, self.release_abort, owner, keys)
            self.schedule(self.time + self.link(source, owner), arrive)

    def release_abort(self, tx, owner, keys):
        if tx.state != "aborting":
            return
        self.admission.release(tx.spec["id"], keys)
        super().release_abort(tx, owner, keys)

    def run(self):
        result = super().run()
        result["policy"] = self.variant
        result["write_admission"] = dict(self.admission.counters)
        for group, cohort in result["cohorts"].items():
            cohort["program_executions"] = sum(self.executions[tx.spec["id"]]
                for tx in self.tx.values() if tx.spec["group"] == group)
        return result


def source_identity():
    result = shared_identity()
    for name in ("output_policies.py", "discovery_probe.py"):
        result[name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", default="0,7,19")
    parser.add_argument("--widths", default="64")
    parser.add_argument("--policies", default=",".join(POLICIES))
    parser.add_argument("--case", default="")
    parser.add_argument("--no-drain", action="store_true")
    parser.add_argument("--drain-horizon", type=int, default=20000)
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    selected = args.policies.split(",")
    if any(policy not in POLICIES for policy in selected):
        parser.error("unknown output policy")
    before = source_identity()
    rows = []
    for seed in map(int, args.seeds.split(",")):
        for width in map(int, args.widths.split(",")):
            for offered in cases(seed=seed, width=width):
                if args.case and args.case not in offered["name"]:
                    continue
                runs = [("offered", offered)]
                if not args.no_drain and offered["name"] in DRAIN_CASES:
                    drained = deepcopy(offered)
                    drained["horizon"] = max(offered["horizon"], args.drain_horizon)
                    runs.append(("long-drain", drained))
                for run_kind, case in runs:
                    for policy in selected:
                        simulation = OutputSimulation(deepcopy(case), policy)
                        row = simulation.run()
                        row.update(seed=seed, width=width, run_kind=run_kind, horizon=case["horizon"])
                        if args.full:
                            row.update(input=case, trace=simulation.trace)
                        rows.append(row)
                        print(f"{seed}/{case['name']}/{run_kind}/{policy}: " + ", ".join(
                            f"{group}={cohort['complete']}/{cohort['offered']} "
                            f"failed={cohort['failed']} pending={cohort['pending']}"
                            for group, cohort in row["cohorts"].items()), flush=True)
    from discovery_probe import run_probes
    dynamic = run_probes()
    assert before == source_identity(), "sources changed during run; discard and rerun"
    result = {
        "source_sha256": before,
        "parameters": {"seeds": args.seeds, "widths": args.widths, "policies": selected,
                       "case_filter": args.case, "drain_horizon": args.drain_horizon,
                       "drain_cases": list(DRAIN_CASES) if not args.no_drain else []},
        "assumptions": [
            "Synthetic ticks, service work and failure-free transport; not measured latency or throughput.",
            "Shared workload outputs are fixed and authored; compute-first gating does not validate general dynamic SQL.",
            "Canonical groups queue only actual discovered outputs; large queued groups still convoy covered point writers.",
            "Short fixed-output local work can combine gating, capture, compute and commit in one logical step.",
            "All writer paths respect gates; gate releases occur with participant install or delivered abort.",
            "Admission waiting has no deadline; pending work at the horizon is reported.",
            "Discovery and refreshed program execution are charged separately; refresh uses a distinct core owner.",
            "Any refreshed output-key change aborts and releases gates; held sets never grow in place.",
        ],
        "dynamic_discovery": dynamic,
        "rows": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(f"Wrote {len(rows)} output-policy comparisons to {args.output}")


if __name__ == "__main__":
    main()
