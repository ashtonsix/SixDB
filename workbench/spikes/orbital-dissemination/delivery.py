#!/usr/bin/env python3
"""Bounded delivery obligations, not a consensus or persistence implementation.

Durability, admission evidence, snapshot correctness and ownership grants are
trusted input facts. Atomic methods expose where a real implementation would
need an atomic durable transition. No clocks or network timings live here.
"""

from dataclasses import dataclass, field
import hashlib
import json
import math


class ProtocolError(ValueError):
    pass


class Backpressure(RuntimeError):
    pass


class GapTooOld(RuntimeError):
    pass


@dataclass(frozen=True)
class Lane:
    lineage: str
    recipient: str
    stream: str


@dataclass(frozen=True)
class Message:
    lane: Lane
    sequence: int
    payload: bytes
    admitted: bool = True


@dataclass(frozen=True)
class Packet:
    message: Message
    target_incarnation: str
    route_version: int


@dataclass(frozen=True)
class Ack:
    lane: Lane
    persisted_through: int
    applied_through: int


@dataclass
class DurableLane:
    lane: Lane
    capacity: int = 64
    records: dict[int, Message] = field(default_factory=dict)
    persisted_through: int = 0
    applied_through: int = 0
    retired_through: int = 0
    owner_generation: int = 0
    # Unbounded audit instrumentation, excluded from the modeled storage budget.
    applied_counts: dict[int, int] = field(default_factory=dict)

    def accept(self, message: Message) -> str:
        """Atomic durable acceptance; a receive interrupt must not call this early."""
        if message.lane != self.lane or message.sequence < 1:
            raise ProtocolError("wrong logical lane or sequence")
        if message.sequence <= self.retired_through:
            return "retired"
        previous = self.records.get(message.sequence)
        if previous is not None:
            if previous.payload != message.payload:
                raise ProtocolError("same identity names different payloads")
            if message.admitted and not previous.admitted:
                self.records[message.sequence] = message
            return "duplicate"
        if len(self.records) >= self.capacity:
            raise Backpressure("durable delivery ledger full")
        self.records[message.sequence] = message
        while self.persisted_through + 1 in self.records:
            self.persisted_through += 1
        return "accepted"

    def apply_ready(self) -> list[int]:
        """Assume each internal effect and its completion record commit atomically."""
        applied = []
        while self.applied_through + 1 in self.records:
            sequence = self.applied_through + 1
            if not self.records[sequence].admitted:
                break
            self.applied_counts[sequence] = self.applied_counts.get(sequence, 0) + 1
            self.applied_through = sequence
            applied.append(sequence)
        return applied

    def retire_results(self, through: int) -> None:
        """Trusted release of results; retain a durable floor rejecting stale retries."""
        if not self.retired_through <= through <= self.applied_through:
            raise ProtocolError("cannot retire unfinished effects")
        self.retired_through = through
        self.records = {n: m for n, m in self.records.items() if n > through}

    def ack(self) -> Ack:
        return Ack(self.lane, self.persisted_through, self.applied_through)


class Endpoint:
    def __init__(self, store: DurableLane, incarnation: str, capacity: int = 8):
        # This stands for an externally granted, enforced ownership transition.
        store.owner_generation += 1
        self.store = store
        self.generation = store.owner_generation
        self.incarnation = incarnation
        self.capacity = capacity
        self.volatile: dict[int, Message] = {}
        self.alive = True

    def check_owner(self) -> None:
        if not self.alive or self.generation != self.store.owner_generation:
            raise ProtocolError("dead or fenced incarnation")

    def receive(self, packet: Packet) -> str:
        self.check_owner()
        if packet.target_incarnation != self.incarnation:
            return "stale-target"
        message = packet.message
        if message.lane != self.store.lane or message.sequence < 1:
            raise ProtocolError("wrong logical lane or sequence")
        previous = self.volatile.get(message.sequence)
        if previous is not None:
            if previous.payload != message.payload:
                raise ProtocolError("same identity names different payloads")
            if message.admitted and not previous.admitted:
                self.volatile[message.sequence] = message
            return "duplicate-receive"
        if len(self.volatile) >= self.capacity:
            raise Backpressure("volatile receive queue full")
        # Route versions are deliberately absent from the obligation identity.
        self.volatile[message.sequence] = message
        return "received"

    def persist(self, sequence: int) -> str:
        self.check_owner()
        result = self.store.accept(self.volatile[sequence])
        del self.volatile[sequence]
        return result

    def apply_ready(self) -> list[int]:
        self.check_owner()
        return self.store.apply_ready()

    def crash(self) -> None:
        self.alive = False
        self.volatile.clear()


@dataclass
class Subscription:
    barrier: int
    accepted_through: int
    snapshot_installed: bool = False
    pending: set[int] = field(default_factory=set)

    @property
    def release_through(self) -> int:
        # The snapshot at barrier is independently pinned/verified by assumption.
        return self.accepted_through if self.snapshot_installed else self.barrier


class RetainedLog:
    """One stream; capture_join atomically pins the snapshot root and future tail."""

    def __init__(self, capacity: int):
        self.capacity = capacity
        self.latest = 0
        self.trimmed_through = 0
        self.events: set[int] = set()
        self.subscriptions: dict[str, Subscription] = {}

    def append(self) -> int:
        if len(self.events) >= self.capacity:
            raise Backpressure("retained tail full; no silent eviction")
        self.latest += 1
        self.events.add(self.latest)
        return self.latest

    def capture_join(self, recipient: str, barrier: int | None = None) -> None:
        if recipient in self.subscriptions:
            raise ProtocolError("logical recipient already subscribed")
        barrier = self.latest if barrier is None else barrier
        if barrier < self.trimmed_through:
            raise GapTooOld("requested snapshot no longer has a complete tail")
        if not 0 <= barrier <= self.latest:
            raise ProtocolError("invalid snapshot barrier")
        self.subscriptions[recipient] = Subscription(barrier, barrier)

    def receive_tail(self, recipient: str, sequence: int) -> None:
        sub = self.subscriptions[recipient]
        if sequence <= sub.accepted_through:
            return
        if sequence not in self.events:
            raise GapTooOld("tail unavailable or event not yet published")
        sub.pending.add(sequence)
        while sub.accepted_through + 1 in sub.pending:
            sub.accepted_through += 1
            sub.pending.remove(sub.accepted_through)

    def install_snapshot(self, recipient: str, barrier: int) -> None:
        sub = self.subscriptions[recipient]
        if barrier != sub.barrier:
            raise ProtocolError("snapshot and pinned tail use different cuts")
        sub.snapshot_installed = True

    def ready(self, recipient: str, through: int) -> bool:
        sub = self.subscriptions[recipient]
        return sub.snapshot_installed and sub.accepted_through >= through

    def missing(self, recipient: str, through: int | None = None) -> list[int]:
        """Obligations derive from subscription membership, not an old route tree."""
        sub = self.subscriptions[recipient]
        through = self.latest if through is None else through
        return [n for n in range(sub.accepted_through + 1, through + 1)
                if n not in sub.pending]

    def trim(self) -> int:
        through = min((s.release_through for s in self.subscriptions.values()),
                      default=self.latest)
        self.events.difference_update(range(self.trimmed_through + 1, through + 1))
        self.trimmed_through = through
        return through


@dataclass(frozen=True)
class Frontier:
    lineage: str
    stream: str
    through: int
    kind: str = "persisted-contiguous"


def aggregate_frontiers(reports: list[Frontier]) -> dict[tuple[str, str], int]:
    """Pointwise max of trusted contiguous-persistence reports for each stream.

    Does not certify producer persistence or witness admission. An aggregate
    forwarded on another in-tree can safely duplicate these monotone claims.
    """
    result: dict[tuple[str, str], int] = {}
    for report in reports:
        if report.kind != "persisted-contiguous" or report.through < 0:
            raise ProtocolError("frontier requires contiguous persistence evidence")
        key = (report.lineage, report.stream)
        result[key] = max(result.get(key, 0), report.through)
    return result


@dataclass(frozen=True)
class Sender:
    name: str
    failure_domain: str
    weight: float = 1.0


def sender_order(identity: tuple[str, ...], membership: str, entropy: str,
                 senders: list[Sender], diverse_first: bool = True) -> list[str]:
    """Weighted rendezvous candidates; selection supplies no exclusive authority.

    Pre-agree candidate names, weights, entropy version and identity encoding.
    Domain diversity can reorder backups after the primary has been selected.
    """
    if len({s.name for s in senders}) != len(senders):
        raise ProtocolError("duplicate candidate name")
    ranked = []
    for sender in senders:
        if not math.isfinite(sender.weight) or sender.weight <= 0:
            raise ProtocolError("candidate weights must be finite and positive")
        encoded = json.dumps([identity, membership, entropy, sender.name],
                             separators=(",", ":"), ensure_ascii=True).encode()
        integer = int.from_bytes(hashlib.blake2b(encoded, digest_size=8).digest(), "big")
        # Strictly inside (0, 1), including under binary64 rounding.
        uniform = ((integer >> 12) + 0.5) / (1 << 52)
        ranked.append((-math.log(uniform) / sender.weight, sender.name, sender))
    ordered = [s for _, _, s in sorted(ranked)]
    if diverse_first:
        distinct, repeated, domains = [], [], set()
        for sender in ordered:
            (repeated if sender.failure_domain in domains else distinct).append(sender)
            domains.add(sender.failure_domain)
        ordered = distinct + repeated
    return [s.name for s in ordered]


@dataclass
class ExternalSink:
    idempotent: bool = False
    effects: int = 0
    keys: set[str] = field(default_factory=set)

    def send(self, key: str) -> None:
        """Sink mutation is separate from the sender's durable completion marker."""
        if not self.idempotent or key not in self.keys:
            self.effects += 1
            self.keys.add(key)


@dataclass(frozen=True)
class ConditionalEntry:
    lineage: str
    configuration: str
    position: int
    payloads: tuple[tuple[str, str], ...]  # (stable event ID, exact content digest)


@dataclass(frozen=True)
class JournalCopy:
    witness: str
    ballot: int
    entry: ConditionalEntry
    complete_durable_write: bool = True


@dataclass(frozen=True)
class PayloadCopy:
    event: str
    digest: str
    holder: str
    failure_domain: str
    complete_durable_write: bool = True
    retention_promised: bool = True


def qualifies_early_certificate(entry: ConditionalEntry, journals: list[JournalCopy],
                               payloads: list[PayloadCopy], witnesses: set[str],
                               quorum_size: int = 2) -> bool:
    """Guard for a proposed conditional-acceptance certificate, not consensus.

    Assumes authentic same-ballot journal facts and fixed configured witnesses.
    The protocol making those facts legal under recovery remains unimplemented.
    A historical valid choice does not become unchosen if a payload later fails.
    """
    if quorum_size < 1 or quorum_size > len(witnesses):
        raise ProtocolError("invalid quorum")
    eligible_journals = [j for j in journals if j.entry == entry
                        and j.witness in witnesses and j.complete_durable_write]
    ballots = {j.ballot for j in eligible_journals}
    has_journal_quorum = any(
        len({j.witness for j in eligible_journals if j.ballot == ballot}) >= quorum_size
        for ballot in ballots)
    if not has_journal_quorum or not entry.payloads:
        return False
    for event, digest in entry.payloads:
        copies = [p for p in payloads if (p.event, p.digest) == (event, digest)
                  and p.complete_durable_write and p.retention_promised]
        if len({p.holder for p in copies}) < 2 or len({p.failure_domain for p in copies}) < 2:
            return False
    return True
