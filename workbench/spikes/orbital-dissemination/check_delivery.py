#!/usr/bin/env python3
"""Small adversarial histories for delivery.py; logical checks, no timings."""

import argparse
from collections import Counter
import hashlib
from itertools import permutations
import json
from pathlib import Path

from delivery import (Backpressure, ConditionalEntry, DurableLane, Endpoint,
                      ExternalSink, Frontier, GapTooOld, JournalCopy, Lane, Message,
                      Packet, PayloadCopy, ProtocolError, RetainedLog, Sender,
                      aggregate_frontiers, qualifies_early_certificate, sender_order)


LANE = Lane("history-A", "extension-7", "producer-2")


def message(sequence, *, admitted=True, payload=None):
    return Message(LANE, sequence, payload or str(sequence).encode(), admitted)


def deliver(endpoint, sequence, route=1, admitted=True):
    endpoint.receive(Packet(message(sequence, admitted=admitted),
                            endpoint.incarnation, route))
    endpoint.persist(sequence)


def rejects(error, action):
    try:
        action()
    except error:
        return
    raise AssertionError(f"expected {error.__name__}")


def check_reorder_duplicate_histories():
    histories = sorted(set(permutations((1, 2, 2, 3))))
    for history in histories:
        store = DurableLane(LANE)
        endpoint = Endpoint(store, "vm-1/boot-1")
        received = set()
        for sequence in history:
            deliver(endpoint, sequence)
            received.add(sequence)
            endpoint.apply_ready()
            prefix = 0
            while prefix + 1 in received:
                prefix += 1
            assert store.persisted_through == prefix
            assert store.applied_through == prefix
            assert store.applied_counts == {n: 1 for n in range(1, prefix + 1)}
        assert store.applied_through == 3
    return {"histories": len(histories)}


def check_crash_every_boundary():
    boundaries = ("before-receive", "after-receive", "after-persist", "after-effect",
                  "after-ack")
    for boundary in boundaries:
        store = DurableLane(LANE)
        endpoint = Endpoint(store, "vm-1/boot-1")
        packet = Packet(message(1), endpoint.incarnation, 1)
        if boundary != "before-receive":
            endpoint.receive(packet)
        if boundary in boundaries[2:]:
            endpoint.persist(1)
        if boundary in boundaries[3:]:
            endpoint.apply_ready()
        if boundary == "after-ack":
            assert store.ack().applied_through == 1
        endpoint.crash()
        restored = Endpoint(store, "vm-2/boot-7")
        assert restored.receive(packet) == "stale-target"
        deliver(restored, 1, route=2)
        restored.apply_ready()
        assert store.applied_counts == {1: 1}
    return {"histories": len(boundaries), "assumption": "durable ledger survives"}


def check_receipt_is_not_persistence_or_effect():
    store = DurableLane(LANE)
    endpoint = Endpoint(store, "vm-1")
    endpoint.receive(Packet(message(1), "vm-1", 1))
    assert store.ack().persisted_through == store.ack().applied_through == 0
    endpoint.persist(1)
    assert store.ack().persisted_through == 1
    assert store.ack().applied_through == 0
    endpoint.apply_ready()
    assert store.ack().applied_through == 1


def check_ownership_transfer_fences_old_incarnation():
    store = DurableLane(LANE)
    old = Endpoint(store, "vm-1")
    deliver(old, 1)
    old.apply_ready()
    new = Endpoint(store, "vm-2")
    rejects(ProtocolError, old.apply_ready)
    rejects(ProtocolError, lambda: deliver(old, 2))
    deliver(new, 1)
    new.apply_ready()
    assert store.applied_counts == {1: 1}


def check_route_change_preserves_obligation():
    store = DurableLane(LANE)
    endpoint = Endpoint(store, "vm-1")
    deliver(endpoint, 2, route=9)
    deliver(endpoint, 1, route=8)
    endpoint.apply_ready()
    deliver(endpoint, 1, route=10)
    endpoint.apply_ready()
    assert store.applied_counts == {1: 1, 2: 1}


def check_identity_and_payload_isolation():
    store = DurableLane(LANE)
    endpoint = Endpoint(store, "vm-1")
    for lane in (Lane("history-B", LANE.recipient, LANE.stream),
                 Lane(LANE.lineage, "extension-8", LANE.stream),
                 Lane(LANE.lineage, LANE.recipient, "producer-3")):
        packet = Packet(Message(lane, 1, b"1"), "vm-1", 1)
        rejects(ProtocolError, lambda: endpoint.receive(packet))
    deliver(endpoint, 1)
    endpoint.receive(Packet(message(1, payload=b"different"), "vm-1", 1))
    rejects(ProtocolError, lambda: endpoint.persist(1))


def check_tentative_payload_waits_for_authority():
    store = DurableLane(LANE)
    endpoint = Endpoint(store, "vm-1")
    deliver(endpoint, 1, admitted=False)
    deliver(endpoint, 2)
    assert store.persisted_through == 2
    assert endpoint.apply_ready() == []
    deliver(endpoint, 1, admitted=True)
    assert endpoint.apply_ready() == [1, 2]


def check_queue_exhaustion_does_not_forge_ack():
    store = DurableLane(LANE, capacity=1)
    endpoint = Endpoint(store, "vm-1", capacity=1)
    endpoint.receive(Packet(message(1), "vm-1", 1))
    rejects(Backpressure, lambda: endpoint.receive(Packet(message(2), "vm-1", 1)))
    assert store.persisted_through == 0
    endpoint.persist(1)
    endpoint.receive(Packet(message(2), "vm-1", 1))
    rejects(Backpressure, lambda: endpoint.persist(2))
    assert store.persisted_through == 1 and 2 in endpoint.volatile
    endpoint.apply_ready()
    store.retire_results(1)
    endpoint.persist(2)
    endpoint.apply_ready()
    assert store.applied_counts == {1: 1, 2: 1}


def check_retirement_keeps_a_stale_retry_floor():
    store = DurableLane(LANE)
    endpoint = Endpoint(store, "vm-1")
    deliver(endpoint, 1)
    rejects(ProtocolError, lambda: store.retire_results(1))
    endpoint.apply_ready()
    store.retire_results(1)
    assert not store.records
    deliver(endpoint, 1)
    endpoint.apply_ready()
    assert store.applied_counts == {1: 1}
    assert not store.records


def check_join_snapshot_tail_interleavings():
    histories = list(permutations(("snapshot", 2, 3)))
    for history in histories:
        log = RetainedLog(3)
        log.append()
        log.capture_join("new-vm")
        log.append()
        log.append()
        for event in history:
            if event == "snapshot":
                log.install_snapshot("new-vm", 1)
            else:
                log.receive_tail("new-vm", event)
            # Prefix release cannot forget the earlier gap or uninstalled snapshot.
            safe = log.subscriptions["new-vm"].release_through
            if event == 3 and 2 not in history[:history.index(event)]:
                assert safe == 1
            log.trim()
        assert log.ready("new-vm", 3)
        assert log.trimmed_through == 3
    return {"histories": len(histories)}


def check_join_after_old_route_has_sent():
    log = RetainedLog(4)
    log.append()
    log.capture_join("old")
    log.install_snapshot("old", 1)
    old_route_recipients = tuple(log.subscriptions)
    log.append()
    log.capture_join("new", barrier=1)
    for recipient in old_route_recipients:
        log.receive_tail(recipient, 2)
    assert log.missing("new") == [2]
    log.trim()
    assert 2 in log.events
    log.receive_tail("new", 2)
    assert not log.ready("new", 2)
    log.install_snapshot("new", 1)
    assert log.ready("new", 2)


def check_finite_tail_forces_backpressure():
    log = RetainedLog(2)
    log.capture_join("slow")
    log.append()
    log.append()
    log.receive_tail("slow", 2)
    log.trim()
    rejects(Backpressure, log.append)
    assert log.latest == 2 and log.events == {1, 2}
    log.receive_tail("slow", 1)
    log.trim()
    rejects(Backpressure, log.append)  # Snapshot is still uninstalled.
    log.install_snapshot("slow", 0)
    log.trim()
    assert log.append() == 3


def check_tail_gap_requires_explicit_resnapshot():
    log = RetainedLog(2)
    log.append()
    log.append()
    log.trim()
    rejects(GapTooOld, lambda: log.capture_join("late", barrier=0))
    log.capture_join("late", barrier=2)
    rejects(ProtocolError, lambda: log.install_snapshot("late", 1))
    log.install_snapshot("late", 2)
    assert log.ready("late", 2)


def check_slowest_required_recipient_controls_retention():
    log = RetainedLog(3)
    for name in ("fast", "slow"):
        log.capture_join(name)
        log.install_snapshot(name, 0)
    log.append()
    log.append()
    for n in (1, 2):
        log.receive_tail("fast", n)
    log.receive_tail("slow", 1)
    assert log.trim() == 1 and log.events == {2}


def check_frontier_aggregation_keeps_streams_and_lineages():
    reports = [Frontier("A", "p", 3), Frontier("A", "q", 2),
               Frontier("A", "p", 1), Frontier("B", "p", 8),
               Frontier("A", "p", 3)]
    expected = {("A", "p"): 3, ("A", "q"): 2, ("B", "p"): 8}
    histories = set(permutations(reports))
    for history in histories:
        assert aggregate_frontiers(list(history)) == expected
    rejects(ProtocolError, lambda: aggregate_frontiers([
        Frontier("A", "p", 8, kind="highest-packet-seen")]))
    return {"histories": len(histories)}


def check_sender_selection_is_repeatable_and_failure_diverse():
    senders = [Sender("a", "AZ1"), Sender("b", "AZ1"),
               Sender("c", "AZ2"), Sender("d", "AZ3")]
    winners = Counter()
    for n in range(256):
        identity = ("A", "p", str(n), "recipient")
        order = sender_order(identity, "members-1", "seed-1", senders)
        assert order == sender_order(identity, "members-1", "seed-1", senders[::-1])
        domains = {s.name: s.failure_domain for s in senders}
        assert len({domains[name] for name in order[:3]}) == 3
        winners[order[0]] += 1
    # A fixture sanity check, not a distribution quality or load-balance guarantee.
    assert set(winners) == {s.name for s in senders}
    return {"identities": 256, "primary_counts": dict(sorted(winners.items()))}


def check_sender_disagreement_and_lost_ack_duplicate_safely():
    candidates = [Sender("a", "AZ1"), Sender("b", "AZ2"), Sender("c", "AZ3")]
    different = None
    for n in range(256):
        identity = ("A", str(n), "recipient")
        left = sender_order(identity, "members-1", "seed", candidates)[0]
        right = sender_order(identity, "members-2", "seed", candidates)[0]
        if left != right:
            different = (left, right)
            break
    assert different
    store = DurableLane(LANE)
    endpoint = Endpoint(store, "vm-1")
    # Both primaries and then the timeout backup send the same logical obligation.
    for _ in (*different, "backup"):
        deliver(endpoint, 1)
        endpoint.apply_ready()
    assert store.applied_counts == {1: 1}


def check_failure_domain_loss_preserves_a_distinct_backup():
    candidates = [Sender("a", "AZ1"), Sender("b", "AZ1"), Sender("c", "AZ2")]
    order = sender_order(("message",), "members", "seed", candidates)
    domains = {s.name: s.failure_domain for s in candidates}
    survivors = [name for name in order if domains[name] != domains[order[0]]]
    assert survivors and domains[survivors[0]] != domains[order[0]]
    # This is candidate availability, not a simulated timer or liveness proof.


def check_external_effect_counterexamples():
    # Marker before send: durable decision says "sent", crash precedes actual send.
    before = ExternalSink()
    sent_marker = True
    if not sent_marker:
        before.send("intent-1")
    assert before.effects == 0
    # Marker after send: sink performed effect, sender crashes before recording it.
    after = ExternalSink()
    sent_marker = False
    after.send("intent-1")
    if not sent_marker:
        after.send("intent-1")
    assert after.effects == 2
    # A sink that atomically remembers the key with its effect can resolve this case.
    keyed = ExternalSink(idempotent=True)
    keyed.send("intent-1")
    keyed.send("intent-1")
    assert keyed.effects == 1
    return {"marker_before_effects": 0, "marker_after_effects": 2,
            "atomic_sink_key_effects": 1}


def check_lost_dedup_metadata_is_not_repaired_by_same_id():
    old = Endpoint(DurableLane(LANE), "vm-1")
    deliver(old, 1)
    old.apply_ready()
    old.crash()
    # Deliberately invalid recovery: original effect survived, its ledger did not.
    wrong = Endpoint(DurableLane(LANE), "vm-2")
    deliver(wrong, 1)
    wrong.apply_ready()
    assert old.store.applied_counts[1] + wrong.store.applied_counts[1] == 2
    return {"counterexample": "same ID plus empty replacement ledger repeats effect"}


def check_conditional_proposal_needs_both_durable_payload_holders():
    entry = ConditionalEntry("A", "config-1", 1, (("event-1", "digest-1"),))
    journals = [JournalCopy("L", 7, entry), JournalCopy("F", 7, entry)]
    producer = PayloadCopy("event-1", "digest-1", "P", "AZ-A")
    follower = PayloadCopy("event-1", "digest-1", "F", "AZ-B")
    voters = {"L", "F", "S"}
    assert not qualifies_early_certificate(entry, journals, [], voters)
    assert not qualifies_early_certificate(entry, journals, [producer], voters)
    assert qualifies_early_certificate(entry, journals, [producer, follower], voters)
    assert all(p.holder != "L" for p in (producer, follower))


def check_slow_follower_cannot_promote_bare_tentative_journals():
    entry = ConditionalEntry("A", "config-1", 1, (("event-1", "digest-1"),))
    journals = [JournalCopy("L", 7, entry), JournalCopy("S", 7, entry)]
    producer = PayloadCopy("event-1", "digest-1", "P", "AZ-A")
    assert not qualifies_early_certificate(entry, journals, [producer], {"L", "F", "S"})
    # This helper has no timeout rule that can turn this into an eligible choice.
    assert not qualifies_early_certificate(entry, journals, [producer], {"L", "F", "S"})


def check_interrupted_writes_are_not_eligibility_evidence():
    entry = ConditionalEntry("A", "config-1", 1, (("event-1", "digest-1"),))
    voters = {"L", "F", "S"}
    journals = [JournalCopy("L", 7, entry), JournalCopy("F", 7, entry)]
    producer = PayloadCopy("event-1", "digest-1", "P", "AZ-A")
    for follower in (PayloadCopy("event-1", "digest-1", "F", "AZ-B", False),
                     PayloadCopy("event-1", "digest-1", "F", "AZ-B", True, False),
                     PayloadCopy("event-1", "different", "F", "AZ-B"),
                     PayloadCopy("event-1", "digest-1", "F", "AZ-A")):
        assert not qualifies_early_certificate(entry, journals, [producer, follower], voters)
    complete = PayloadCopy("event-1", "digest-1", "F", "AZ-B")
    journals[1] = JournalCopy("F", 7, entry, False)
    assert not qualifies_early_certificate(entry, journals, [producer, complete], voters)


def check_every_payload_in_conditional_entry_is_covered():
    entry = ConditionalEntry("A", "config-1", 1,
                             (("event-1", "digest-1"), ("event-2", "digest-2")))
    journals = [JournalCopy("L", 7, entry), JournalCopy("F", 7, entry)]
    payloads = [PayloadCopy("event-1", "digest-1", "P", "AZ-A"),
                PayloadCopy("event-1", "digest-1", "F", "AZ-B")]
    assert not qualifies_early_certificate(entry, journals, payloads, {"L", "F", "S"})
    payloads += [PayloadCopy("event-2", "digest-2", "P", "AZ-A"),
                 PayloadCopy("event-2", "digest-2", "F", "AZ-B")]
    assert qualifies_early_certificate(entry, journals, payloads, {"L", "F", "S"})


def check_reconfiguration_cannot_relabel_old_conditional_votes():
    old = ConditionalEntry("A", "config-1", 1, (("event-1", "digest-1"),))
    new = ConditionalEntry("A", "config-2", 1, old.payloads)
    journals = [JournalCopy("L", 7, old), JournalCopy("F", 7, old)]
    payloads = [PayloadCopy("event-1", "digest-1", "P", "AZ-A"),
                PayloadCopy("event-1", "digest-1", "F", "AZ-B")]
    assert qualifies_early_certificate(old, journals, payloads, {"L", "F", "S"})
    assert not qualifies_early_certificate(new, journals, payloads, {"L", "F", "S"})
    assert not qualifies_early_certificate(old, journals, payloads, {"X", "Y", "Z"})
    mixed = [JournalCopy("L", 7, old), JournalCopy("F", 8, old)]
    assert not qualifies_early_certificate(old, mixed, payloads, {"L", "F", "S"})


def check_ineligible_early_slot_blocks_ready_later_stream():
    earlier = ConditionalEntry("A", "config-1", 1, (("p1:event-1", "digest-1"),))
    later = ConditionalEntry("A", "config-1", 2, (("p2:event-1", "digest-2"),))
    journals = [JournalCopy(witness, 7, entry)
                for entry in (earlier, later) for witness in ("L", "F")]
    payloads = [PayloadCopy("p1:event-1", "digest-1", "P1", "AZ-A"),
                PayloadCopy("p2:event-1", "digest-2", "P2", "AZ-A"),
                PayloadCopy("p2:event-1", "digest-2", "F", "AZ-B")]
    voters = {"L", "F", "S"}
    ready = [qualifies_early_certificate(e, journals, payloads, voters)
             for e in (earlier, later)]
    assert ready == [False, True]
    # Prefix publication stops at the first missing condition, despite p2 ready.
    contiguous = 0
    for valid in ready:
        if not valid:
            break
        contiguous += 1
    assert contiguous == 0
    payloads.append(PayloadCopy("p1:event-1", "digest-1", "F", "AZ-B"))
    assert all(qualifies_early_certificate(e, journals, payloads, voters)
               for e in (earlier, later))
    return {"ready_later_slot": 2, "blocked_admission_prefix": 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    checks = {}
    for name, check in sorted(globals().copy().items()):
        if name.startswith("check_") and callable(check):
            checks[name.removeprefix("check_")] = check() or {"passed": True}
    source = Path(__file__).resolve()
    output = {"kind": "bounded logical histories; no timing or consensus proof",
              "checks": checks,
              "source_sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in (source, source.with_name("delivery.py"))}}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(f"{len(checks)} delivery checks passed")
    print(json.dumps({name: value for name, value in checks.items()
                      if value != {"passed": True}}, indent=2))


if __name__ == "__main__":
    main()
