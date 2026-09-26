"""Retained protection and independent component arbitration for a shared harness.

This reuses the old model's anchored selection, not its epoch timing. The caller
owns computation, retries, messages, costs and recovery. Collection sees a whole
claim snapshot and verdict application is atomic; neither is a wire protocol.
"""

from collections import Counter, defaultdict
from dataclasses import dataclass

from batch import independent_set


@dataclass(frozen=True)
class Claim:
    owner: str
    part: int
    reads: frozenset[str]
    writes: frozenset[str]
    generation: int = 0

    def __post_init__(self):
        object.__setattr__(self, "reads", frozenset(self.reads))
        object.__setattr__(self, "writes", frozenset(self.writes))
        if not self.owner or self.part < 0 or self.generation < 0:
            raise ValueError("claims require an owner and nonnegative part/generation")
        if not all(isinstance(key, str) and key for key in self.reads | self.writes):
            raise ValueError("claim keys must be nonempty strings")

    @property
    def ref(self):
        return self.owner, self.part


def conflict(left, right):
    """Exact-key incompatibility; callers must expand collection coverage."""
    return left.owner != right.owner and bool(
        left.writes & (right.reads | right.writes)
        or right.writes & left.reads
    )


def _scope(claims):
    reads, writes = set(), set()
    for claim in claims:
        reads.update(claim.reads)
        writes.update(claim.writes)
    return frozenset(reads), frozenset(writes)


def _scope_conflict(left, right):
    lr, lw = left
    rr, rw = right
    return bool(lw & (rr | rw) or rw & lr)


def _shard(key):
    # Both encodings appear naturally in authored fixtures. A caller with a
    # different key format can supply shard_of explicitly.
    return key.split("/", 1)[0].split(":", 1)[0]


class Arb:
    def __init__(self, solver="age", shard_of=None):
        if solver not in ("age", "work", "bounded"):
            raise ValueError("solver must be age, work or bounded")
        self.solver = solver
        self.shard_of = shard_of or _shard
        self.grants = {}
        self.waiting = {}
        self.fences = {}
        self.counts = Counter()
        self._next_plan = 0

    def _release_finished(self):
        for number, fence in list(self.fences.items()):
            if not fence["pending"] and all(
                self.waiting.get(ref) != claim
                for ref, claim in fence["selected_ready"].items()
            ):
                del self.fences[number]
                self.counts["released"] += 1

    def request(self, claim):
        """Acquire the complete part or retain only its waiting request.

        An identical granted request is idempotent. A caller changing a granted
        part must release it first; this avoids silently dropping protection.
        """
        self._release_finished()
        self.counts["requests"] += 1
        old = self.grants.get(claim.ref)
        if old is not None:
            if old != claim:
                raise ValueError("release a granted part before replacing its claim")
            return True
        self.waiting[claim.ref] = claim
        blocked = any(conflict(claim, held) for held in self.grants.values())
        blocked |= any(
            claim.owner not in fence["winners"]
            and _scope_conflict(_scope([claim]), fence["scope"])
            for fence in self.fences.values()
        )
        if blocked:
            self.counts["blocked"] += 1
            return False
        self.grants[claim.ref] = claim
        del self.waiting[claim.ref]
        self.counts["granted"] += 1
        self._release_finished()
        return True

    def release(self, owner, from_part=0):
        """Remove a linear chain suffix; the caller resets its computation."""
        for table in (self.grants, self.waiting):
            for ref in list(table):
                if ref[0] == owner and ref[1] >= from_part:
                    del table[ref]
        self._release_finished()

    def _snapshot(self, owners):
        return {
            "grants": {ref: claim for ref, claim in self.grants.items() if ref[0] in owners},
            "waiting": {ref: claim for ref, claim in self.waiting.items() if ref[0] in owners},
        }

    def collect(self, priority, irrevocable=None, weights=None):
        """Reserve new components and return plans for caller-delayed verdicts.

        A plan's ``local`` flag means its claims use one shard. The harness must
        also check coordinator authority before assigning a zero-delay verdict.
        New bridges defer behind existing components; they never replace them.
        """
        self._release_finished()
        irrevocable = set(irrevocable or ())
        claims = {**self.grants, **self.waiting}
        by_owner = defaultdict(list)
        readers, writers = defaultdict(set), defaultdict(set)
        for ref, claim in sorted(claims.items()):
            by_owner[claim.owner].append(claim)
            for key in claim.reads:
                readers[key].add(claim.owner)
            for key in claim.writes:
                writers[key].add(claim.owner)
        graph = {owner: set() for owner in by_owner}
        # Read/read overlap creates no edge. Generate only pairs involving a
        # writer, rather than repeatedly enumerating every broad-reader pair.
        for key, owners in writers.items():
            for left in owners:
                others = (owners | readers[key]) - {left}
                graph[left].update(others)
                for right in others:
                    graph[right].add(left)
        remaining = set(graph)
        components = []
        while remaining:
            stack, component = [min(remaining)], set()
            while stack:
                owner = stack.pop()
                if owner not in component:
                    component.add(owner)
                    stack.extend(graph[owner] - component)
            remaining -= component
            if any(ref[0] in component for ref in self.waiting):
                components.append(component)
        plans = []
        for members in components:
            component_claims = [claim for owner in sorted(members) for claim in by_owner[owner]]
            scope = _scope(component_claims)
            if any(members & fence["members"] or _scope_conflict(scope, fence["scope"])
                   for fence in self.fences.values()):
                self.counts["deferred"] += 1
                continue
            mandatory = members & irrevocable
            if any(graph[owner] & mandatory for owner in mandatory):
                raise ValueError("irrevocable owners have incompatible claims")
            order = lambda owner: (priority[owner], owner)
            selected, visits = independent_set(
                members, graph, order,
                {owner: (weights or {}).get(owner, 0) for owner in members},
                mandatory, self.solver,
            )
            selected_claims = [claim for owner in sorted(selected) for claim in by_owner[owner]]
            victims = sorted(
                ref for ref, claim in self.grants.items()
                if ref[0] in members - selected
                and any(conflict(claim, winner) for winner in selected_claims)
            )
            selected_ready = {ref: claim for ref, claim in self.waiting.items()
                              if ref[0] in selected}
            keys = frozenset(scope[0] | scope[1])
            shards = frozenset(self.shard_of(key) for key in keys)
            edges = sum(len(graph[owner] & members) for owner in members) // 2
            self._next_plan += 1
            plan = {
                "id": self._next_plan,
                "members": frozenset(members),
                "selected": frozenset(selected),
                "winners": frozenset(selected),
                "victims": victims,
                "snapshot": self._snapshot(members),
                "selected_ready": selected_ready,
                "keys": keys,
                "shards": shards,
                "edges": edges,
                "local": len(shards) == 1,
            }
            self.fences[plan["id"]] = {
                "members": frozenset(members), "scope": scope,
                "winners": frozenset(), "pending": True,
                "selected_ready": selected_ready,
            }
            self.counts["collected"] += 1
            self.counts["edges"] += edges
            self.counts["claims"] += len(component_claims)
            self.counts["scope_keys"] += len(keys)
            self.counts["reports"] += len(shards)
            self.counts["solver_nodes"] += visits
            self.counts["max_component"] = max(self.counts["max_component"], len(members))
            plans.append(plan)
        return plans

    def verdict(self, plan, irrevocable=None):
        """Activate winners and return each victim owner's earliest lost part.

        The caller immediately releases these suffixes and invalidates their
        computation. Missing/changed claims, discovery or new irrevocability
        reject the entire plan. A late bridge does not invalidate older plans.
        """
        fence = self.fences.get(plan["id"])
        stale = fence is None or not fence["pending"]
        stale |= self._snapshot(plan["members"]) != plan["snapshot"]
        stale |= bool({owner for owner, _ in plan["victims"]} & set(irrevocable or ()))
        if stale:
            # A duplicate verdict must not remove an already activated fence.
            if fence is not None and fence["pending"]:
                del self.fences[plan["id"]]
            self.counts["obsolete"] += 1
            return None
        fence["winners"] = plan["selected"]
        fence["pending"] = False
        earliest = {}
        for owner, part in plan["victims"]:
            earliest[owner] = min(part, earliest.get(owner, part))
        self.counts["verdicts"] += 1
        self._release_finished()
        return sorted(earliest.items())


def probe():
    """Small state checks, not a distributed safety or liveness proof."""
    def c(owner, part, reads=(), writes=(), generation=0):
        return Claim(owner, part, frozenset(reads), frozenset(writes), generation)

    readers = Arb()
    assert readers.request(c("r1", 0, reads=["A/shared"]))
    assert readers.request(c("r2", 0, reads=["A/shared"]))
    assert not readers.request(c("w", 0, writes=["A/shared"]))

    arb = Arb()
    for prefix in ("left", "right"):
        assert arb.request(c(prefix + "1", 0, reads=["A/shared"], writes=["A/" + prefix]))
        assert arb.request(c(prefix + "2", 0, writes=["B/" + prefix]))
        assert not arb.request(c(prefix + "1", 1, writes=["B/" + prefix]))
        assert not arb.request(c(prefix + "2", 1, writes=["A/" + prefix]))
    priority = {owner: (i,) for i, owner in enumerate(("left1", "left2", "right1", "right2", "bridge", "third1", "third2"))}
    plans = arb.collect(priority)
    assert len(plans) == 2 and all(len(plan["members"]) == 2 for plan in plans)
    assert all(plan["edges"] == 1 for plan in plans) and arb.counts["edges"] == 2
    assert not arb.request(c("bridge", 0, writes=["A/left", "A/right"]))
    assert arb.collect(priority) == [] and len(arb.fences) == 2
    assert arb.request(c("third1", 0, writes=["C/other"]))
    assert not arb.request(c("third2", 0, writes=["C/other"]))
    unrelated = arb.collect(priority)
    assert len(unrelated) == 1 and unrelated[0]["local"]
    plan = next(plan for plan in plans if "left1" in plan["members"])
    victims = arb.verdict(plan)
    assert victims == [("left2", 0)]
    for owner, part in victims:
        arb.release(owner, part)
    assert arb.request(c("left1", 1, writes=["B/left"]))
    assert plan["id"] not in arb.fences
    assert any("right1" in fence["members"] for fence in arb.fences.values())

    atomic = Arb()
    assert atomic.request(c("holder", 0, writes=["A/x"]))
    assert not atomic.request(c("both", 0, writes=["A/x", "A/y"]))
    assert ("both", 0) not in atomic.grants
    assert atomic.request(c("independent", 0, writes=["A/y"]))

    retained = Arb()
    assert retained.request(c("writer", 0, writes=["A/x"]))
    assert not retained.request(c("r1", 0, reads=["A/x"]))
    assert not retained.request(c("r2", 0, reads=["A/x"]))
    plan = retained.collect({"r1": (0,), "r2": (1,), "writer": (2,)})[0]
    assert retained.verdict(plan) == [("writer", 0)]
    retained.release("writer")
    assert retained.request(c("r1", 0, reads=["A/x"]))
    assert plan["id"] in retained.fences
    assert retained.verdict(plan) is None and plan["id"] in retained.fences
    assert retained.request(c("r2", 0, reads=["A/x"]))
    assert plan["id"] not in retained.fences

    stale = Arb()
    assert stale.request(c("holder", 0, writes=["A/x"]))
    assert not stale.request(c("winner", 0, writes=["A/x"]))
    plan = stale.collect({"winner": (0,), "holder": (1,)})[0]
    assert stale.verdict(plan, irrevocable={"holder"}) is None
    plan = stale.collect({"winner": (0,), "holder": (1,)})[0]
    stale.release("winner")
    assert not stale.request(c("winner", 0, writes=["A/x"], generation=1))
    assert stale.verdict(plan) is None
    return {"checks": 7, "passed": True}


if __name__ == "__main__":
    import json
    print(json.dumps(probe(), sort_keys=True))
