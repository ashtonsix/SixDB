#!/usr/bin/env python3
"""Exact predicate selection as one generic message-enhancement experiment.

Synthetic service times; application-supplied immutable data, cut, predicate,
coverage and deterministic transform validity are assumed. Reuses the shared
network core without changing its resource or transport model.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
import hashlib
import json
import math
from pathlib import Path
import random

from experiments import network
from simulator import Outcomes, Sim, resource_report


@dataclass(frozen=True)
class Binding:
    dataset: str
    version: str
    cut: str
    predicate: str
    row_domain: str
    mapping: str


@dataclass(frozen=True)
class Selection:
    binding: Binding
    encoding: str
    data: object
    rows: int
    bytes: int
    complete: bool = True


class BindingError(ValueError):
    pass


def selection(binding, matches, physical_ids):
    """Smallest of exact bitmap, uint32 stable IDs, uint32 (start,length) spans."""
    if len(set(physical_ids)) != len(physical_ids):
        raise BindingError("physical mapping has duplicate logical IDs")
    matches = frozenset(matches)
    if not matches <= set(physical_ids):
        raise BindingError("matches lie outside declared row domain")
    if any(not 0 <= row < (1 << 32) for row in physical_ids):
        raise BindingError("fixture uses uint32 logical IDs")
    positions = [i for i, row in enumerate(physical_ids) if row in matches]
    mask = bytearray(math.ceil(len(physical_ids) / 8))
    spans = []
    for position in positions:
        mask[position // 8] |= 1 << (position % 8)
        if spans and position == spans[-1][0] + spans[-1][1]:
            start, length = spans[-1]
            spans[-1] = (start, length + 1)
        else:
            spans.append((position, 1))
    candidates = [(len(mask), "bitmap", bytes(mask)),
                  (4 * len(matches), "row_ids", tuple(sorted(matches))),
                  (8 * len(spans), "spans", tuple(spans))]
    amount, encoding, data = min(candidates, key=lambda item: (item[0], item[1]))
    # 112 bytes represents input/predicate/cut/domain/encoding/coverage binding.
    return Selection(binding, encoding, data, len(physical_ids), 112 + amount)


def decode(selected, expected, physical_ids):
    bound = selected.binding
    for field in ("dataset", "version", "cut", "predicate", "row_domain"):
        if getattr(bound, field) != getattr(expected, field):
            raise BindingError("selection disagrees on " + field)
    if not selected.complete:
        raise BindingError("no complete domain-coverage assertion")
    if selected.rows != len(physical_ids):
        raise BindingError("row-domain length mismatch")
    if selected.encoding in ("bitmap", "spans") and bound.mapping != expected.mapping:
        raise BindingError("physical selection requires matching row mapping")
    if selected.encoding == "row_ids":
        result = frozenset(selected.data)
    elif selected.encoding == "bitmap":
        if len(selected.data) != math.ceil(selected.rows / 8):
            raise BindingError("truncated or oversized exact bitmap")
        result = frozenset(row for i, row in enumerate(physical_ids)
                           if selected.data[i // 8] & (1 << (i % 8)))
    elif selected.encoding == "spans":
        if any(start < 0 or length <= 0 or start + length > selected.rows
               for start, length in selected.data):
            raise BindingError("invalid exact span")
        result = frozenset(physical_ids[i] for start, length in selected.data
                           for i in range(start, start + length))
    else:
        raise BindingError("unknown exact encoding")
    if not result <= set(physical_ids):
        raise BindingError("selection outside declared domain")
    return result


def selection_reference(selected):
    """Content reference includes validity/coverage metadata, not only bit bytes."""
    data = selected.data.hex() if isinstance(selected.data, bytes) else selected.data
    canonical = [selected.binding.__dict__, selected.encoding, data,
                 selected.rows, selected.complete]
    return hashlib.sha256(json.dumps(canonical, sort_keys=True).encode()).hexdigest()


def resolve_reference(reference, cache, expected, physical_ids):
    selected = cache.get(reference)
    if selected is None:
        raise BindingError("selection reference is unavailable; fetch remains owed")
    if selection_reference(selected) != reference:
        raise BindingError("cached selection does not match its content reference")
    return decode(selected, expected, physical_ids)


def run_enhancement(config, policy):
    if policy not in ("raw", "blocking", "optional", "edge_selective"):
        raise ValueError("unknown enhancement policy")
    count = int(config.get("count", 8))
    rate = float(config.get("rate", 1000))
    rows = int(config.get("rows", 4096))
    row_bytes = int(config.get("row_bytes", 32))
    selectivity = float(config.get("selectivity", .1))
    if min(count, rate, rows, row_bytes) <= 0 or not 0 <= selectivity <= 1:
        raise ValueError("invalid workload")
    sim = Sim(config.get("seed", 1))
    names = {"origin": "a", "relay": "a", "local": "a", "az": "b", "public": "public"}
    net_config = {"retry_us": 5000, "queue_bytes": 1 << 20, "slow_extra_us": 0, **config}
    net = network(sim, names, net_config)
    recipients = ("local", "az", "public")
    residency = config.get("residency", "all")
    if residency not in ("all", "none", "mixed"):
        raise ValueError("unknown residency")
    declared_resident = {c: residency == "all" or residency == "mixed" and c != "az"
                         for c in recipients}
    # Stale resident copies cannot service this query's cut; fetch matching bytes.
    stale = set(config.get("stale_residents", []))
    resident = {c: value and c not in stale for c, value in declared_resident.items()}
    relay_resident = bool(config.get("relay_resident", True))
    physical_ids = tuple(range(rows))
    base_bytes = rows * row_bytes
    predicate_us = rows * float(config.get("predicate_us_per_row", .05))
    encode_us = rows * float(config.get("encode_us_per_row", .0005))
    count_matches = int(rows * selectivity)
    cutoff = count * 1e6 / rate
    drain = cutoff + float(config.get("drain_us", 100000))
    out = Outcomes(sim)
    by_recipient = {c: Outcomes(sim) for c in recipients}
    encoding_counts, selection_sizes, chosen_paths = {}, [], {}

    def compute_predicate(node, done, *, enhanced=False):
        duration = predicate_us + (encode_us if enhanced else 0)
        accepted = net.compute(node, duration, base_bytes, done, "data")
        if accepted:
            sim.count["predicate_invocations"] += 1
            sim.count["predicate_work_us"] += predicate_us
            if enhanced:
                sim.count["encoding_work_us"] += encode_us

    def offer(key):
        out.offer(key)
        for endpoint in by_recipient.values():
            endpoint.offer(key)
        predicate_id = (f"fixture-v1:{config.get('pattern', 'scattered')}:"
                        f"{count_matches}:{config.get('seed', 1)}:{key}")
        # Distinct immutable input chunks avoid assuming repeated cold fetches of
        # the same artifact without accounting for a possible cross-query cache.
        binding = Binding(f"table-chunk-{key}", "version-7", "cut-41", predicate_id,
                          f"rows-0-{rows}", "mapping-3")
        if config.get("pattern", "scattered") == "clustered":
            start = (key * 31) % max(1, rows - count_matches + 1)
            matches = frozenset(range(start, start + count_matches))
        else:
            matches = frozenset(random.Random(config.get("seed", 1) * 10000 + key)
                                .sample(physical_ids, count_matches))
        selected = selection(binding, matches, physical_ids)
        encoding_counts[selected.encoding] = encoding_counts.get(selected.encoding, 0) + 1
        selection_sizes.append(selected.bytes)
        state = {c: dict(base=resident[c], raw_arrived=False, selected=None,
                         raw_started=False, decoding=False, done=False) for c in recipients}
        done_recipients = set()

        def finish(c, result):
            if result != matches:
                raise AssertionError("different exact predicate result")
            if state[c]["done"]:
                sim.count["redundant_completed_results"] += 1
                return
            state[c]["done"] = True
            sim.count["useful_result_rows"] += len(result)
            sim.count["useful_result_bytes"] += len(result) * row_bytes
            if not result:
                sim.count["complete_empty_results"] += 1
            by_recipient[c].finish(key)
            done_recipients.add(c)
            if len(done_recipients) == len(recipients):
                out.finish(key)

        def use_selection(c):
            s = state[c]
            if s["done"] or s["decoding"] or not s["base"] or s["selected"] is None:
                return False
            try:
                result = decode(s["selected"], binding, physical_ids)
            except BindingError:
                sim.count["rejected_selection_bindings"] += 1
                s["selected"] = None
                return False
            s["decoding"] = True
            duration = .2 + len(result) * float(config.get("decode_us_per_match", .001))
            accepted = net.compute(c, duration, max(1, s["selected"].bytes),
                                   lambda: finish(c, result), "data")
            if accepted:
                sim.count["selection_decode_work_us"] += duration
                sim.count["selection_decodes"] += 1
            return True

        def raw_arrived(c):
            s = state[c]
            s["base"] = True
            s["raw_arrived"] = True
            if s["done"] or use_selection(c) or s["raw_started"]:
                return
            s["raw_started"] = True
            compute_predicate(c, lambda: finish(c, matches))

        def send_raw(c):
            amount = 64 + (0 if resident[c] else base_bytes)
            sim.count["raw_base_payload_bytes"] += 0 if resident[c] else base_bytes
            sim.count["request_payload_bytes"] += 64
            net.send("origin", c, amount, lambda: raw_arrived(c))

        def enhanced_arrived(c, packet_selection, materialized_rows):
            s = state[c]
            try:
                decode(packet_selection, binding, physical_ids)
            except BindingError:
                sim.count["rejected_selection_bindings"] += 1
                # Optional remains able to complete its raw path. A blocking
                # invalid enhancement stays unfinished; no guessed answer.
                return
            s["selected"] = packet_selection
            if materialized_rows:
                s["base"] = True  # Exact selected rows suffice for this result.
            use_selection(c)

        if policy == "raw":
            enhanced_targets = []
            raw_targets = list(recipients)
        elif policy == "blocking":
            enhanced_targets = list(recipients)
            raw_targets = []
        elif policy == "optional":
            enhanced_targets = list(recipients)
            raw_targets = list(recipients)
        else:
            # Authored deployment rule, no future-arrival oracle: cheap-edge
            # resident consumers share compute; public resident consumer filters
            # locally; nonresident consumers can receive selected rows anywhere.
            enhanced_targets = [c for c in recipients if names[c] != "public" or not resident[c]]
            raw_targets = [c for c in recipients if c not in enhanced_targets]
        chosen_paths.update({c: "both" if c in enhanced_targets and c in raw_targets else
                              "enhanced" if c in enhanced_targets else "raw" for c in recipients})
        for c in raw_targets:
            send_raw(c)
        if not enhanced_targets:
            return

        def at_relay():
            def transformed():
                for c in enhanced_targets:
                    packet_selection = selected
                    if config.get("stale_optional_sidecar", False) and policy == "optional" and c == "public":
                        packet_selection = replace(selected,
                                                   binding=replace(binding, cut="old-cut"))
                    materialized = not resident[c] and policy != "optional"
                    row_amount = len(matches) * row_bytes if materialized else 0
                    # Even empty matches send a complete bound selection; absence
                    # of traffic cannot mean an exact empty result.
                    amount = packet_selection.bytes + row_amount
                    sim.count["selection_payload_bytes"] += packet_selection.bytes
                    sim.count["selected_row_payload_bytes"] += row_amount
                    net.send("relay", c, amount,
                             lambda c=c, packet_selection=packet_selection, materialized=materialized:
                             enhanced_arrived(c, packet_selection, materialized))
            compute_predicate("relay", transformed, enhanced=True)
        sim.count["relay_input_payload_bytes"] += 0 if relay_resident else base_bytes
        net.send("origin", "relay", 64 + (0 if relay_resident else base_bytes), at_relay)

    for key in range(count):
        sim.at(key * 1e6 / rate, lambda key=key: offer(key))
    sim.run(cutoff)
    at_cutoff = {name: resource.used for name, resource in sim.resources.items()}
    sim.run(drain)
    report = out.summary(cutoff, drain)
    report.update(policy=policy, config=config, count_matches=count_matches,
                  declared_resident=declared_resident, usable_resident=resident,
                  relay_resident=relay_resident, chosen_paths=chosen_paths,
                  encoding_counts=encoding_counts,
                  selection_bytes_min=min(selection_sizes), selection_bytes_max=max(selection_sizes),
                  required_recipient_completions=count * len(recipients),
                  completed_recipients=sum(len(value.completed) for value in by_recipient.values()),
                  by_recipient={c: value.summary(cutoff, drain) for c, value in by_recipient.items()},
                  counters=dict(sim.count), resources=resource_report(sim),
                  total_cpu_service_us=sum(r.busy_us for name, r in sim.resources.items()
                                           if name.endswith(":cpu")),
                  resource_bytes_at_cutoff=sum(at_cutoff.values()),
                  unacked_transfers=sum(t.reliable and not t.acked for t in net.transfers))
    return report


def campaign(count=8):
    cases = []
    for residency in ("all", "none"):
        for fraction in (.001, .1, .8):
            cases.append(dict(name=f"{residency}-scattered-{fraction}", count=count,
                              residency=residency, selectivity=fraction))
    cases += [dict(name="mixed-cheap-predicate", count=count, residency="mixed",
                   selectivity=.1, predicate_us_per_row=.001),
              dict(name="mixed-clustered-dense", count=count, residency="mixed",
                   selectivity=.8, pattern="clustered"),
              dict(name="resident-empty", count=count, residency="all", selectivity=0),
              dict(name="stale-base-cold-relay", count=count, residency="all",
                   selectivity=.001, stale_residents=["public"], relay_resident=False)]
    return cases


def source_hashes():
    directory = Path(__file__).resolve().parent
    names = ("enhancement_study.py", "simulator.py", "experiments.py")
    return {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, help="one workload JSON; all four policies compared")
    parser.add_argument("--count", type=int, default=8)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before = source_hashes()
    configs = [json.loads(args.config.read_text())] if args.config else campaign(args.count)
    results = []
    for config in configs:
        rows = [run_enhancement(config, policy)
                for policy in ("raw", "blocking", "optional", "edge_selective")]
        results.append(dict(config=config, comparisons=rows))
        print(config.get("name", "custom"), flush=True)
    after = source_hashes()
    if before != after:
        raise RuntimeError("enhancement sources changed during comparison")
    output = dict(kind="authored exact-selection enhancement; synthetic timing and cost",
                  source_hashes_before=before, source_hashes_after=after,
                  selection="All four policies for every declared case; includes empty and stale-base cases",
                  cases=results)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    print(f"{len(results)} workloads / {len(results) * 4} comparisons -> {args.output}")


if __name__ == "__main__":
    main()
