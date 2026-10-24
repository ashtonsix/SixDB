#!/usr/bin/env python3
"""Bitset advisor prototype and its evaluation against measured plans.

The advisor is the Ikea side of the seam. It reads cheap per-group facts
(population, live blocks, Bec256 size estimates), a cost table measured on
synthetic calibration classes, and a demand summary from Engine (operation
counts, hot-group count, a resident-memory budget). It returns a decision
package: a recommendation, a few alternatives that differ in a way that
matters, the reuse at which retained preparation pays, and whether its own
evidence separates the leading candidates.

The evaluation replays every trace against the measured prices of every
fixed plan on the actual collection, and reports regret against the best
fixed plan that fits the budget. Usage: advise.py prices.json [--detail]
"""
import json
import math
import statistics
import sys
from dataclasses import dataclass, field

PLANS = ["plain", "plain+counts", "positions", "bec256", "bec4096", "bec4096+retained"]
OPS = ["probe", "rank1", "rank8", "whole"]


# ---- What Ikea knows: per-plan bytes from facts, per-op costs from a table.

def predicted_bytes(plan, facts, reader_bytes):
    """Stored and retained bytes predicted from cheap facts, per collection."""
    pops, live, est = facts["population"], facts["live"], facts["estimate"]
    groups = len(pops)
    if plan == "plain":
        return 512 * groups, 0
    if plan == "plain+counts":
        return 512 * groups, 32 * groups
    if plan == "positions":
        return 2 * sum(pops) + 4 * (groups + 1), 0
    if plan == "bec256":
        return sum(est) + 68 * groups, 0
    # Bec4096: the blocks' streams, a raw mask and roughly one byte of head
    # and length table per live block; raw blocks cap at 32 bytes.
    body = sum(0 if p in (0, 4096) else 2 + e + l for p, e, l in zip(pops, est, live))
    retained = reader_bytes * groups if plan == "bec4096+retained" else 0
    return body + 8 * groups, retained


class CostTable:
    """Per-group operation costs on calibration classes, looked up by the
    group's population and live-block count (log-population interpolation,
    linear between the 2- and 16-live-block classes)."""

    def __init__(self, calibration):
        self.rows = {}
        for c in calibration:
            prices = {p["plan"]: p for p in c["plans"]}
            key = c["name"]
            self.rows[key] = (c["population"], c["live"], prices)
        self.lines = {}
        for live in (2, 16):
            pts = sorted((pop, prices) for name, (pop, _, prices) in self.rows.items()
                         if name.startswith(f"live{live}/"))
            self.lines[live] = pts

    def _line(self, live, pop, plan, op):
        pts = self.lines[live]
        x = math.log2(max(pop, 1))
        xs = [math.log2(p) for p, _ in pts]
        ys = [pr[plan][op] for _, pr in pts]
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        for i in range(1, len(xs)):
            if x <= xs[i]:
                t = (x - xs[i - 1]) / (xs[i] - xs[i - 1])
                return ys[i - 1] + t * (ys[i] - ys[i - 1])
        return ys[-1]

    def group(self, plan, op, pop, live):
        if live == 0:
            return self.rows["full" if pop == 4096 else "empty"][2][plan][op]
        t = min(max((live - 2) / 14, 0.0), 1.0)
        return (1 - t) * self._line(2, pop, plan, op) + t * self._line(16, pop, plan, op)


def predicted_costs(plan, facts, table):
    """Per-operation nanoseconds over the collection: probes and hot ranks
    land on uniformly chosen groups (Engine does not name the hot ones);
    whole passes visit every group; construction and preparation are totals."""
    pops, live = facts["population"], facts["live"]
    n = len(pops)
    g = [(p, l) for p, l in zip(pops, live)]
    mean = lambda op: sum(table.group(plan, op, p, l) for p, l in g) / n
    total = lambda op: sum(table.group(plan, op, p, l) for p, l in g)
    hot = facts.get("hot", [])
    named = lambda op, k: (sum(table.group(plan, op, pops[i], live[i]) for i in hot[:k])
                           / max(1, len(hot[:k])))
    return {"probe": mean("probe"), "rank1": mean("rank1"), "rank8": mean("rank8"),
            "rank1@hot": named("rank1", 1), "rank8@hot": named("rank8", 8),
            "whole": total("whole"), "construct": total("construct"),
            "prepare": total("prepare")}


def actual_costs(prices, groups):
    return {"probe": prices["probe"], "rank1": prices["rank1"], "rank8": prices["rank8"],
            "rank1@hot": prices["rank1"], "rank8@hot": prices["rank8"],
            "whole": prices["whole"] * groups, "construct": prices["construct"] * groups,
            "prepare": prices["prepare"] * groups}


@dataclass
class Demand:
    probes: float
    ranks: float
    hot: int  # 1 or 8
    wholes: float
    named: bool = False  # Engine names the hot groups

    def coarse(self):
        """Order-of-magnitude summary: counts rounded to a power of ten, and
        no knowledge of which or how many groups are hot."""
        r = lambda x: 0 if x == 0 else 10 ** round(math.log10(x))
        return Demand(r(self.probes), r(self.ranks), 8, r(self.wholes))

    def naming(self):
        return Demand(self.probes, self.ranks, self.hot, self.wholes, True)


def trace_cost(c, d):
    rank = c[("rank1" if d.hot == 1 else "rank8") + ("@hot" if d.named else "")]
    return (c["construct"] + c["prepare"] + d.probes * c["probe"] + d.ranks * rank
            + d.wholes * c["whole"])


def probe_cost(c, d, groups, sample=8, ops=1024):
    rank = c["rank1"] if d.hot == 1 else c["rank8"]
    share = sample / groups
    return ((c["construct"] + c["prepare"]) * share + ops * (c["probe"] * (d.probes > 0)
            + rank * (d.ranks > 0)) + c["whole"] * share * (d.wholes > 0))


@dataclass
class Package:
    recommend: str
    alternatives: list = field(default_factory=list)  # (plan, why)
    retain_after: float = None   # accesses per group at which retention pays
    resolved: bool = True


def describe(facts, table, reader_bytes):
    """Everything the advisor derives from data, once per collection."""
    return {p: (sum(predicted_bytes(p, facts, reader_bytes)), predicted_costs(p, facts, table))
            for p in PLANS} | {"groups": len(facts["population"])}


def advise(desc, demand, budget, margin=1.3):
    """The decision package. Hard budgets filter; costs rank; candidates
    within `margin` of the leader are reported as unresolved."""
    rows = {p: (desc[p][0], trace_cost(desc[p][1], demand), desc[p][1]) for p in PLANS}
    legal = [p for p in PLANS if rows[p][0] <= budget] or [min(PLANS, key=lambda p: rows[p][0])]
    ranked = sorted(legal, key=lambda p: rows[p][1])
    best = ranked[0]
    pkg = Package(best)
    smallest = min(legal, key=lambda p: rows[p][0])
    if smallest != best:
        pkg.alternatives.append((smallest, "smallest resident bytes"))
    if len(ranked) > 1:
        if ranked[1] != smallest:
            pkg.alternatives.append((ranked[1], "next cheapest for this demand"))
        pkg.resolved = rows[ranked[1]][1] > margin * rows[best][1]
    # Break-even for Bec4096 preparation, in accesses per group.
    fresh, kept = rows["bec4096"][2], rows["bec4096+retained"][2]
    groups = desc["groups"]
    saving = fresh["probe"] - kept["probe"]
    if saving > 0:
        pkg.retain_after = kept["prepare"] / groups / saving
    return pkg, rows


# ---- Policies compared against the advisor.

def preset_smallest(desc, budget, **_):
    return min(PLANS, key=lambda p: desc[p][0])


def preset_bec4096(desc, budget, **_):
    for p in ("bec4096+retained", "bec4096"):
        if desc[p][0] <= budget:
            return p
    return preset_smallest(desc, budget)


def reuse_rule(desc, budget, demand, **_):
    """Retain or use plain words when accesses per group are high, else store
    the smallest form."""
    per_group = (demand.probes + demand.ranks) / desc["groups"] + demand.wholes
    if per_group > 8:
        for p in ("plain+counts", "plain", "bec4096+retained"):
            if desc[p][0] <= budget:
                return p
    return preset_smallest(desc, budget)


# ---- Evaluation.

def traces():
    counts = [0, 1e3, 1e5, 1e7]
    for probes in counts:
        for ranks in counts:
            for hot in ((1, 8) if ranks else (8,)):
                for wholes in (0, 1, 10, 100):
                    if probes or ranks or wholes:
                        yield Demand(probes, ranks, hot, wholes)


def examples(data):
    """Decision packages for one mixed demand on a few collections."""
    table = CostTable(data["calibration"])
    d = Demand(1e5, 1e5, 8, 10)
    print(f"Decision packages for {d}, no budget")
    for col in data["collections"]:
        if col["name"] not in ("random", "eighth-empty", "census-income", "wikileaks-noquotes",
                               "dimension_008"):
            continue
        desc = describe(col, table, data["reader_bytes"])
        pkg, rows = advise(desc, d, math.inf)
        alts = "; ".join(f"{a} ({why}, {rows[a][1] / rows[pkg.recommend][1]:.1f}x time, "
                         f"{rows[a][0] / rows[pkg.recommend][0]:.2f}x bytes)"
                         for a, why in pkg.alternatives)
        bodies = predicted_bytes("bec4096", col, data["reader_bytes"])[0]
        retain = (f"Bec4096 readers repay binding after {pkg.retain_after:.1f} accesses per "
                  f"group and add {data['reader_bytes'] * col['groups'] / bodies:.1f}x its bytes"
                  if pkg.retain_after else "retention never pays")
        print(f"  {col['name']}: {pkg.recommend} ({rows[pkg.recommend][0]} B)"
              f"{'' if pkg.resolved else ', unresolved'}; {alts}; {retain}")


def main():
    data = json.load(open(sys.argv[1]))
    if "--examples" in sys.argv:
        return examples(data)
    detail = "--detail" in sys.argv
    reader_bytes = data["reader_bytes"]
    table = CostTable(data["calibration"])
    methods = ["advisor", "advisor-named", "advisor-coarse", "advisor+probe", "preset-smallest",
               "preset-bec4096", "reuse-rule"]
    regret = {m: [] for m in methods}
    by_kind = {}
    over = {m: 0 for m in methods}
    probed = 0
    picks = {}
    cases = 0
    byte_err, cost_err = [], {op: [] for op in OPS}
    for col in data["collections"]:
        groups = col["groups"]
        prices = {p["plan"]: p for p in col["plans"]}
        facts = col
        actual = {p: actual_costs(prices[p], groups) for p in PLANS}
        resident = {p: prices[p]["stored"] + prices[p]["retained"] for p in PLANS}
        for p in PLANS:
            s, r = predicted_bytes(p, facts, reader_bytes)
            byte_err.append((p, (s + r) / resident[p] - 1))
            pred = predicted_costs(p, facts, table)
            for op in OPS:
                cost_err[op].append(pred[op] / actual[p][op] - 1)
        desc = describe(facts, table, reader_bytes)
        smallest = min(resident.values())
        for budget_name, budget in (("none", math.inf), ("2x", 2 * smallest),
                                    ("1.15x", 1.15 * smallest)):
            for d in traces():
                legal = [p for p in PLANS if resident[p] <= budget]
                oracle = min(trace_cost(actual[p], d) for p in legal)
                pkg, _ = advise(desc, d, budget)
                coarse, _ = advise(desc, d.coarse(), budget)
                named, _ = advise(desc, d.naming(), budget)
                chosen = {"advisor": pkg.recommend, "advisor-named": named.recommend,
                          "advisor-coarse": coarse.recommend}
                # Probe: when unresolved, build each finalist on 8 groups and
                # run 1,024 of each demanded operation (one whole pass over
                # those groups), then keep the cheapest. Modeled from the
                # measured prices, not run.
                probe_charge = 0
                finalists = [pkg.recommend] + [a for a, _ in pkg.alternatives
                                               if desc[a][0] <= budget]
                # Probe only when its predicted cost is under 5% of the
                # predicted trace: advice has to recover what it costs.
                worth = (sum(probe_cost(desc[p][1], d, groups) for p in finalists)
                         < 0.05 * trace_cost(desc[pkg.recommend][1], d))
                if pkg.resolved or not worth:
                    chosen["advisor+probe"] = pkg.recommend
                else:
                    pick = min(finalists, key=lambda p: trace_cost(actual[p], d))
                    chosen["advisor+probe"] = pick
                    probe_charge = sum(probe_cost(actual[p], d, groups) for p in finalists)
                    probed += 1
                kw = dict(desc=desc, budget=budget, demand=d)
                chosen["preset-smallest"] = preset_smallest(**kw)
                chosen["preset-bec4096"] = preset_bec4096(**kw)
                chosen["reuse-rule"] = reuse_rule(**kw)
                cases += 1
                best = min(legal, key=lambda p: trace_cost(actual[p], d))
                picks[("oracle", best)] = picks.get(("oracle", best), 0) + 1
                picks[("advisor", pkg.recommend)] = picks.get(("advisor", pkg.recommend), 0) + 1
                for m, p in chosen.items():
                    if resident[p] > budget:
                        over[m] += 1
                        value = math.inf
                    else:
                        cost = trace_cost(actual[p], d) + (probe_charge
                                                           if m == "advisor+probe" else 0)
                        value = cost / oracle - 1
                    regret[m].append(value)
                    by_kind.setdefault((col["kind"], m), []).append(value)
                if detail and chosen["advisor"] != min(legal, key=lambda p: trace_cost(actual[p], d)):
                    print(f"{col['name']:24s} {budget_name:5s} {d} advisor={chosen['advisor']} "
                          f"regret={regret['advisor'][-1]:.2f}")

    def summary(values):
        finite = sorted(v for v in values if math.isfinite(v))
        if not finite:
            return "all over budget"
        q = lambda f: finite[min(len(finite) - 1, int(f * len(finite)))]
        return (f"median {100 * q(0.5):6.1f}%  p90 {100 * q(0.9):7.1f}%  "
                f"max {100 * finite[-1]:8.1f}%  >10%: {sum(v > 0.1 for v in values):4d}")

    print(f"{cases} trace x budget x collection cases; advisor+probe probed {probed}")
    for m in methods:
        print(f"{m:16s} {summary(regret[m])}  over budget: {over[m]}")
    print("\nchoices (oracle / advisor)")
    for p in PLANS:
        print(f"  {p:18s} {picks.get(('oracle', p), 0):5d} {picks.get(('advisor', p), 0):5d}")
    print("\nby collection kind")
    for kind in ("synthetic", "probe", "realroaring"):
        for m in methods:
            print(f"  {kind:12s} {m:16s} {summary(by_kind[(kind, m)])}")
    print("\nprediction error (predicted / measured - 1), median [min, max]")
    for p in PLANS:
        e = [v for q, v in byte_err if q == p]
        print(f"  bytes {p:18s} {statistics.median(e):+.2f} [{min(e):+.2f}, {max(e):+.2f}]")
    for op in OPS:
        e = cost_err[op]
        print(f"  cost  {op:18s} {statistics.median(e):+.2f} [{min(e):+.2f}, {max(e):+.2f}]")


if __name__ == "__main__":
    main()
