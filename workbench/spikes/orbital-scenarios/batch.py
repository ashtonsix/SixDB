"""Component-owned reservations under an atomic multishard retry cut.

Collection/closure and verdict application are idealized; arbitration_us charges
an explicit aggregate collection, solving and return delay. See BATCH.md.
"""

from collections import defaultdict
from itertools import combinations

from model import Model


def independent_set(vertices, adjacency, priority, weights, mandatory, solver):
    """Keep irrevocable C2, then an oldest compatible anchor; bounded optimization.

The anchor is deliberate fairness policy. This is not an unconstrained maximum
independent set, nor a minimum-abort solver. Exact search never exceeds 20,000
nodes and is attempted only on <=18 optional vertices.
"""
    selected = set(mandatory)
    optional = [v for v in sorted(vertices, key=priority) if v not in selected
                and not (adjacency[v] & selected)]
    if optional:
        selected.add(optional.pop(0))
    optional = [v for v in optional if not (adjacency[v] & selected)]
    ordered = sorted(optional, key=(lambda v: (-weights[v], priority(v))) if solver == "work" else priority)
    greedy = set(selected)
    for v in ordered:
        if not (adjacency[v] & greedy):
            greedy.add(v)
    visits = 0
    best = greedy
    if solver == "bounded" and len(optional) <= 18:
        def search(candidates, chosen):
            nonlocal visits, best
            visits += 1
            if visits > 20000 or len(chosen) + len(candidates) <= len(best):
                return
            if not candidates:
                best = set(chosen)
                return
            v, rest = candidates[0], candidates[1:]
            search([w for w in rest if w not in adjacency[v]], chosen | {v})
            search(rest, chosen)
        search(optional, selected)
    return best, min(visits, 20001)


class BatchModel(Model):
    def __init__(self, scenario):
        super().__init__(scenario)
        self.round = 0
        self.waiting_rounds = set()
        self.fences = {}
        self.schedule(self.scenario["policy"]["batch_us"], "batch_tick", None)

    def scope(self, ref):
        p = self.part(ref).spec
        return {(p["shard"], key) for key in p["locks"]}

    def fence_blockers(self, ref):
        blocked = set()
        for fence in self.fences.values():
            if self.scope(ref) & fence["scope"] and ref[0] not in fence["winners"]:
                # A real representative is for trace compatibility only. These
                # are arbitration fences, not edges in the granted-lock wait graph.
                blocked.add(fence["representative"])
        return blocked

    def blockers(self, ref, epoch_grants=()):
        return super().blockers(ref, epoch_grants) | self.fence_blockers(ref)

    def wait_blockers(self, ref):
        return super().blockers(ref)

    def retry(self, ref, blockers):
        p = self.part(ref)
        p.failures += 1
        p.retry_epoch = self.epochs[p.spec["shard"]] + 1
        self.note("retry", transaction=ref[0], part=ref[1],
                  blockers=[list(b) for b in sorted(blockers)], next_epoch=p.retry_epoch)

    def eligible(self, ref):
        p = self.part(ref)
        if p.retry_epoch > self.epochs[p.spec["shard"]]:
            return False
        if p.attempts == 0:
            return True
        if self.fence_blockers(ref):
            return False
        if any(ref[0] in f["winners"] for f in self.fences.values()):
            return True
        return self.scenario["policy"]["fast_retries"] and not super().blockers(ref)

    def handle_event(self, kind, payload):
        if kind == "batch_tick":
            self.schedule(self.time + self.scenario["policy"]["batch_us"], "batch_tick", None)
            if self.waiting_rounds and self.scenario["policy"]["yield"] == "batch-hold":
                self.counts["batch_tick_deferred"] += 1
            else:
                self.collect()
            return True
        if kind == "batch_verdict":
            self.verdict(payload)
            return True
        return False

    def ready(self):
        return sorted((tid, pid) for tid, tx in self.transactions.items() if tx.state == "preparing"
                      for pid, p in tx.parts.items() if p.state == "ready")

    def release_finished(self):
        for key, fence in list(self.fences.items()):
            owners = fence["members"] if fence["pending"] else fence["winners"]
            if all(self.transactions[tid].state == "complete" for tid in owners):
                del self.fences[key]
                self.note("component_release", round=fence["round"], members=fence["members"])

    def complete(self, tid):
        super().complete(tid)
        self.release_finished()

    def collect(self):
        candidates = [ref for ref in self.ready() if self.part(ref).attempts > 0]
        if not candidates:
            return
        self.round += 1
        self.fences.clear()
        self.note("batch_begin", round=self.round, retry_parts=len(candidates))
        # Synchronous retry input is a strong abstraction, not extra free ordinary
        # epoch capacity: count every attempt and expose the uncharged batch work.
        epoch_grants = set()
        retry_set = set(candidates)
        isolated = {ref for ref in candidates if not any(
            self.conflict(ref, other) for other in retry_set | self.grants if other != ref)}
        for ref in candidates:
            if ref in isolated:
                self.acquire(ref, epoch_grants)
                self.counts["batch_isolated_success"] += 1
            else:
                self.part(ref).attempts += 1
                blocked = {b for b in retry_set | self.grants if self.conflict(ref, b)}
                self.retry(ref, blocked)
        failed = {ref[0] for ref in candidates if self.part(ref).state == "ready"}
        if not failed:
            return
        # Globally merge all current known claims by transaction identity and key.
        # This models complete entrainment; it does NOT implement its wire protocol.
        claims = self.grants | set(self.ready())
        by_key = defaultdict(set)
        by_tx = defaultdict(set)
        for ref in sorted(claims):
            by_tx[ref[0]].add(ref)
            for key in self.scope(ref):
                by_key[key].add(ref[0])
        scope_graph = {tid: set() for tid in by_tx}
        adjacency = {tid: set() for tid in by_tx}
        for txids in by_key.values():
            for a, b in combinations(sorted(txids), 2):
                scope_graph[a].add(b)
                scope_graph[b].add(a)
        # Compute incompatibility only for pairs sharing at least one key.
        for a, neighbors in scope_graph.items():
            for b in sorted(neighbors):
                if a < b and any(self.conflict(x, y) for x in by_tx[a] for y in by_tx[b]):
                    adjacency[a].add(b)
                    adjacency[b].add(a)
        remaining = set(scope_graph)
        components = []
        while remaining:
            stack, component = [min(remaining)], set()
            while stack:
                v = stack.pop()
                if v in component:
                    continue
                component.add(v)
                stack.extend(scope_graph[v] - component)
            remaining -= component
            if component & failed:
                components.append(component)
        plans = []
        for index, vertices in enumerate(components):
            refs = set().union(*(by_tx[v] for v in vertices))
            scope = set().union(*(self.scope(ref) for ref in refs))
            mandatory = {v for v in vertices if self.transactions[v].state == "authorized"}
            winners, search_nodes = independent_set(vertices, adjacency, self.priority,
                {v: self.work(v) for v in vertices}, mandatory, self.scenario["policy"]["solver"])
            key = self.round, index
            fence = {"round": self.round, "members": sorted(vertices), "scope": scope,
                     "representative": min(refs), "winners": set(), "pending": True}
            self.fences[key] = fence
            selected_refs = set().union(*(by_tx[v] for v in winners)) if winners else set()
            victims = defaultdict(list)
            for ref in sorted(refs & self.grants):
                if ref[0] not in winners and any(self.conflict(ref, selected) for selected in selected_refs):
                    victims[ref[0]].append(ref[1])
            shard_set = {shard for shard, _ in scope}
            local = self.scenario["policy"]["local_solver"] and len(shard_set) == 1 and all(
                self.transactions[v].spec["coordinator"] in shard_set for v in vertices)
            plan = {"key": key, "members": sorted(vertices), "winners": sorted(winners),
                          "victims": dict(victims),
                          "generations": {ref: self.part(ref).generation for ref in refs},
                          "known": {v: set(self.transactions[v].known) for v in vertices}}
            if local:
                self.schedule(self.time, "batch_verdict", (self.round, [plan]))
                self.counts["local_component_solves"] += 1
            else:
                plans.append(plan)
            edges = sum(len(adjacency[v] & vertices) for v in vertices) // 2
            shard_count = len({shard for shard, _ in scope})
            self.counts["component_edges"] += edges
            self.counts["component_claims"] += len(refs)
            self.counts["component_reports"] += shard_count
            self.counts["solver_nodes"] += search_nodes
            self.counts["max_component_transactions"] = max(self.counts["max_component_transactions"], len(vertices))
            self.note("component", round=self.round, component=index, members=sorted(vertices),
                      edges=edges, keys=len(scope), shards=shard_count, solver_nodes=search_nodes,
                      proposed_winners=sorted(winners))
        if plans:
            self.waiting_rounds.add(self.round)
            self.schedule(self.time + self.scenario["policy"]["arbitration_us"], "batch_verdict", (self.round, plans))

    def verdict(self, payload):
        round_id, plans = payload
        # Local verdicts can share a round with remote ones still in flight.
        for plan in plans:
            key = plan["key"]
            if key not in self.fences:
                self.note("verdict_stale", round=round_id, reason="reservation generation replaced")
                continue
            changed = any(self.part(ref).generation != generation for ref, generation in plan["generations"].items())
            changed |= any(self.transactions[tid].known != known for tid, known in plan["known"].items())
            winners = set(plan["winners"])
            losers = set(plan["members"]) - winners
            changed |= any(self.transactions[tid].state == "authorized" for tid in plan["victims"])
            if changed:
                del self.fences[key]
                self.note("verdict_stale", round=round_id, reason="preparation or C2 authority changed")
                continue
            invalidated = []
            for tid, roots in sorted(plan["victims"].items()):
                if self.transactions[tid].state != "preparing":
                    continue
                self.invalidate(tid, roots, "component verdict")
                invalidated.append(tid)
            self.fences[key]["winners"] = winners
            self.fences[key]["pending"] = False
            self.note("verdict_apply", round=round_id, members=plan["members"], winners=sorted(winners),
                      invalidated=invalidated)
        if not any(f["round"] == round_id and f["pending"] for f in self.fences.values()):
            self.waiting_rounds.discard(round_id)
        self.release_finished()

    def check(self):
        super().check()
        occupied = set()
        for fence in getattr(self, "fences", {}).values():
            assert not (fence["scope"] & occupied), "component reservations overlap"
            assert fence["winners"] <= set(fence["members"])
            occupied |= fence["scope"]

    def snapshot(self):
        result = super().snapshot()
        result["components"] = [{"round": f["round"], "members": f["members"],
            "winners": sorted(f["winners"]), "pending": f["pending"],
            "keys": [list(key) for key in sorted(f["scope"])]} for f in self.fences.values()]
        return result
