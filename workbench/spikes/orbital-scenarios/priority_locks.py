"""Direct wound-wait / wait-die decisions for the shared payload harness.

Priority is the original (arrival, transaction id), retained across restarts.
Requests acquire their entire part or wait. Older incompatible waiters retain
their place at the keys they requested; younger arrivals cannot barge ahead when
a holder releases. The caller charges control delivery and restarts each
victim's whole transaction. Collection observes current claims
completely and victim invalidation is atomic, as in the arbitration baseline.
This is a policy model, not a distributed lock manager or recovery protocol.
"""

from collections import Counter, defaultdict

from arbitration import Claim, conflict


def _shard(key):
    return key.split("/", 1)[0].split(":", 1)[0]


class PriorityLocks:
    def __init__(self, mode="wound-wait", shard_of=None, priority=None):
        if mode not in ("wound-wait", "wait-die"):
            raise ValueError("mode must be wound-wait or wait-die")
        self.mode = mode
        self.shard_of = shard_of or _shard
        self.grants = {}
        self.waiting = {}
        self.fences = {}  # Interface compatibility; these policies reserve no scope.
        self.counts = Counter()
        self._priority = {}
        self._pending = {}
        self._next_plan = 0
        self._register_priority(priority or {})

    def _register_priority(self, priority):
        for owner, value in priority.items():
            supplied = tuple(value)
            if owner in self._priority and self._priority[owner] != supplied:
                raise ValueError("transaction priority must survive restarts")
            self._priority[owner] = supplied

    def _order(self, owner):
        return self._priority[owner], owner

    def _queue_blockers(self, claim):
        return {
            ref: waiting for ref, waiting in self.waiting.items()
            if conflict(claim, waiting)
            and (waiting.owner not in self._priority
                 or claim.owner not in self._priority
                 or self._order(waiting.owner) < self._order(claim.owner))
        }

    def request(self, claim):
        """Grant a compatible whole part, otherwise retain its waiting request."""
        self.counts["requests"] += 1
        old = self.grants.get(claim.ref)
        if old is not None:
            if old != claim:
                raise ValueError("release a granted part before replacing its claim")
            return True
        self.waiting[claim.ref] = claim
        queued = self._queue_blockers(claim)
        if queued or any(conflict(claim, held) for held in self.grants.values()):
            self.counts["blocked"] += 1
            self.counts["handoff_waits"] += bool(queued)
            return False
        self.grants[claim.ref] = claim
        del self.waiting[claim.ref]
        self.counts["granted"] += 1
        return True

    def release(self, owner, from_part=0):
        """Release claims; whole-transaction restarts use the default part zero."""
        for table in (self.grants, self.waiting):
            for ref in list(table):
                if ref[0] == owner and ref[1] >= from_part:
                    del table[ref]
        # Pending messages still arrive. Their claim snapshots reject obsolete
        # decisions without silently allowing a duplicate concurrent decision.

    def collect(self, priority, irrevocable=None, weights=None):
        """Return delayed decisions from holders and priority handoff conflicts.

        ``weights`` is accepted for the Arb interface and deliberately unused.
        Pending decisions suppress duplicate initiators/victims. Admission
        respects only older incompatible waiters at the requested keys; there
        are no component unions or provisional component reservations. Wait-die
        treats an older queued waiter as a denial witness too, avoiding a cycle
        between a younger holder's new request and that older waiter.
        """
        irrevocable = set(irrevocable or ())
        self._register_priority(priority)
        order = self._order
        requests = defaultdict(list)
        for _, claim in sorted(self.waiting.items()):
            requests[claim.owner].append(claim)
        pending_requesters = {plan["requester"] for plan in self._pending.values()}
        pending_victims = {owner for plan in self._pending.values()
                           for owner, _ in plan["victims"]}
        plans = []
        for requester in sorted(requests, key=order):
            if requester in pending_requesters | pending_victims:
                self.counts["deferred_pending"] += 1
                continue
            relevant_requests = {}
            holders = {}
            queued = {}
            for requested in requests[requester]:
                for ref, held in self.grants.items():
                    if conflict(requested, held):
                        relevant_requests[requested.ref] = requested
                        holders[ref] = held
                if self.mode == "wait-die":
                    blockers = self._queue_blockers(requested)
                    if blockers:
                        relevant_requests[requested.ref] = requested
                        queued.update(blockers)
            holder_owners = {claim.owner for claim in holders.values()}
            blocker_owners = holder_owners | {claim.owner for claim in queued.values()}
            if self.mode == "wound-wait":
                victims = {owner for owner in holder_owners
                           if order(requester) < order(owner)
                           and owner not in irrevocable}
                # An already-issued wound can handle that holder; other direct
                # conflicts need not wait for the same decision to return.
                victims -= pending_victims
            else:
                victims = ({requester} if requester not in irrevocable
                           and any(order(owner) < order(requester)
                                   for owner in blocker_owners) else set())
            if not victims:
                continue
            if self.mode == "wait-die":
                # One older blocker suffices to justify death. Do not turn an
                # entire resource queue into a coordinated decision snapshot.
                witnesses = {min((owner for owner in blocker_owners
                                  if order(owner) < order(requester)), key=order)}
            else:
                witnesses = victims
            holders = {ref: claim for ref, claim in holders.items()
                       if claim.owner in witnesses}
            queued = {ref: claim for ref, claim in queued.items()
                      if claim.owner in witnesses}
            relevant_requests = {
                ref: claim for ref, claim in relevant_requests.items()
                if any(conflict(claim, witness)
                       for witness in (*holders.values(), *queued.values()))
            }
            blocker_owners = witnesses
            members = blocker_owners | {requester}
            member_claims = [claim for table in (self.grants, self.waiting)
                             for claim in table.values() if claim.owner in members]
            keys = frozenset(key for claim in member_claims
                             for key in claim.reads | claim.writes)
            shards = frozenset(self.shard_of(key) for key in keys)
            # Only direct conflict witnesses must remain unchanged. Unrelated
            # progress or discovery does not invalidate a priority decision.
            snapshot = {"grants": dict(holders), "waiting": {**relevant_requests, **queued}}
            self._next_plan += 1
            plan = {
                "id": self._next_plan,
                "mode": self.mode,
                "requester": requester,
                "members": frozenset(members),
                "selected": frozenset(members - victims),
                "winners": frozenset(members - victims),
                "victims": [(owner, 0) for owner in sorted(victims)],
                "snapshot": snapshot,
                "keys": keys,
                "shards": shards,
                "edges": len(blocker_owners),
                "local": len(shards) == 1,
            }
            self._pending[plan["id"]] = plan
            pending_requesters.add(requester)
            pending_victims.update(victims)
            self.counts["collected"] += 1
            self.counts["edges"] += plan["edges"]
            self.counts["claims"] += len(member_claims)
            self.counts["reports"] += len(shards)
            self.counts["max_participants"] = max(self.counts["max_participants"], len(members))
            plans.append(plan)
        return plans

    def verdict(self, plan, irrevocable=None):
        """Return whole-owner victims, or None for a stale/duplicate decision.

        ``selected`` identifies surviving participants, not grants or a promised
        independent set. Older or compatible arrivals may still acquire keys.
        The caller immediately releases/restarts returned victims atomically.
        """
        pending = self._pending.pop(plan["id"], None)
        stale = pending is None or pending != plan
        for name in ("grants", "waiting"):
            current = getattr(self, name)
            stale |= any(current.get(ref) != claim
                         for ref, claim in plan["snapshot"][name].items())
        stale |= bool({owner for owner, _ in plan["victims"]}
                      & set(irrevocable or ()))
        if stale:
            self.counts["obsolete"] += 1
            return None
        self.counts["verdicts"] += 1
        self.counts["wounds" if self.mode == "wound-wait" else "dies"] += len(plan["victims"])
        return list(plan["victims"])


def probe():
    """Policy checks under complete observation and atomic caller invalidation."""
    def c(owner, part=0, reads=(), writes=(), generation=0):
        return Claim(owner, part, frozenset(reads), frozenset(writes), generation)

    checks = 0
    for mode in ("wound-wait", "wait-die"):
        priority = {"old": (0, "old"), "young": (1, "young"),
                    "third": (2, "third"), "free": (3, "free")}
        locks = PriorityLocks(mode)
        assert locks.request(c("old", writes=["A/x"]))
        assert locks.request(c("young", writes=["B/y"]))
        assert not locks.request(c("old", 1, writes=["B/y"]))
        assert not locks.request(c("young", 1, writes=["A/x"]))
        plans = locks.collect(priority)
        assert len(plans) == 1 and plans[0]["victims"] == [("young", 0)]
        assert plans[0]["edges"] == 1 and not locks.fences
        assert locks.collect(priority) == []
        assert locks.request(c("free", writes=["C/free"]))
        victims = locks.verdict(plans[0])
        assert victims == [("young", 0)]
        for owner, part in victims:
            locks.release(owner, part)
        # A fast local restart cannot steal B/y while the older remote request
        # remains registered, even before that older request is retried.
        assert not locks.request(c("young", writes=["B/y"], generation=1))
        assert locks.request(c("old", 1, writes=["B/y"]))
        assert locks.verdict(plans[0]) is None
        locks.release("old")
        assert locks.request(c("young", writes=["B/y"], generation=1))
        locks.collect(priority)  # Stable priority survives the new generation.
        checks += 1

        stale = PriorityLocks(mode)
        holder, requester = (("young", "old") if mode == "wound-wait"
                             else ("old", "young"))
        assert stale.request(c(holder, writes=["A/x"]))
        assert not stale.request(c(requester, writes=["A/x"]))
        plan = stale.collect(priority)[0]
        stale.release(holder)
        stale.request(c(holder, writes=["A/x"], generation=1))
        assert stale.verdict(plan) is None
        checks += 1

        protected = PriorityLocks(mode)
        assert protected.request(c(holder, writes=["A/x"]))
        assert not protected.request(c(requester, writes=["A/x"]))
        plan = protected.collect(priority)[0]
        assert protected.verdict(plan, {"young"}) is None
        assert protected.collect(priority, {"young"}) == []
        checks += 1

        committed_holder = PriorityLocks(mode)
        assert committed_holder.request(c(holder, writes=["A/x"]))
        assert not committed_holder.request(c(requester, writes=["A/x"]))
        plans = committed_holder.collect(priority, {holder})
        if mode == "wound-wait":
            assert plans == []
        else:
            assert len(plans) == 1
            assert committed_holder.verdict(plans[0], {holder}) == [(requester, 0)]
        assert c(holder, writes=["A/x"]) in committed_holder.grants.values()
        checks += 1

        readers = PriorityLocks(mode, priority=priority)
        assert readers.request(c("old", reads=["A/shared"]))
        assert readers.request(c("young", reads=["A/shared"]))
        assert readers.request(c("old", 1, writes=["A/shared"])) is False
        assert readers.request(c("free", writes=["C/free"]))
        assert readers.request(c("free", 2, reads=["C/free"]))
        assert not readers.request(c("third", writes=["A/shared", "B/free"]))
        assert readers.request(c("old", 2, writes=["B/free"]))
        assert not readers.fences
        checks += 1

    handoff = PriorityLocks("wound-wait", priority=priority)
    assert handoff.request(c("young", reads=["A/x"]))
    assert not handoff.request(c("old", writes=["A/x"]))
    plan = handoff.collect(priority)[0]
    assert not handoff.request(c("third", reads=["A/x"]))
    assert handoff.request(c("free", reads=["A/unrelated"]))
    assert handoff.verdict(plan) == [("young", 0)]
    handoff.release("young")
    assert not handoff.request(c("young", reads=["A/x"], generation=1))
    assert handoff.request(c("old", writes=["A/x"]))
    assert not handoff.fences
    checks += 1

    # Handoff must not introduce a wait-die cycle: young holds x, old waits for
    # x, and young's upgrade cannot wait behind old while retaining x forever.
    upgrade = PriorityLocks("wait-die", priority=priority)
    assert upgrade.request(c("young", reads=["A/x"]))
    assert not upgrade.request(c("old", writes=["A/x"]))
    assert not upgrade.request(c("young", 1, writes=["A/x"]))
    plan = upgrade.collect(priority)[0]
    assert upgrade.verdict(plan) == [("young", 0)]
    upgrade.release("young")
    assert upgrade.request(c("old", writes=["A/x"]))
    checks += 1
    return {"checks": checks, "passed": True}


def progress_probe():
    """Regress local restart barging ahead of the oldest remote waiter."""
    from comparison import Simulation
    from comparison_inputs import cases

    case = next(case for case in cases(seed=7, width=4)
                if case["name"] == "ordinary_distributed_hot")
    case["horizon"] = 160
    completions = {}
    for mode in ("wound-wait", "wait-die"):
        simulation = Simulation(case, mode)
        result = simulation.run()
        oldest = min((tx for tx in simulation.tx.values()
                      if tx.spec["group"] == "distributed"),
                     key=lambda tx: (tx.spec["arrival"], tx.spec["id"]))
        assert oldest.state == "complete", (mode, oldest.state)
        assert result["serial_check"]
        completions[mode] = oldest.completion
    return {"oldest_completion_ticks": completions, "passed": True}


if __name__ == "__main__":
    import json
    print(json.dumps({"policy": probe(), "progress": progress_probe()}, sort_keys=True))
