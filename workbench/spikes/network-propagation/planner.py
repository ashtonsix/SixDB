"""Price/latency frontier search, with held-out simulator evaluation.

The cheap score proposes trees; it never supplies reported delivery percentiles.
Final selection replays an independent planning workload through Simulator.
"""
from collections import defaultdict
from functools import lru_cache
import json
import math

from model import Topology, Simulator, chunks, plan, validate_plan, wire


def identity(route):
    return tuple(sorted(route.items()))


def depths(topo, spec, route):
    ds = {spec["source"]: 0}
    def depth(n):
        if n not in ds:
            ds[n] = 1 + depth(topo.edges[route[n]]["src"])
        return ds[n]
    for n in route:
        depth(n)
    return max(ds.values()), sum(ds[t] for t in spec["targets"])


def alternatives(topo, spec, route):
    included = {spec["source"], *route}
    for dst in sorted(route):
        for e in sorted(topo.edges.values(), key=lambda e: e["id"]):
            if (e["dst"] != dst or e["src"] not in included or
                    e["id"] == route[dst] or not topo.legal(e, spec)):
                continue
            candidate = dict(route, **{dst: e["id"]})
            try:
                validate_plan(topo, spec["source"], spec["targets"], candidate)
            except ValueError:
                continue
            yield candidate


def shallow(topo, spec, initial=None):
    """Remove avoidable depth at no extra payload price. Diagnostic seed only."""
    route = initial or plan(topo, spec, "cost")
    price = lambda p: sum(topo.cost(topo.edges[e], spec["size_bytes"],
                                   spec["chunk_bytes"], spec["mtu"]) for e in p.values())
    while True:
        before = depths(topo, spec, route)
        candidates = [p for p in alternatives(topo, spec, route)
                      if price(p) <= price(route) + 1e-15 and depths(topo, spec, p) < before]
        if not candidates:
            return route
        route = min(candidates, key=lambda p: (depths(topo, spec, p), price(p), identity(p)))


def estimate(topo, spec, route):
    """Resource service / pipeline / queue proxy for search, not a tail estimate."""
    capacities = {r: c * spec.get("capacity_scale", 1) *
                  (1-spec.get("background", {}).get(r, 0)) for r, c in topo.capacity.items()}
    parts = [wire(s, spec["mtu"]) for s in chunks(spec["size_bytes"], spec["chunk_bytes"])]
    data_bytes, data_packets = map(sum, zip(*parts))
    usage, cpu, price = defaultdict(float), defaultdict(float), 0.0
    def account(e, b, packets, jobs, request=False):
        nonlocal price
        for r in e["resources"]:
            usage[r] += b
        for n in (e["src"], e["dst"]):
            node = topo.nodes[n]
            cpu[n] += (jobs*node.get("chunk_cpu_us", 1) + b/(node.get("crypto_gbps", 32)*125)
                       + packets*node.get("packet_cpu_us", .08))
        price += b/1e9*sum(e.get("charges", {}).values())
        if request:
            price += e.get("request_usd", 0)
    for eid in route.values():
        account(topo.edges[eid], data_bytes, data_packets, len(parts), True)
    control = (route.values() if spec.get("control_mode") == "tree" else
               [topo.direct(spec["source"], t, spec) for t in spec["targets"]]
               if spec.get("control_mode", "direct") == "direct" else [])
    for eid in control:
        account(topo.edges[eid], *wire(256, spec["mtu"]), 1)
    for t in spec["targets"]:
        account(topo.edges[topo.direct(t, spec["source"], spec)], *wire(128, spec["mtu"]), 1)
    service = max([usage[r]/capacities[r] for r in usage] + list(cpu.values()))
    first = {spec["source"]: 0.0}
    def arrival(n):
        if n not in first:
            e = topo.edges[route[n]]
            # Charge a first-chunk share of all work at a congested pool/node.
            step = max([usage[r]/capacities[r]/len(parts) for r in e["resources"]] +
                       [cpu[e["src"]]/len(parts), cpu[n]/len(parts)])
            first[n] = arrival(e["src"]) + e["delay_us"] + e.get("setup_us", 0) + step
        return first[n]
    latency = max(arrival(t) for t in spec["targets"]) + service*(len(parts)-1)/len(parts)
    rho = service*spec["rate_per_s"]/1e6
    # M/D/1-inspired proxy below saturation; finite-cohort backlog above it.
    # Correlated multi-resource queues are evaluated by simulation after search.
    queue = (service*rho/(2*(1-rho)) if rho < .98 else
             service*25 + max(0, service-1e6/spec["rate_per_s"])*(spec["messages"]-1))
    return price, latency+queue, *depths(topo, spec, route)


def percentile(values, q):
    xs = sorted(math.inf if v is None else v for v in values)
    p = (len(xs)-1)*q
    lo, hi = math.floor(p), math.ceil(p)
    if not math.isfinite(xs[hi]):
        return math.inf
    return xs[lo] if lo == hi else xs[lo] + (p-lo)*(xs[hi]-xs[lo])


def planning_spec(spec):
    result = dict(spec, messages=max(spec.get("planner_messages", 12),32 if spec.get("burst") else 1),
                  packet_loss=0, jitter_scale=0)
    # Repair timeout is known policy, including its healthy-traffic duplicates.
    # Future fault events and random loss/jitter are withheld from selection.
    result.pop("failure", None)
    result.pop("capacity_events", None)
    return result


@lru_cache(maxsize=128)
def search(topology_json, spec_json):
    topo, spec = Topology(json.loads(topology_json)), json.loads(spec_json)
    seeds = [plan(topo, spec, p) for p in ("direct", "shortest", "cost", "bounded")]
    seeds.append(shallow(topo, spec, seeds[2]))
    scores, routes = {}, {}
    def add(p):
        k = identity(p)
        if k not in scores:
            routes[k], scores[k] = p, estimate(topo, spec, p)
        return k
    beam = {add(p) for p in seeds}
    anchors = set(beam)
    # Epsilon-constraint beam: cheapest route within each latency allowance.
    # Keep the fast endpoint and different price/latency tradeoffs, not only a
    # weighted scalar optimum. Each mutation changes an actual incoming edge.
    def choose(keys, allowances):
        best = min(scores[k][1] for k in keys)
        selected = {min(keys, key=lambda k: (scores[k][1], scores[k][0], scores[k][2:], k))}
        for factor in allowances:
            eligible = [k for k in keys if scores[k][1] <= best*factor + 1e-8]
            selected.add(min(eligible, key=lambda k: (scores[k][0], scores[k][1], scores[k][2:], k)))
        return selected
    for _ in range(spec.get("planner_rounds", 5)):
        previous = set(beam)
        keys = set(beam)
        for k in sorted(beam):
            keys.update(add(p) for p in alternatives(topo, spec, routes[k]))
        beam = choose(keys, [1, 1.05, 1.1, 1.25, 1.5, 2, 4, math.inf])
        if beam == previous:
            break
    shortlist = choose(set(scores), [1, 1.025, 1.05, 1.1, 1.15, 1.25, 1.5, 2, 3, 5, math.inf]) | anchors
    planning = planning_spec(spec)
    # Known capacity/background are usable inputs. Future fault times, losses,
    # evaluation arrivals and jitter are not available to the planner.
    q = spec.get("planner_quantile", .9)
    seed = spec.get("planner_seed", 271828)
    evaluated = []
    for k in sorted(shortlist):
        sim = Simulator(topo, planning, routes[k], seed)
        run = sim.run()
        last = percentile([r["last_us"] for r in run["rows"]], q)
        # Mean recipient completion prevents hiding a slower branch behind an
        # unchanged last-recipient maximum. Per-object node times are available
        # through the simulator object, and are independent of evaluation draws.
        mean = sum(m["delivered"].get(t,math.inf)-m["release_us"] for m in sim.messages.values()
                   for t in planning["targets"])/(planning["messages"]*len(planning["targets"]))
        evaluated.append(dict(route=routes[k], price=sum(run["costs"].values())/planning["messages"],
                              latency_us=last, mean_recipient_us=mean,
                              incomplete=sum(r["last_us"] is None for r in run["rows"]),
                              max_depth=scores[k][2], total_depth=scores[k][3]))
    fastest = min(evaluated, key=lambda x: (x["incomplete"],x["latency_us"], x["price"], x["mean_recipient_us"], x["total_depth"], identity(x["route"])))
    budget = fastest["latency_us"]*(1+spec.get("planner_slack", .1))
    eligible = [x for x in evaluated if x["incomplete"]==fastest["incomplete"] and x["latency_us"] <= budget+1e-8]
    balanced = min(eligible, key=lambda x: (round(x["price"], 15), x["latency_us"], x["mean_recipient_us"], x["total_depth"], identity(x["route"])))
    frontier = [x for x in evaluated if x["incomplete"]==fastest["incomplete"] and not any(
        y["incomplete"]==fastest["incomplete"] and
        y["price"] <= x["price"]+1e-15 and y["latency_us"] <= x["latency_us"]+1e-8 and
        (y["price"] < x["price"]-1e-15 or y["latency_us"] < x["latency_us"]-1e-8) for y in evaluated)]
    return dict(balanced=balanced["route"], latency=fastest["route"], frontier=frontier, candidates=evaluated,
                evaluated=len(evaluated), proposed=len(scores), planning_seed=seed,
                planning_messages=planning["messages"], planning_quantile=q,
                planning_incomplete=fastest["incomplete"],latency_budget_us=budget if math.isfinite(budget) else None,
                status="finite bound found" if math.isfinite(budget) else "no finite latency bound found",
                price_scope="all variable bytes and API fees; fixed provisioned costs separate")


def optimize(topo, spec):
    return search(json.dumps(topo.data, sort_keys=True), json.dumps(spec, sort_keys=True))
