"""Small harness for incidents and external inputs relative to observed events.

This belongs to the experiment, never to a protocol actor. Predicates see copied
trace facts; input contents are authored before execution, not derived from a
remote actor's hidden state. A missing trigger means unexercised coverage.
"""
from __future__ import annotations

from dataclasses import dataclass
import inspect

from kernel import clone


@dataclass(frozen=True)
class Fault:
    after: int
    action: str
    args: dict


@dataclass(frozen=True)
class Input:
    after: int
    actor: str
    kind: str
    data: dict
    # Application bindings may put the actual arrival time in their own field.
    time_field: str | None = None


class Scenario:
    def __init__(self, world):
        self.world = world
        self.rules = {}
        self.fired = {}
        self.scheduled_inputs = []
        self.previous = world.kernel.listener
        world.kernel.listener = self.observe

    def when(self, name, kind, predicate, steps):
        if name in self.rules or name in self.fired:
            raise ValueError("scenario milestone name reused")
        prepared = []
        for step in steps:
            if not isinstance(step, (Fault, Input)):
                raise ValueError("expected an external input or fault")
            if type(step.after) is not int or step.after < 0:
                raise ValueError("relative time must be a nonnegative integer")
            if isinstance(step, Fault):
                if step.action not in ("crash", "restart", "power_loss", "power_on", "destroy",
                                       "partition", "slowdown", "pause"):
                    raise ValueError("unknown fault action")
                inspect.signature(getattr(self.world, step.action)).bind(**step.args)
                for field, value in step.args.items():
                    if field == "actor" and value not in self.world.actors:
                        raise ValueError("unknown incident actor")
                    if field in ("host", "source_host", "target_host") and value not in self.world.hosts:
                        raise ValueError("unknown incident host")
                prepared.append(Fault(step.after, step.action, clone(step.args)))
            else:
                if step.actor not in self.world.actors:
                    raise ValueError("unknown input actor")
                prepared.append(Input(step.after, step.actor, step.kind, clone(step.data), step.time_field))
        self.rules[name] = (kind, predicate, prepared)
        return self

    def observe(self, entry):
        if self.previous is not None:
            self.previous(entry)
        for name, (kind, predicate, steps) in list(self.rules.items()):
            if name not in self.rules or kind != entry["kind"] or not predicate(clone(entry)):
                continue
            # Retire before recording: listeners can observe nested records.
            del self.rules[name]
            self.fired[name] = dict(event=entry["id"], time=entry["time"])
            kernel = self.world.kernel
            previous_cause = kernel.cause
            try:
                kernel.cause = entry["id"]
                kernel.cause = kernel.record("scenario_trigger", name=name,
                                             observed=entry["id"])
                for step in steps:
                    at = entry["time"] + step.after
                    if isinstance(step, Fault):
                        self.world.fault(at, step.action, **step.args)
                    else:
                        data = clone(step.data)
                        if step.time_field is not None:
                            data[step.time_field] = at
                        receipt = dict(milestone=name, actor=step.actor, message=step.kind,
                                       at=at, data=data)
                        self.scheduled_inputs.append(clone(receipt))
                        kernel.record("scenario_input_scheduled", **receipt)
                        self.world.inject(step.actor, step.kind, data, at=at)
            finally:
                kernel.cause = previous_cause

    def coverage(self):
        return dict(fired=clone(self.fired), missing=list(self.rules))

    def require_all(self):
        if self.rules:
            raise ValueError("scenario milestones not exercised: " + ", ".join(self.rules))
