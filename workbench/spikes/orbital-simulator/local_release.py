"""Local release on durable fixed position, now adopted in BRIEF.

All reservations must still be held before minimum announcement; the coordinator
still durably stores one immutable position before distributing it, and waits
for every fix acknowledgement before computation. Only the explicit subsequent
release round is removed. The strict traffic binding remains a comparison.
This experiment assumes its existing prepared authority and recovery
rules; agreement on a position is not itself permission to publish effects.
"""
from __future__ import annotations

from dataclasses import replace

from traffic import Config, Coordinator, Shard, Strategy, initial_state


class LocalReleaseShard(Shard):
    def transition(self, request):
        result = super().transition(request)
        if request["kind"] == "fix":
            self.tickets[request["tx"]]["released"] = True
        return result


class LocalReleaseCoordinator(Coordinator):
    def advance(self, ctx, state):
        if state["phase"] == "fix" and len(state["fixed"]) == len(state["locks"]):
            # Each acknowledged fix already released that shard's reservation.
            state["released"].update(state["locks"])
        super().advance(ctx, state)


def build(config=Config(), strategy=Strategy(), seed=1, *, replicated=False,
          early=True, replay=None, replica_delays=(0, 3_000, 9_000),
          offers=True, assembly=None, factories=None):
    """Select candidate implementations before boot; explicit factories win.

    Assembly changes physical placement only on the replicated binding. The
    common factories argument supports authored roles in either binding, while
    offers=False leaves workload injection to the experiment's author.
    """
    if strategy.ordering != "mv":
        raise ValueError("local release candidate applies to the multiversion protocol")
    initial = initial_state(config)
    chosen = {}
    if early:
        chosen.update({f"coordinator{i}": lambda: LocalReleaseCoordinator(strategy, config)
                       for i in range(2)})
    if replicated:
        from replicated_contention import Assembly, Consumer, build as baseline
        assembly = assembly if assembly is not None else Assembly()
        if not isinstance(assembly, Assembly):
            raise TypeError("assembly must be an Assembly")
        if set(assembly.factories) & set(factories or {}):
            raise ValueError("actor factory supplied in both assembly and factories")
        class LocalReleaseConsumer(Consumer):
            def __init__(self, shard, initial, strategy, replica, fold_ns):
                super().__init__(shard, initial, strategy, replica, fold_ns)
                self.history = LocalReleaseShard(shard, initial, strategy, 0)
        if early:
            chosen.update({f"consumer{shard}_{replica}":
                (lambda shard=shard, replica=replica: LocalReleaseConsumer(
                    shard, initial, strategy, replica, replica_delays[replica]))
                for shard in range(config.shards) for replica in range(3)})
        chosen.update(assembly.factories)
        chosen.update(factories or {})
        return baseline(config, strategy, seed, replica_delays=replica_delays,
                        replay=replay, offers=offers, assembly=replace(assembly, factories=chosen))
    if assembly is not None:
        raise ValueError("physical Assembly applies to the replicated binding only")
    from traffic import build as baseline
    if early:
        chosen.update({f"shard{index}": (lambda index=index: LocalReleaseShard(
            index, initial, strategy, config.metadata_ns_per_entry)) for index in range(config.shards)})
    chosen.update(factories or {})
    return baseline(config, strategy, seed, replay=replay, offers=offers, factories=chosen)
