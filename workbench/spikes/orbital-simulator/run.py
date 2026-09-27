"""Run the authored comparisons; Python builders remain the primary interface."""
from __future__ import annotations

import argparse
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import platform
import traceback

from kernel import digest
import epochs
import composed
import objects
import object_oracles
import protocol
from dataflow_scenario import FlowConfig, build as build_flow, summarize as summarize_flow


HERE = Path(__file__).resolve().parent
CASES = (
    "composed-result", "composed-delayed-artifact", "composed-recovery",
    "admission-direct", "admission-relay", "admission-loss", "admission-recovery",
    "admission-blocked-payload", "admission-lost-leader", "negative-admission",
    "epochs-broad-read", "negative-epochs", "objects-demand", "objects-window",
    "objects-full", "objects-refused", "objects-late-view", "negative-objects",
    "extension-match", "extension-mismatch", "extension-missing", "negative-extension",
    "lease-retirement", "flow-source-raw", "flow-source-enhanced",
    "flow-destination-raw", "flow-destination-enhanced", "flow-consumer-raw",
    "flow-consumer-enhanced", "flow-cheap-raw", "flow-cheap-enhanced",
)


def sources():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(HERE.glob("*.py"))}


def build_case(name, seed=1, ordering="shuffle", replay=None):
    """Return environment, observation cutoff and independent scenario check."""
    if name.startswith("composed-"):
        w, config = composed.build(seed, ordering, replay,
            hold_artifact_until=800_000 if name == "composed-delayed-artifact" else None)
        if name == "composed-recovery":
            w.when("durable_write", lambda e: e["actor"] == "application" and e["key"] == "outbox/2",
                   "crash", actor="application")
            w.fault(800_000, "restart", actor="application")
            w.when("durable_write", lambda e: e["actor"] == "consumer" and e["key"] == "applied/2",
                   "crash", actor="consumer")
            w.fault(1_500_000, "restart", actor="consumer")
        return w, 3_000_000, lambda world: composed.audit(world, config)
    if name.startswith("admission-") or name == "negative-admission":
        kwargs = dict(mode="relay" if name == "admission-relay" else "direct",
                      seed=seed, ordering=ordering, replay=replay)
        if name == "admission-loss":
            kwargs.update(loss=.2, duplicate=.35)
        if name == "negative-admission":
            kwargs["negative"] = "publish-too-early"
        if name == "admission-recovery":
            kwargs["consumer_compute_ns"] = 250_000
        w = protocol.build_scenario(**kwargs)
        if name == "admission-recovery":
            for host in ("source", "witness_a"):
                w.when("prefix_chosen", lambda e: len(e["entries"]) == 4,
                       "destroy", host=host)
        elif name == "admission-blocked-payload":
            for source in ("source", "copy_b", "copy_c"):
                w.partition(source, "reader")
        elif name == "admission-lost-leader":
            w.fault(0, "destroy", host="witness_a")

        def check(world):
            result = protocol.audit(world)
            result["complete"] = set(result["published"]) == {1, 2, 3, 4}
            result["last_publication_ns"] = max((e["time"] for e in world.trace
                                                 if e["kind"] == "published"), default=None)
            return result
        return w, 3_000_000, check

    if name in ("epochs-broad-read", "negative-epochs"):
        w = epochs.build_scenario(width=64, seed=seed, ordering=ordering, replay=replay,
                                  ignore_pending=name.startswith("negative"))
        def check(world):
            errors = epochs.audit(world, 64)
            coverage = {(e["actor"], e["epoch"]) for e in world.trace if e["kind"] == "epoch_fixpoint"}
            observed = {(e["actor"], e["op"]) for e in world.trace if e["kind"] == "snapshot_result"}
            required = {(f"consumer{i}", epoch) for i in range(3) for epoch in (1, 2)}
            reads = {(f"consumer{i}", op) for i in range(3)
                     for op in ("analysis", "dependent", "local-result")}
            return dict(ok=not errors, violations=errors,
                        complete=coverage == required and observed == reads,
                        epochs=len(coverage), results=len(observed))
        return w, 200_000, check

    if name.startswith("objects-") or name == "negative-objects":
        policy = name.removeprefix("objects-")
        if policy not in ("demand", "window", "full"):
            policy = "full" if name == "objects-refused" else "demand"
        w = objects.object_world(policy=policy, resident_pages=4 if name == "objects-full" else 2,
            seed=seed, ordering=ordering, late_open=name == "objects-late-view",
            unsafe_latest=name == "negative-objects")
        def check(world):
            errors = []
            try: object_oracles.check_observations(world)
            except AssertionError as error: errors.append(str(error))
            counts = world.report()["counts"]
            return dict(ok=not errors, violations=errors,
                        complete=bool(counts.get("complete")), refused=bool(counts.get("refused")),
                        observed=counts.get("view_observed", 0), batches=counts.get("page_batch", 0))
        w.kernel.replay = replay
        return w, 10_000_000, check

    if name.startswith("extension-") or name == "negative-extension":
        w = objects.extension_world(seed=seed, ordering=ordering,
            mismatch=name != "extension-match", unsafe_final_only=name == "negative-extension")
        if name == "extension-missing":
            w.fault(0, "crash", actor="check-b")
        def check(world):
            errors = []
            try: object_oracles.check_verification(world)
            except AssertionError as error: errors.append(str(error) or "incomplete or differing transcripts published")
            outcomes = {e["op"]: e["kind"] for e in world.trace if e["kind"] in ("complete", "aborted")}
            publications = {e["op"]: e["time"] for e in world.trace if e["kind"] == "published"}
            return dict(ok=not errors, violations=errors, complete="T" in outcomes,
                        outcomes=outcomes, publication_ns=publications)
        w.kernel.replay = replay
        return w, 10_000_000, check

    if name == "lease-retirement":
        w = objects.lease_world(seed, ordering)
        w.kernel.replay = replay
        def check(world):
            observations = {e["phase"]: e["acquired"] for e in world.trace if e["kind"] == "lease_probe"}
            expected = {"after_owner_release": False, "inside_completion": False, "after_completion": True}
            return dict(ok=observations == expected, violations=[] if observations == expected else ["lease retired too soon"],
                        complete=observations == expected, observations=observations)
        return w, 100_000, check

    if name.startswith("flow-"):
        placement, mode = name.removeprefix("flow-").split("-")
        config = FlowConfig(seed=seed, ordering=ordering,
                            placement="source" if placement == "cheap" else placement,
                            enhance=mode == "enhanced")
        if placement == "cheap":
            config = replace(config, filter_ns_per_row=0, consume_ns_per_row=0,
                             wan_bytes_per_ns=.02, retry_ns=2_000_000, foreground_count=0)
        w = build_flow(config, replay=replay)
        def check(world):
            summary = summarize_flow(world, config)
            complete = summary["materializations"] == 2 and summary["finished_sources"] == 2
            return dict(summary, ok=summary["correct"], complete=complete,
                        violations=[] if summary["correct"] else ["incorrect published reduction"])
        return w, 2_000_000 if placement == "cheap" else 1_000_000, check
    raise ValueError(name)


def exercise(name, seed=1, ordering="shuffle", output=None, replay=None):
    w, until, check = build_case(name, seed, ordering, replay)
    exception = None
    try:
        report = w.run(until=until)
        if replay is not None:
            w.kernel.check_replay()
        result = check(w)
    except Exception:
        exception = traceback.format_exc()
        report = w.report()
        result = dict(ok=False, complete=False, violations=[exception])
    expected_violation = name.startswith("negative-")
    expected_unfinished = name in {"admission-blocked-payload", "admission-lost-leader", "extension-missing"}
    expected_refusal = name == "objects-refused"
    matched = (not result["ok"] if expected_violation else result["ok"])
    if not expected_violation:
        matched &= (bool(result.get("refused")) if expected_refusal else
                    not result["complete"] if expected_unfinished else result["complete"])
    matched &= exception is None
    record = dict(case=name, seed=seed, ordering=ordering, until=until,
                  expectation_met=bool(matched), observation=result,
                  model=report, exception=exception)
    if output:
        output.mkdir(parents=True, exist_ok=False)
        (output / "result.json").write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
        (output / "choices.json").write_text(json.dumps(w.decisions, separators=(",", ":")) + "\n")
        with (output / "trace.jsonl").open("w") as stream:
            for row in w.trace:
                stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
        (output / "provenance.json").write_text(json.dumps(dict(sources=sources(),
            python=platform.python_version(), platform=platform.platform()), indent=2, sort_keys=True) + "\n")
        if w.waits:
            explanations = {str(wait["op"]): w.explain(wait["op"]) for wait in w.waits.values()}
            (output / "unfinished.json").write_text(json.dumps(explanations, indent=2, sort_keys=True) + "\n")
    return record


def compact(record):
    model = record["model"]
    observation = dict(record["observation"])
    for key in ("hosts", "counts", "trace_hash", "waits"):
        observation.pop(key, None)
    return dict(case=record["case"], seed=record["seed"], expectation_met=record["expectation_met"],
                observation=observation, trace_hash=model["trace_hash"],
                unfinished_waits=len(model["waits"]), retired_waits=len(model["retired_waits"]),
                pending_events=model["pending_events"],
                worker_ns=sum(v["busy_ns"] for k, v in model["resources"].items() if k.endswith("/workers")),
                sum_host_memory_peaks_bytes=sum(h["memory_peak"] for h in model["hosts"].values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=("study",) + CASES, default="study")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--ordering", choices=("fifo", "shuffle"), default="shuffle")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--replay", type=Path, help="one earlier case's preserved output directory")
    args = parser.parse_args()
    replay = None
    if args.replay:
        previous = json.loads((args.replay / "result.json").read_text())
        provenance = json.loads((args.replay / "provenance.json").read_text())
        if provenance["sources"] != sources() or provenance["python"] != platform.python_version():
            parser.error("replay requires the recorded source and Python versions")
        args.case, args.seed, args.ordering = previous["case"], previous["seed"], previous["ordering"]
        replay = json.loads((args.replay / "choices.json").read_text())
    selected = CASES if args.case == "study" else [args.case]
    results = [exercise(name, args.seed, args.ordering,
                        args.output / name if args.output else None, replay) for name in selected]
    receipt = dict(kind="synthetic-model-comparison", sources=sources(),
                   python=platform.python_version(), cases=[compact(r) for r in results])
    if args.output:
        (args.output / "summary.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(json.dumps(receipt, indent=2, sort_keys=True))
    return 0 if all(r["expectation_met"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
