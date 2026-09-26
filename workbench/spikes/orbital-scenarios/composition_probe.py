#!/usr/bin/env python3
"""Logical extension/fixpoint and publication-dependency histories.

No WASM VM, Firecracker VM, BLAKE3 primitive, witness or transport is implemented.
Programs here are small pure byte functions. An accepted exact-byte verification
fact stands for completed BLAKE3 verification; absent verification still gates
the epoch where the invocation ran. SHA-256 identifies this probe's artifacts,
not Firecracker acceptance. Agreed remote read facts/cuts are explicit inputs.
"""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path


def packed(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def fingerprint(value):
    return hashlib.sha256(packed(value)).hexdigest()


SNAPSHOT = [10, "T/attempt-1"]
SOURCE = [{"account": "alpha", "score": 7, "shard": "us"},
          {"account": "beta", "score": 3, "shard": "ap"}]


def router(source_bytes):
    """An exact-input-byte invocation emits a query, then returns completely."""
    rows = json.loads(source_bytes)
    chosen = min(rows, key=lambda row: (-row["score"], row["account"]))
    return packed({"snapshot": SNAPSHOT, "shard": chosen["shard"],
                   "account": chosen["account"], "query": "SUM(open_exposure)", "limit": 10})


def policy(query_bytes, answer_bytes):
    query, answer = json.loads(query_bytes), json.loads(answer_bytes)
    assert answer["snapshot"] == query["snapshot"]
    assert answer["account"] == query["account"]
    accepted = answer["exposure"] <= query["limit"]
    return packed({"accepted": accepted,
                   "writes": {f'{query["shard"]}/decision/{query["account"]}': answer["exposure"]}
                   if accepted else {},
                   "reason": "within_limit" if accepted else "limit_exceeded"})


QUERY = router(packed(SOURCE))
ANSWER = packed({"snapshot": SNAPSHOT, "account": "alpha", "exposure": 8})


def fixture(remote=True, first="wasm", second="firecracker", verify_first=True, verify_second=True, exposure=8):
    expected_answer = packed({"snapshot": SNAPSHOT, "account": "alpha", "exposure": exposure})
    answer = expected_answer if remote else None
    return {"epoch": "execution/E10", "source": packed(SOURCE).hex(),
            "remote_facts": {QUERY.hex(): answer.hex()} if answer else {},
            "runtime": {"router": first, "policy": second},
            "accepted_exact_bytes": {
                "router": QUERY.hex() if verify_first else None,
                "policy": policy(QUERY, expected_answer).hex() if verify_second else None},
            "regional": {"eu/regional/0": 0, "eu/regional/1": 0, "eu/regional/2": 0}}


def initial():
    return {"done": [], "bytes": {}, "regional": {}, "messages": {}, "invocations": {}}


def enabled(state, inputs):
    done = set(state["done"])
    actions = [f"regional/{number}" for number in range(3) if f"regional/{number}" not in done]
    if "read" not in done:
        actions.append("read")
    if "read" in done and "router" not in done:
        actions.append("router")
    if "router" in done and "router_verified" not in done:
        if (inputs["runtime"]["router"] == "wasm"
                or inputs["accepted_exact_bytes"]["router"] == state["bytes"]["query"]):
            actions.append("router_verified")
    if "router_verified" in done and "remote" not in done:
        if state["bytes"]["query"] in inputs["remote_facts"]:
            actions.append("remote")
    if "remote" in done and "policy" not in done:
        actions.append("policy")
    if "policy" in done and "policy_verified" not in done:
        if (inputs["runtime"]["policy"] == "wasm"
                or inputs["accepted_exact_bytes"]["policy"] == state["bytes"]["result"]):
            actions.append("policy_verified")
    return sorted(actions)


def transition(state, inputs, action):
    assert action in enabled(state, inputs)
    result = deepcopy(state)
    result["done"].append(action)
    result["done"].sort()
    if action.startswith("regional/"):
        key = "eu/" + action
        result["regional"][key] = inputs["regional"][key] + 1
    elif action == "read":
        result["bytes"]["source"] = inputs["source"]
    elif action == "router":
        output = router(bytes.fromhex(result["bytes"]["source"]))
        result["bytes"]["query"] = output.hex()
        result["invocations"]["router"] = {"epoch": inputs["epoch"],
            "runtime": inputs["runtime"]["router"], "input": result["bytes"]["source"],
            "output": output.hex()}
    elif action == "router_verified":
        result["messages"]["T/attempt-1/query/1"] = result["bytes"]["query"]
    elif action == "remote":
        result["bytes"]["answer"] = inputs["remote_facts"][result["bytes"]["query"]]
    elif action == "policy":
        output = policy(bytes.fromhex(result["bytes"]["query"]), bytes.fromhex(result["bytes"]["answer"]))
        result["bytes"]["result"] = output.hex()
        result["invocations"]["policy"] = {"epoch": inputs["epoch"],
            "runtime": inputs["runtime"]["policy"],
            "input": packed([result["bytes"]["query"], result["bytes"]["answer"]]).hex(),
            "output": output.hex()}
    elif action == "policy_verified":
        # A transaction's candidate values/results, not automatic database commit.
        result["messages"]["T/attempt-1/candidate/2"] = result["bytes"]["result"]
    return result


def summary(state, inputs):
    done = set(state["done"])
    unverified = [name for name, invocation in state["invocations"].items()
                  if invocation["runtime"] == "firecracker" and name + "_verified" not in done]
    if "policy_verified" in done:
        pending = None
    elif unverified:
        pending = {"kind": "verification", "invocations": sorted(unverified)}
    else:
        pending = {"kind": "remote_read", "query": state["bytes"].get("query")}
    return {"logical_state": state, "pending": pending,
            "epoch_publishable": not unverified,
            "internal_calls": len(state["invocations"]),
            "internal_witness_events": 0,
            "candidate_values_are_not_committed": True}


def all_fixpoints(inputs, start=None):
    terminal, schedules = {}, 0

    def visit(state):
        nonlocal schedules
        actions = enabled(state, inputs)
        if not actions:
            schedules += 1
            result = summary(state, inputs)
            terminal[fingerprint(result)] = result
        else:
            for action in actions:
                visit(transition(state, inputs, action))

    visit(deepcopy(start) if start is not None else initial())
    assert len(terminal) == 1, "permitted local schedules disagree on exact state/protocol bytes"
    return next(iter(terminal.values())), schedules


def ordered_fold(inputs, reversed_order=False):
    state = initial()
    while actions := enabled(state, inputs):
        state = transition(state, inputs, actions[-1] if reversed_order else actions[0])
    return summary(state, inputs)


def merge_messages(deliveries):
    agreed = {}
    for identifier, payload in deliveries:
        assert identifier not in agreed or agreed[identifier] == payload, "duplicate message has different exact bytes"
        agreed[identifier] = payload
    return agreed


def finish_times(nodes):
    """Small dependency evaluator; durations are authored illustrative wait units."""
    finished = {}
    while True:
        ready = [name for name, node in nodes.items()
                 if name not in finished and all(dep in finished for dep in node["after"])]
        if not ready:
            break
        for name in sorted(ready):
            node = nodes[name]
            finished[name] = max((finished[dep] for dep in node["after"]), default=0) + node["duration"]
    return finished, sorted(set(nodes) - set(finished))


def publication_graph(execution_owner, verification_delay=200):
    assert execution_owner in ("data-shard", "execution-shard")
    nodes = {"fc_execute": {"after": [], "duration": 1},
             "fc_verify": {"after": ["fc_execute"], "duration": verification_delay},
             "execution_epoch_visible": {"after": ["fc_verify"], "duration": 0}}
    for index in range(3):
        nodes[f"U{index}_compute"] = {"after": [], "duration": index + 1}
        predecessors = [f"U{index}_compute"]
        if index:
            predecessors.append(f"U{index - 1}_visible")
        if execution_owner == "data-shard":
            predecessors.append("execution_epoch_visible")
        nodes[f"U{index}_visible"] = {"after": predecessors, "duration": 0}
    finished, pending = finish_times(nodes)
    assert not pending
    return {"placement": execution_owner, "nodes": nodes, "finish": finished,
            "unrelated_responses_delayed": sum(finished[f"U{i}_visible"] > i + 1 for i in range(3)),
            "unrelated_extra_wait": [finished[f"U{i}_visible"] - i - 1 for i in range(3)]}


def publication_cycle():
    # A synchronous FC invocation emits a query but does not return until its
    # result arrives. If that query needs the enclosing execution epoch visible,
    # verification and execution wait on each other. Pending does not cut this.
    nodes = {
        "query_authoritative": {"after": ["execution_epoch_visible"], "duration": 0},
        "remote_reply": {"after": ["query_authoritative"], "duration": 1},
        "fc_return": {"after": ["remote_reply"], "duration": 1},
        "fc_verify": {"after": ["fc_return"], "duration": 1},
        "execution_epoch_visible": {"after": ["fc_verify"], "duration": 0},
        "regional_compute": {"after": [], "duration": 1},
        "regional_visible": {"after": ["regional_compute", "execution_epoch_visible"], "duration": 0},
    }
    finished, pending = finish_times(nodes)
    assert finished == {"regional_compute": 1}
    assert "execution_epoch_visible" in pending
    return {"nodes": nodes, "finished": finished, "pending": pending}


def dynamic_candidate():
    # The discovered remote answer also determines a real output identity.
    from certification import Store
    from fixed_execution import FixedStore
    from write_admission import Admission

    initial_values = {"eu/route": 0, "us/exposure": 8,
                      "us/decision/low": None, "us/decision/high": None, "eu/regional": 0}
    scopes = {key: (key,) for key in initial_values}

    def output(values):
        amount = values["us/exposure"]
        return {"us/decision/low" if amount < 5 else "us/decision/high": amount}

    db = FixedStore(initial_values, scopes)
    admission = Admission({"T": (0, "T"), "other": (1, "other")})
    predicted = {"us/decision/low"}
    assert admission.request("T", predicted)
    attempt = db.begin_fixed("T", (10, "T"), predicted, admission.grants, "T")
    db.reserve_position(attempt, predicted)
    db.fix_position(attempt)
    db.publish_position(attempt, predicted)
    for scope in ("eu/route", "us/exposure"):
        assert db.capture(attempt, scope, wait=True)
    attempt.writes.update(output({key: value for read in attempt.reads.values() for key, value in read.items()}))
    try:
        db.seal_values(attempt)
    except AssertionError:
        pass
    else:
        raise AssertionError("fixed predicted coverage silently expanded")
    db.decide(attempt, False)
    db.release(attempt, predicted)
    admission.release_all("T")
    assert db.check_serial() == initial_values

    optimistic = Store(initial_values, scopes)
    tx = optimistic.begin("actual-output", (10, "actual-output"))
    for scope in ("eu/route", "us/exposure"):
        assert optimistic.capture(tx, scope)
    tx.writes.update(output({key: value for read in tx.reads.values() for key, value in read.items()}))
    assert optimistic.promise(tx, tx.writes)
    assert optimistic.choose(tx) == tx.s
    optimistic.decide(tx, True)
    optimistic.install(tx, tx.writes)
    assert optimistic.check_serial()["us/decision/high"] == 8

    broad = Admission({"T": (0, "T"), "other": (1, "other"), "regional": (2, "regional")})
    assert broad.request("T", ["us/decision/low", "us/decision/high"])
    assert not broad.request("other", ["us/decision/low"])
    assert broad.request("regional", ["eu/regional"])
    return {"predicted_fixed_outputs": sorted(predicted), "actual_outputs": sorted(tx.writes),
            "fixed_prediction": "whole attempt aborts safely",
            "optimistic_actual_outputs": "quiet history commits without predeclared output identities",
            "optimistic_progress": "not established; moving sources plus output-order pressure can require retry",
            "conservative_output_domain": "blocks low decision writer although actual output is high",
            "disjoint_regional_writer": "passes either output-only gate domain"}


def run_probes():
    results = []
    available, schedules = all_fixpoints(fixture())
    assert available["epoch_publishable"] and available["pending"] is None
    assert available["internal_calls"] == 2 and available["internal_witness_events"] == 0
    assert json.loads(bytes.fromhex(available["logical_state"]["bytes"]["result"])) == {
        "accepted": True, "writes": {"us/decision/alpha": 8}, "reason": "within_limit"}
    results.append({"name": "available_query_extension_query_extension_same_epoch", "schedules": schedules, **available})

    rejection, schedules = all_fixpoints(fixture(exposure=12))
    assert rejection["epoch_publishable"] and rejection["pending"] is None
    assert json.loads(bytes.fromhex(rejection["logical_state"]["bytes"]["result"])) == {
        "accepted": False, "writes": {}, "reason": "limit_exceeded"}
    results.append({"name": "conditional_business_rejection_preserves_exact_result", "schedules": schedules, **rejection})

    inputs = fixture(remote=False, first="firecracker", second="wasm")
    pending, schedules = all_fixpoints(inputs)
    assert pending["epoch_publishable"] and pending["pending"]["kind"] == "remote_read"
    assert pending["logical_state"]["invocations"]["router"]["epoch"] == "execution/E10"
    results.append({"name": "verified_query_emitter_closes_with_pending_remote_continuation", "schedules": schedules, **pending})

    resumed_inputs = fixture(first="firecracker", second="wasm")
    resumed_inputs["epoch"] = "execution/E11"
    resumed, schedules = all_fixpoints(resumed_inputs, pending["logical_state"])
    assert resumed["epoch_publishable"] and resumed["pending"] is None
    invocations = resumed["logical_state"]["invocations"]
    assert invocations["router"]["epoch"] == "execution/E10"
    assert invocations["policy"]["epoch"] == "execution/E11"
    assert resumed["logical_state"]["bytes"]["source"] == pending["logical_state"]["bytes"]["source"]
    results.append({"name": "later_agreed_remote_fact_resumes_exact_continuation", "schedules": schedules, **resumed})

    blocked, schedules = all_fixpoints(fixture(remote=False, first="firecracker", verify_first=False))
    assert not blocked["epoch_publishable"]
    assert len(blocked["logical_state"]["regional"]) == 3
    assert not blocked["logical_state"]["messages"]
    results.append({"name": "pending_does_not_waive_firecracker_verification", "schedules": schedules, **blocked})

    # Independent consumers/origins derive exactly the same message IDs/bytes.
    first = ordered_fold(fixture())
    second = ordered_fold(fixture(), reversed_order=True)
    assert first == second
    messages = list(first["logical_state"]["messages"].items())
    assert merge_messages(messages * 3) == merge_messages(list(reversed(messages)) * 2)
    identifier, payload = messages[0]
    try:
        merge_messages(messages + [(identifier, (bytes.fromhex(payload) + b" ").hex())])
    except AssertionError:
        pass
    else:
        raise AssertionError("semantically equivalent JSON with different exact bytes was accepted")
    results.append({"name": "independent_origins_and_duplicate_routes_preserve_exact_bytes",
                    "messages": dict(messages), "route_or_duplicate_order_changes_state": False})

    shared = publication_graph("data-shard")
    isolated = publication_graph("execution-shard")
    assert shared["unrelated_responses_delayed"] == 3
    assert isolated["unrelated_responses_delayed"] == 0
    results.extend([{"name": "verification_in_data_epoch_propagates_publication_wait", **shared},
                    {"name": "execution_owned_epoch_isolates_verification_publication_wait", **isolated},
                    {"name": "synchronous_fc_query_visibility_cycle", **publication_cycle()},
                    {"name": "dynamic_outputs_compare_contracts", **dynamic_candidate()}])
    return {"source_sha256": {name: hashlib.sha256((Path(__file__).parent / name).read_bytes()).hexdigest()
                for name in ("composition_probe.py", "certification.py", "fixed_execution.py", "write_admission.py")},
            "scope": "Small exact-byte logical histories and dependency graphs; no dissemination design or implementation.",
            "assumptions": [
                "Every remote reply/cut supplied to a fold is already agreed and read-certified; this probe does not create that authority.",
                "Available internal calls need no individual witness event; missing remote facts produce an agreed continuation instead of a replica-local timing decision.",
                "Pure functions stand for hardened WASM or completed Firecracker invocations. Accepted-byte facts abstract BLAKE3 verification; no cryptographic verifier or VM is implemented.",
                "Reference fold state containing an unverified invocation is tentative and unpublishable; enumerating its pure reference function does not establish agreement of unverified physical VM executions.",
                "A genuinely slow or unverified FC invocation gates its execution epoch even if the transaction is Pending.",
                "Execution-owned placement is an explicit architecture option: it moves where the invocation actually runs, not the verification gate after the fact.",
                "An invocation that emits an unavailable database query returns that query as its complete output; an unbounded synchronous callback needs a different admission/authority contract.",
                "Candidate write messages are not committed data. Atomic transaction certification, predicates, recovery and retained history remain separate obligations.",
                "Dependency durations are illustrative logical wait units, not measured timings."],
            "scenarios": results, "passed": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run_probes()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    for scenario in result["scenarios"]:
        print("PASS:", scenario["name"])


if __name__ == "__main__":
    main()
