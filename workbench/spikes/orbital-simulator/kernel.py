"""Discrete events, explicit choices and causal records; no Orbital policy."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import heapq
import json
from typing import Callable


def clone(value):
    """A modeled port cannot pass references into another actor's state."""
    return json.loads(json.dumps(value, sort_keys=True, allow_nan=False))


def digest(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


class ReplayMismatch(ValueError):
    pass


@dataclass(frozen=True)
class Event:
    time: int
    identity: int
    kind: str
    data: dict
    cause: int

    def signature(self):
        return digest([self.time, self.identity, self.kind, self.data])


class Kernel:
    def __init__(self, seed=1, ordering="fifo", replay=None):
        if ordering not in ("fifo", "shuffle"):
            raise ValueError("ordering must be fifo or shuffle")
        self.now = 0
        self.seed = seed
        self.ordering = ordering
        self.events: list[tuple[int, int, Event]] = []
        self.trace: list[dict] = []
        self.decisions: list[dict] = []
        self.replay = replay
        self.cause = 0
        self.serial = 0
        self.listener: Callable | None = None
        self.dispatch: Callable | None = None

    def record(self, kind, **fields):
        entry = dict(id=len(self.trace) + 1, time=self.now, cause=self.cause,
                     kind=kind, **clone(fields))
        self.trace.append(entry)
        if self.listener:
            self.listener(entry)
        return entry["id"]

    def schedule(self, delay, kind, data):
        if not isinstance(delay, int) or delay < 0:
            raise ValueError("delay must be a nonnegative integer number of ns")
        self.serial += 1
        identity = self.serial
        cause = self.record("schedule", event=identity, event_kind=kind,
                            due=self.now + delay)
        event = Event(self.now + delay, identity, kind, clone(data), cause)
        heapq.heappush(self.events, (event.time, event.identity, event))
        return identity

    def sample(self, namespace, identity):
        """Independent keyed draws; an extra retry does not shift other flows."""
        raw = digest([self.seed, namespace, identity])
        return int(raw[:13], 16) / 16 ** 13

    def run(self, until=None, max_events=1_000_000):
        executed = 0
        while self.events and (until is None or self.events[0][0] <= until):
            if executed >= max_events:
                raise RuntimeError("event budget exhausted; preserve trace and unfinished waits")
            time = self.events[0][0]
            ready = []
            while self.events and self.events[0][0] == time:
                ready.append(heapq.heappop(self.events)[2])
            ready.sort(key=lambda e: e.identity)
            choice = dict(time=time, ready=[e.signature() for e in ready])
            if self.replay is not None:
                index = len(self.decisions)
                if index >= len(self.replay):
                    raise ReplayMismatch(f"new choice at {index}")
                old = self.replay[index]
                if old.get("time") != time or old.get("ready") != choice["ready"]:
                    raise ReplayMismatch(f"enabled events changed at choice {index}")
                selected = old["selected"]
                if not isinstance(selected, int) or not 0 <= selected < len(ready):
                    raise ReplayMismatch(f"invalid selection at choice {index}")
            elif self.ordering == "shuffle":
                selected = min(range(len(ready)),
                               key=lambda i: self.sample("schedule", ready[i].identity))
            else:
                selected = 0
            choice["selected"] = selected
            self.decisions.append(choice)
            event = ready.pop(selected)
            for other in ready:
                heapq.heappush(self.events, (other.time, other.identity, other))
            self.now = time
            self.cause = event.cause
            self.cause = self.record("dispatch", event=event.identity,
                                     event_kind=event.kind)
            self.dispatch(event.kind, event.data)
            executed += 1
        if until is not None:
            if until < self.now:
                raise ValueError("cannot run backwards")
            self.now = until
        self.cause = 0
        if not self.events and self.replay is not None:
            self.check_replay()
        return executed

    def check_replay(self):
        if self.replay is not None and len(self.decisions) != len(self.replay):
            raise ReplayMismatch("recorded choices remain unconsumed")

    def slice(self, roots):
        needed = set()
        todo = list(roots)
        while todo:
            identity = todo.pop()
            if not identity or identity in needed:
                continue
            needed.add(identity)
            entry = self.trace[identity - 1]
            todo.append(entry["cause"])
            todo.extend(entry.get("parents", []))
        return [e for e in self.trace if e["id"] in needed]
