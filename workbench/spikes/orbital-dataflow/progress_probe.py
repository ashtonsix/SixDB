#!/usr/bin/env python3
"""Bounded semantic dataflow probes; counts are logical work, never timings.

Durability, source seals, ownership and transaction acceptance are supplied facts.
This file implements neither a distributed progress protocol nor consensus.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import platform
from typing import Iterable


Row = tuple[str, str]
Joined = tuple[str, str, str]
Time = tuple[int, int]
Edge = tuple[int, int]


def clean(values: Counter) -> Counter:
    """Counter unary plus would incorrectly discard negative changes."""
    return Counter({key: value for key, value in values.items() if value})


def add_counts(*bags: Counter) -> Counter:
    result = Counter()
    for bag in bags:
        result.update(bag)
    return clean(result)


def difference(new: Counter, old: Counter) -> Counter:
    return add_counts(new, Counter({key: -value for key, value in old.items()}))


def records(bag: Counter) -> list[dict]:
    return [{"row": list(row), "weight": weight}
            for row, weight in sorted(bag.items()) if weight]


def weighted_join(left: Counter, right: Counter) -> Counter:
    """Bag equijoin, accepting signed changes as well as positive relations."""
    index = defaultdict(list)
    for (key, value), weight in right.items():
        index[key].append((value, weight))
    output = Counter()
    for (key, value), weight in left.items():
        for other, other_weight in index[key]:
            output[key, value, other] += weight * other_weight
    return clean(output)


def delta_join(left: Counter, right: Counter,
               delta_left: Counter, delta_right: Counter) -> Counter:
    return add_counts(weighted_join(delta_left, right),
                      weighted_join(left, delta_right),
                      weighted_join(delta_left, delta_right))


def unmatched(left: Counter, right: Counter) -> Counter:
    keys = {key for (key, _), weight in right.items() if weight > 0}
    return Counter({row: weight for row, weight in left.items()
                    if weight > 0 and row[0] not in keys})


@dataclass(frozen=True)
class Packet:
    lane: str
    sequence: int
    updates: tuple[tuple[Row, int], ...]


class EpochInput:
    """Finite declared input coverage. A source seal is an external fact.

    All lanes must seal, including empty lanes. Seals can precede payloads;
    receiving a high sequence number does not fill gaps. Physical attempt IDs
    do not enter the logical packet key.
    """

    def __init__(self, epoch: int, sides: dict[str, str]):
        if not sides or set(sides.values()) - {"L", "R"}:
            raise ValueError("declare nonempty L/R lane coverage")
        self.epoch = epoch
        self.sides = dict(sides)
        self.packets: dict[tuple[str, int], Packet] = {}
        self.seals: dict[str, int] = {}
        self.duplicates = 0

    def receive(self, packet: Packet) -> None:
        if packet.lane not in self.sides or packet.sequence < 1:
            raise ValueError("packet is outside declared coverage")
        key = packet.lane, packet.sequence
        if key in self.packets:
            if self.packets[key] != packet:
                raise ValueError("logical packet identity has conflicting bytes")
            self.duplicates += 1
            return
        if packet.sequence > self.seals.get(packet.lane, packet.sequence):
            raise ValueError("packet exceeds closed input cut")
        self.packets[key] = packet

    def seal(self, lane: str, last: int) -> None:
        if lane not in self.sides or last < 0:
            raise ValueError("invalid source seal")
        if lane in self.seals and self.seals[lane] != last:
            raise ValueError("source changed its sealed cut")
        if any(name == lane and sequence > last for name, sequence in self.packets):
            raise ValueError("seal excludes already supplied input")
        self.seals[lane] = last

    def complete(self) -> bool:
        return all(lane in self.seals and
                   all((lane, number) in self.packets
                       for number in range(1, self.seals[lane] + 1))
                   for lane in self.sides)

    def changes(self) -> tuple[Counter, Counter]:
        if not self.complete():
            raise ValueError("input coverage is incomplete")
        sides = {"L": Counter(), "R": Counter()}
        for key in sorted(self.packets):
            packet = self.packets[key]
            for row, weight in packet.updates:
                sides[self.sides[packet.lane]][row] += weight
        return clean(sides["L"]), clean(sides["R"])


@dataclass
class Candidate:
    flow: str
    epoch: int
    base_epoch: int
    left: Counter
    right: Counter
    joined: Counter
    absent: Counter
    join_delta: Counter
    absent_delta: Counter
    manifest: str

    def output(self) -> list[tuple[str, tuple, int]]:
        return [("join", row, weight) for row, weight in sorted(self.join_delta.items())] + [
            ("absent", row, weight) for row, weight in sorted(self.absent_delta.items())]


class Store:
    """Atomic in-memory stand-in for one accepted application transition.

    Accepted state includes offsets, derived relations and retained output IDs.
    It is intentionally an assumption boundary, not a persistence algorithm.
    """

    def __init__(self, flow: str = "orders/code-v1"):
        self.flow = flow
        self.epoch = 0
        self.left, self.right = Counter(), Counter()
        self.joined, self.absent = Counter(), Counter()
        self.accepted: dict[int, str] = {}
        self.outbox: dict[tuple[str, int, int], tuple[str, tuple, int]] = {}

    def prepare(self, inputs: EpochInput) -> Candidate:
        if inputs.epoch != self.epoch + 1:
            raise ValueError("candidate must extend the captured state cut")
        dl, dr = inputs.changes()
        left, right = add_counts(self.left, dl), add_counts(self.right, dr)
        if any(weight < 0 for bag in (left, right) for weight in bag.values()):
            raise ValueError("negative base multiplicity")
        change = delta_join(self.left, self.right, dl, dr)
        joined = add_counts(self.joined, change)
        absent = unmatched(left, right)
        payload = {"flow": self.flow, "epoch": inputs.epoch,
                   "base_epoch": self.epoch, "left": records(left),
                   "right": records(right), "joined": records(joined),
                   "absent": records(absent), "join_delta": records(change),
                   "absent_delta": records(difference(absent, self.absent)),
                   "seals": sorted(inputs.seals.items()),
                   "packets": [(key, packet.updates)
                               for key, packet in sorted(inputs.packets.items())]}
        manifest = hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()
        return Candidate(self.flow, inputs.epoch, self.epoch, left, right, joined,
                         absent, change, difference(absent, self.absent), manifest)

    def accept(self, candidate: Candidate, *, checks_complete: bool,
               authorised: bool) -> bool:
        if candidate.flow != self.flow:
            raise ValueError("wrong application/code identity")
        if candidate.epoch in self.accepted:
            if self.accepted[candidate.epoch] != candidate.manifest:
                raise ValueError("conflicting accepted outcome identity")
            return False
        if not checks_complete or not authorised:
            raise ValueError("progress does not grant publication authority")
        if candidate.base_epoch != self.epoch or candidate.epoch != self.epoch + 1:
            raise ValueError("stale candidate")
        # One logical transition: physical crash atomicity is explicitly supplied.
        self.left, self.right = candidate.left.copy(), candidate.right.copy()
        self.joined, self.absent = candidate.joined.copy(), candidate.absent.copy()
        self.epoch = candidate.epoch
        self.accepted[candidate.epoch] = candidate.manifest
        for number, item in enumerate(candidate.output()):
            self.outbox[self.flow, candidate.epoch, number] = item
        return True


def leq(left: Time, right: Time) -> bool:
    return left[0] <= right[0] and left[1] <= right[1]


def frontier(times: Iterable[Time]) -> tuple[Time, ...]:
    unique = set(times)
    return tuple(sorted(time for time in unique
                        if not any(other != time and leq(other, time) for other in unique)))


class Progress:
    """Exact central accounting for one loop; no distributed path summaries.

    'operator' can produce messages even with an empty queue. A message itself
    owns a progress token. Atomic handover avoids an artificial zero-token gap.
    """

    def __init__(self):
        self.tokens: dict[str, tuple[str, Time]] = {}
        self.observed = False

    def hold(self, identity: str, kind: str, time: Time) -> None:
        if self.observed or identity in self.tokens or kind not in {"operator", "message"}:
            raise ValueError("invalid token")
        self.tokens[identity] = kind, time

    def transfer(self, identity: str,
                 replacements: tuple[tuple[str, str, Time], ...]) -> None:
        old_time = self.tokens[identity][1]
        names = [name for name, _, _ in replacements]
        if len(names) != len(set(names)) or any(name in self.tokens for name in names):
            raise ValueError("replacement token collision")
        if any(not leq(old_time, time) or kind not in {"operator", "message"}
               for _, kind, time in replacements):
            raise ValueError("invalid or backwards progress transition")
        self.observed = True
        del self.tokens[identity]
        for name, kind, time in replacements:
            self.tokens[name] = kind, time

    def at(self) -> tuple[Time, ...]:
        self.observed = True
        return frontier(time for _, time in self.tokens.values())

    def complete_at(self, time: Time) -> bool:
        return not any(leq(held, time) for held in self.at())

    def outer_complete(self, epoch: int) -> bool:
        self.observed = True
        return not any(time[0] <= epoch for _, time in self.tokens.values())


def adjacency(edges: set[Edge], reverse: bool = False) -> dict[int, set[int]]:
    index = defaultdict(set)
    for source, target in edges:
        index[target if reverse else source].add(source if reverse else target)
    return index


def reachable_full(edges: set[Edge], root: int = 0) -> tuple[set[int], int]:
    index = adjacency(edges)
    result, pending = {root}, deque([root])
    visits = 0
    while pending:
        source = pending.popleft()
        for target in sorted(index[source]):
            visits += 1
            if target not in result:
                result.add(target)
                pending.append(target)
    return result, visits


@dataclass
class ReachResult:
    reachable: set[int]
    overdeleted: set[int]
    after_overdelete: set[int]
    edge_visits: int
    index_updates: int
    rederived: int
    worklist_peak: int


def update_reachability(old_edges: set[Edge], old_reachable: set[int],
                        added: set[Edge], removed: set[Edge], root: int = 0) -> ReachResult:
    """Single-source set reachability, scoped overdelete and rederive.

    This is a specialized DRed-style comparison, not a general Datalog engine.
    Index construction/copying is excluded equally from traversal counts. In a
    retained implementation this candidate needs both incoming/outgoing indexes;
    full recomputation only needs outgoing adjacency.
    """
    if added & removed or added & old_edges or not removed <= old_edges:
        raise ValueError("batch must contain actual, disjoint edge changes")
    old_index = adjacency(old_edges)
    doomed = {target for source, target in removed
              if source in old_reachable and target != root}
    pending = deque(sorted(doomed))
    visits, peak = len(removed), len(pending)
    while pending:
        source = pending.popleft()
        for target in sorted(old_index[source]):
            visits += 1
            if target in old_reachable and target != root and target not in doomed:
                doomed.add(target)
                pending.append(target)
        peak = max(peak, len(pending))
    remaining = old_reachable - doomed
    intermediate = remaining.copy()
    edges = (old_edges - removed) | added
    forward, backward = adjacency(edges), adjacency(edges, reverse=True)
    seeds = set()
    for target in sorted(doomed):
        for source in sorted(backward[target]):
            visits += 1
            if source in remaining:
                seeds.add(target)
                break
    for source, target in sorted(added):
        visits += 1
        if source in remaining and target not in remaining:
            seeds.add(target)
    pending = deque(sorted(seeds))
    remaining.update(seeds)
    peak = max(peak, len(pending))
    while pending:
        source = pending.popleft()
        for target in sorted(forward[source]):
            visits += 1
            if target not in remaining:
                remaining.add(target)
                pending.append(target)
        peak = max(peak, len(pending))
    return ReachResult(remaining, doomed, intermediate, visits,
                       len(added) + len(removed), len(doomed & remaining), peak)


def naive_support_delete(edges: set[Edge], reachable: set[int],
                         removed: Edge, root: int = 0) -> set[int]:
    """Negative control: local support counts can preserve an unrooted cycle."""
    index = adjacency(edges - {removed})
    count = Counter({root: 1})
    for source, target in edges - {removed}:
        if source in reachable:
            count[target] += 1
    pending = deque(node for node in reachable if count[node] == 0)
    result = reachable.copy()
    while pending:
        node = pending.popleft()
        if node not in result:
            continue
        result.remove(node)
        for target in index[node]:
            count[target] -= 1
            if count[target] == 0:
                pending.append(target)
    return result


class Retention:
    """Finite byte reservations with independently owned lifetime pins."""

    def __init__(self, capacity: int):
        self.capacity = capacity
        self.objects: dict[str, tuple[int, set[str]]] = {}

    def size(self) -> int:
        return sum(size for size, _ in self.objects.values())

    def add(self, identity: str, size: int, owners: set[str]) -> bool:
        if identity in self.objects or size < 0 or not owners:
            raise ValueError("invalid retained object")
        if self.size() + size > self.capacity:
            return False
        self.objects[identity] = size, set(owners)
        return True

    def release(self, owner: str) -> None:
        for identity, (_, owners) in list(self.objects.items()):
            owners.discard(owner)
            if not owners:
                del self.objects[identity]

    def describe(self) -> dict:
        return {"retained_bytes": self.size(),
                "objects": {identity: {"bytes": size, "owners": sorted(owners)}
                            for identity, (size, owners) in sorted(self.objects.items())}}


def epoch_input(epoch: int, left: Counter, right: Counter) -> EpochInput:
    inputs = EpochInput(epoch, {"orders/0": "L", "orders/1": "L",
                               "payments/0": "R", "payments/1": "R"})
    for lane, side in inputs.sides.items():
        changes = left if side == "L" else right
        if lane.endswith("/0") and changes:
            inputs.receive(Packet(lane, 1, tuple(sorted(changes.items()))))
            inputs.seal(lane, 1)
        else:
            inputs.seal(lane, 0)
    return inputs


def join_histories() -> list[dict]:
    store = Store()
    histories = []
    changes = [
        ("same-cut cross term", Counter({("a", "order-a"): 1}),
         Counter({("a", "payment-a"): 1})),
        ("unmatched order at complete cut", Counter({("b", "order-b/event-9"): 1}), Counter()),
        ("later accepted correction of old event time", Counter(),
         Counter({("b", "payment-b/event-9"): 1})),
        ("delete the last matching payment", Counter(),
         Counter({("a", "payment-a"): -1})),
        ("duplicate-valued bag rows are real multiplicity", Counter({("b", "order-b/event-9"): 1}), Counter()),
        ("same-cut deletion of both sides", Counter({("b", "order-b/event-9"): -2}),
         Counter({("b", "payment-b/event-9"): -1})),
    ]
    for epoch, (name, dl, dr) in enumerate(changes, 1):
        inputs = epoch_input(epoch, dl, dr)
        candidate = store.prepare(inputs)
        store.accept(candidate, checks_complete=True, authorised=True)
        histories.append({"epoch": epoch, "case": name, "left_changes": records(dl),
                          "right_changes": records(dr), "joined": records(store.joined),
                          "unmatched": records(store.absent),
                          "join_delta": records(candidate.join_delta),
                          "unmatched_delta": records(candidate.absent_delta)})
    return histories


def coverage_and_progress() -> dict:
    inputs = EpochInput(1, {"orders": "L", "payments": "R"})
    order = Packet("orders", 1, ((("a", "order"), 1),))
    payment1 = Packet("payments", 1, ((("b", "unrelated-payment"), 1),))
    payment2 = Packet("payments", 2, ((("a", "late-payment"), 1),))
    inputs.receive(order)
    inputs.seal("orders", 1)
    stages = [{"stage": "queues empty; payment source has not sealed", "complete": inputs.complete()}]
    inputs.seal("payments", 2)
    inputs.receive(payment2)
    stages.append({"stage": "seal and sequence 2 present; sequence 1 missing", "complete": inputs.complete()})
    inputs.receive(payment2)
    inputs.receive(payment1)
    candidate = Store().prepare(inputs)
    stages.append({"stage": "gap repaired, replay suppressed", "complete": inputs.complete(),
                   "duplicates": inputs.duplicates, "unmatched": records(candidate.absent)})
    gates = []
    for checks, authority in ((False, False), (True, False), (False, True), (True, True)):
        try:
            Store().accept(candidate, checks_complete=checks, authorised=authority)
            accepted = True
        except ValueError:
            accepted = False
        gates.append({"checks_complete": checks, "authority": authority, "published": accepted})
    progress = Progress()
    progress.hold("worker", "operator", (7, 0))
    states = [{"stage": "empty queues; worker may still emit", "epoch_complete": progress.outer_complete(7)}]
    progress.transfer("worker", (("feedback", "message", (7, 1)),))
    states.append({"stage": "worker handed token to feedback message", "epoch_complete": progress.outer_complete(7)})
    progress.transfer("feedback", ())
    states.append({"stage": "all work and messages retired", "epoch_complete": progress.outer_complete(7)})
    incomparable = ((0, 2), (1, 0))
    query = (1, 1)
    premature = unmatched(Counter(dict(order.updates)), Counter())
    maintained = Store()
    first = maintained.prepare(epoch_input(1, Counter({("a", "old-order"): 1}), Counter()))
    maintained.accept(first, checks_complete=True, authorised=True)
    forgotten = Store()
    forgotten.accept(first, checks_complete=True, authorised=True)
    # Negative control: confuse completed input time with the lifetime of a
    # stateful join's arrangement, then accept a later matching payment.
    forgotten.left.clear()
    later = epoch_input(2, Counter(), Counter({("a", "new-payment"): 1}))
    correct_later, unsafe_later = maintained.prepare(later), forgotten.prepare(later)
    return {"coverage": stages, "publication_gates": gates, "loop_progress": states,
            "partial_order": {"frontier": frontier(incomparable), "query": query,
                              "complete": not any(leq(time, query) for time in incomparable),
                              "unsafe_lexicographic_min_complete": not leq(min(incomparable), query)},
            "timeout_antijoin_negative_control": {
                "published_without_payment_seal": records(premature),
                "correct_after_missing_payment": records(candidate.absent),
                "policy": "provisional output requires retraction; irreversible action cannot be recalled"},
            "arrangement_eviction_negative_control": {
                "correct_later_join": records(correct_later.joined),
                "later_join_after_frontier_based_eviction": records(unsafe_later.joined),
                "reason": "completed input epoch does not end future uses of retained join state"}}


def split_checkpoint_histories() -> list[dict]:
    """Actually execute the two split-checkpoint failure orders."""
    histories = []
    for emit_first in (True, False):
        cut, external = 0, Counter()
        source = epoch_input(1, Counter({("a", "order"): 1}),
                             Counter({("a", "payment"): 1}))
        candidate = Store().prepare(source)
        if emit_first:
            external.update(candidate.join_delta)
        else:
            cut = 1
        # Crash discards the private candidate. Restart trusts the persisted cut.
        if cut < source.epoch:
            replay = Store().prepare(source)
            external.update(replay.join_delta)
            cut = source.epoch
        histories.append({"crash": "emit delta before persisting its input cut" if emit_first
                          else "persist input cut before derived state/output",
                          "negative_control": True, "recovered_cut": cut,
                          "sink_effects_without_dedup": sum(external.values()),
                          "sink_relation": records(external)})
    observations = []
    for first_send_reached_sink in (False, True):
        effects = int(first_send_reached_sink)
        local = {"intent": "orders/code-v1/epoch-1/record-0", "receipt": None}
        before = effects
        effects += 1  # Replay the committed intent; the remote sink has no dedup.
        observations.append({"local_observation": local, "sink_effects_before_retry": before,
                             "sink_effects_after_blind_retry": effects})
    histories.append({"crash": "external reply lost", "negative_control": True,
                      "indistinguishable_histories": observations})
    return histories


def recovery_histories() -> list[dict]:
    histories = []
    for point in ("before_prepare", "after_prepare", "after_accept", "after_sink_before_receipt"):
        durable = Store()
        inputs = epoch_input(1, Counter({("a", "order"): 1}),
                             Counter({("a", "payment"): 1}))
        candidate = None
        sink_seen = set()
        sink_effects = 0
        if point != "before_prepare":
            candidate = durable.prepare(inputs)
        if point in {"after_accept", "after_sink_before_receipt"}:
            durable.accept(candidate, checks_complete=True, authorised=True)
        if point == "after_sink_before_receipt":
            for identity in durable.outbox:
                sink_seen.add(identity)
                sink_effects += 1
        # Lose disposable private execution. Recover durable accepted state or
        # replay its retained inputs; never silently advance only the offset.
        if durable.epoch == 0:
            candidate = durable.prepare(inputs)
            durable.accept(candidate, checks_complete=True, authorised=True)
        elif candidate is not None:
            durable.accept(candidate, checks_complete=True, authorised=True)
        sends = 0
        for identity in durable.outbox:
            sends += 1
            if identity not in sink_seen:
                sink_seen.add(identity)
                sink_effects += 1
        histories.append({"crash": point, "accepted_epoch": durable.epoch,
                          "joined": records(durable.joined), "retained_output_records": len(durable.outbox),
                          "sink_effects_with_sink_atomic_dedup": sink_effects,
                          "recovery_sends": sends})
    histories.extend(split_checkpoint_histories())
    return histories


def recursive_bag_nontermination() -> dict:
    edges = {(0, 1), (1, 2), (2, 1)}
    current, accumulated = Counter({0: 1}), Counter({0: 1})
    rounds = []
    for iteration in range(1, 9):
        following = Counter()
        for source, target in sorted(edges):
            following[target] += current[source]
        current = clean(following)
        accumulated.update(current)
        rounds.append({"iteration": iteration, "new_walks": sum(current.values()),
                       "total_walks": sum(accumulated.values())})
    return {"case": "UNION ALL counts walks through a cycle",
            "round_limit": len(rounds), "iterations": rounds,
            "remaining_work": sum(current.values()), "completed": not current,
            "set_reachability_nodes": len(reachable_full(edges)[0])}


def recursive_histories() -> list[dict]:
    chain = {(node, node + 1) for node in range(200)}
    branch = {(0, 1)} | {(node, node + 1) for node in range(1, 200)} | {(200, 1)}
    diamond = {(0, 1), (0, 2), (1, 3), (2, 3)} | {(node, node + 1) for node in range(3, 200)}
    scenarios = [
        ("cycle loses its only rooted support", {(0, 1), (1, 2), (2, 1)}, set(), {(0, 1)}),
        ("local leaf deletion", chain, set(), {(199, 200)}),
        ("bridge deletion discards almost everything", branch, set(), {(0, 1)}),
        ("redundant path deletion rederives a long suffix", diamond, set(), {(1, 3)}),
        ("local insertion into an existing chain", chain, {(200, 201)}, set()),
        ("new bridge reaches a previously disconnected cycle", {(0, 1), (2, 3), (3, 2)}, {(1, 2)}, set()),
        ("same-cut replacement bridge", {(0, 1), (1, 2), (2, 1), (0, 3)}, {(3, 2)}, {(0, 1)}),
    ]
    rows = []
    for name, edges, added, removed in scenarios:
        before, _ = reachable_full(edges)
        result = update_reachability(edges, before, added, removed)
        full, visits = reachable_full((edges - removed) | added)
        rows.append({"case": name, "initial_edges": [list(edge) for edge in sorted(edges)]
                     if len(edges) < 10 else {"generator": "recursive_histories", "count": len(edges)},
                     "added": sorted(added), "removed": sorted(removed),
                     "before_nodes": len(before), "after_nodes": len(full),
                     "correct": result.reachable == full, "incremental_edge_visits": result.edge_visits,
                     "full_edge_visits": visits, "index_updates": result.index_updates,
                     "overdeleted_nodes": len(result.overdeleted), "rederived_nodes": result.rederived,
                     "worklist_peak_nodes": result.worklist_peak,
                     "partial_overdelete_is_final": result.after_overdelete == full,
                     "incremental_adjacency_entries": 2 * len((edges - removed) | added),
                     "full_adjacency_entries": len((edges - removed) | added)})
    cycle = scenarios[0][1]
    before, _ = reachable_full(cycle)
    rows[0]["naive_support_count_result"] = sorted(naive_support_delete(cycle, before, (0, 1)))
    rows[0]["correct_result"] = [0]
    return rows


def retention_history() -> dict:
    retention = Retention(5504)
    assert retention.add("input@1", 1024, {"worker", "replay"})
    assert retention.add("result@1", 256, {"worker", "client"})
    assert retention.add("committed-intent@1", 128, {"dispatcher"})
    assert retention.add("state@0", 4096, {"snapshot-reader"})
    states = [{"stage": "all owners active", **retention.describe()}]
    refused = not retention.add("next-input", 1, {"next-worker"})
    for stage, owner in (("client cancels interest", "client"),
                         ("worker reaches actual stopping point", "worker"),
                         ("accepted checkpoint covers replay cut", "replay"),
                         ("independent snapshot reader finishes", "snapshot-reader"),
                         ("external intent obtains qualifying receipt", "dispatcher")):
        retention.release(owner)
        states.append({"stage": stage, **retention.describe()})
    return {"capacity_bytes": retention.capacity, "over_capacity_offer_refused": refused,
            "states": states}


def report() -> dict:
    path = Path(__file__).resolve()
    sources = {name: hashlib.sha256(path.with_name(name).read_bytes()).hexdigest()
               for name in ("progress_probe.py", "check_progress.py")}
    return {"study": "finite dataflow progress, incremental changes and recovery",
            "python": platform.python_version(), "source_sha256": sources,
            "units": "logical tuples, edge entries, modeled bytes; no elapsed timings",
            "assumptions": ["source seals and closed lane membership are supplied facts",
                            "one accepted application transition is atomic and durable",
                            "sink deduplication is atomic where explicitly selected",
                            "source inputs and code identity survive replay",
                            "progress accounting is exact central state, not a distributed implementation"],
            "join_histories": join_histories(), "coverage_progress_publication": coverage_and_progress(),
            "recovery_histories": recovery_histories(), "recursive_histories": recursive_histories(),
            "recursive_bag_nontermination": recursive_bag_nontermination(),
            "retention": retention_history()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    encoded = json.dumps(report(), indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
