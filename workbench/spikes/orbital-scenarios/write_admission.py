"""Write-only admission for transactions with complete, predeclared outputs.

The caller requests disjoint participant groups in one canonical shard order
before taking the execution snapshot. A blocked request registers only that
group; it neither acquires part of the group nor reserves future participants.
Earlier grants remain held until their releases are delivered.

Read scopes are deliberately absent. This layer does not replace multiversion
capture or certification, and it makes no promise that reads can bypass a later
certification write promise. It is not admission for dynamically discovered
outputs, a distributed failure protocol, or a bound on waiting time.
"""

from collections import Counter
from collections.abc import Iterable


class Admission:
    def __init__(self, priority: dict[str, tuple[int, str]]):
        self.priority = dict(priority)
        self.grants: dict[str, str] = {}
        self.waiting: dict[str, frozenset[str]] = {}
        self.counters = Counter()

    def request(self, owner: str, keys: Iterable[str]) -> bool:
        """Grant this entire participant group, or retain it as one waiter.

        A request already fully granted to its owner is an idempotent success,
        including when a later participant group is currently waiting. Other
        requests cannot pass an older registered overlapping waiter. There is
        at most one outstanding group per owner; the caller advances to the
        next participant only after this group succeeds.
        """
        rank = self.priority[owner]
        keys = frozenset(keys)
        self.counters["requests"] += 1
        if not keys:
            self.counters["empty_requests"] += 1
            return True
        if all(self.grants.get(key) == owner for key in keys):
            self.counters["duplicate_grants"] += 1
            return True
        previous = self.waiting.get(owner)
        assert previous is None or previous == keys, (
            "an owner can wait for only one complete participant group")

        holders = {self.grants[key] for key in keys
                   if key in self.grants and self.grants[key] != owner}
        older = {other for other, pending in self.waiting.items()
                 if other != owner and self.priority[other] < rank
                 and keys.intersection(pending)}
        if holders or older:
            if previous is None:
                self.waiting[owner] = keys
                self.counters["queued_groups"] += 1
            self.counters["blocked_requests"] += 1
            self.counters["blocked_by_holder"] += bool(holders)
            self.counters["blocked_by_older_waiter"] += bool(older)
            return False

        self.waiting.pop(owner, None)
        newly_granted = sum(self.grants.get(key) != owner for key in keys)
        for key in sorted(keys):
            self.grants[key] = owner
        self.counters["granted_groups"] += 1
        self.counters["granted_keys"] += newly_granted
        return True

    def release(self, owner: str, keys: Iterable[str]):
        """Deliver release/cancellation for these keys; never release another owner.

        Cancelling any part of a queued group cancels that whole atomic request,
        rather than silently changing its output set. No waiter is auto-granted:
        the caller retries through ``request`` in its agreed execution order.
        """
        keys = frozenset(keys)
        self.counters["releases"] += 1
        pending = self.waiting.get(owner)
        if pending is not None and keys.intersection(pending):
            del self.waiting[owner]
            self.counters["cancelled_waits"] += 1
        for key in sorted(keys):
            if self.grants.get(key) == owner:
                del self.grants[key]
                self.counters["released_keys"] += 1

    def release_all(self, owner: str):
        """Deliver release of every grant and queued group for this owner."""
        keys = {key for key, holder in self.grants.items() if holder == owner}
        keys.update(self.waiting.get(owner, ()))
        self.release(owner, keys)


def probes():
    """Small admission histories; these are not a distributed progress proof."""
    passed = []
    priority = {"old": (0, "old"), "middle": (1, "middle"),
                "young": (2, "young"), "other": (3, "other")}

    admission = Admission(priority)
    assert admission.request("young", ["eu/x"])
    assert not admission.request("old", ["eu/x", "eu/y"])
    assert admission.grants == {"eu/x": "young"}
    assert admission.waiting["old"] == frozenset({"eu/x", "eu/y"})
    assert admission.request("other", ["eu/z"])
    passed.append("atomic participant group and disjoint progress")

    # A repeated acknowledgement of an existing grant must not make its holder
    # wait behind a transaction waiting for that very grant to be released.
    assert admission.request("young", ["eu/x"])
    assert admission.waiting["old"] == frozenset({"eu/x", "eu/y"})
    passed.append("idempotent held-group request")

    # y is physically free, but belongs to the older registered group's actual
    # output set. The younger writer cannot bypass that queued whole group.
    assert not admission.request("middle", ["eu/y"])
    admission.release_all("young")
    assert not admission.request("middle", ["eu/y"])
    assert admission.request("old", ["eu/x", "eu/y"])
    admission.release_all("old")
    assert admission.request("middle", ["eu/y"])
    passed.append("older waiter handoff without barging")

    admission.release("other", ["eu/y"])
    assert admission.grants["eu/y"] == "middle"
    admission.release_all("middle")
    admission.release_all("middle")
    admission.release_all("other")
    assert not admission.grants and not admission.waiting
    passed.append("owner-safe and idempotent release")

    admission = Admission(priority)
    assert admission.request("young", ["eu/x"])
    assert not admission.request("old", ["eu/x", "eu/y"])
    admission.release("old", ["eu/y"])
    assert "old" not in admission.waiting
    assert admission.request("middle", ["eu/y"])
    passed.append("cancellation does not shrink a queued atomic group")

    # Three transactions acquire EU before US. Only requested participants
    # appear in waiting; an older EU waiter does not reserve its future US key.
    admission = Admission(priority)
    assert admission.request("young", ["eu/x"])
    assert not admission.request("old", ["eu/x"])
    assert admission.request("middle", ["eu/y"])
    assert admission.request("young", ["us/z"])
    assert not admission.request("middle", ["us/z"])
    assert admission.grants["eu/y"] == "middle"
    assert admission.request("middle", ["eu/y"])
    assert admission.waiting["middle"] == frozenset({"us/z"})
    admission.release_all("young")
    assert admission.request("middle", ["us/z"])
    assert admission.request("old", ["eu/x"])
    assert not admission.request("old", ["us/z"])
    admission.release_all("middle")
    assert admission.request("old", ["us/z"])
    admission.release_all("old")
    assert not admission.grants and not admission.waiting
    passed.append("canonical participant order progresses in the authored history")

    # There are intentionally no read claims or compatibility modes to probe.
    # A read-only transaction supplies no output group and leaves no state.
    admission = Admission(priority)
    assert admission.request("young", ["eu/x"])
    before = (dict(admission.grants), dict(admission.waiting))
    assert admission.request("old", [])
    assert before == (admission.grants, admission.waiting)
    passed.append("empty output set creates no read protection")
    return passed


if __name__ == "__main__":
    for name in probes():
        print("PASS:", name)
