"""Small experiment loops over callable evaluators; no simulator or actor policy."""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from itertools import combinations as pairs, product
import json
import math
from pathlib import Path
import platform
import traceback

from kernel import clone, digest


@dataclass
class Observation:
    cohorts: dict
    metrics: dict
    violations: list = field(default_factory=list)
    details: dict = field(default_factory=dict)


def cohort(arrivals, completions, refusals, *, offered_until, until):
    """Account from authored arrivals, including inputs no actor ever processed.

    IDs identify obligations, not attempts. Times are absolute model ns. Samples
    retain censoring and the offering/draining distinction; no missing latency is 0.
    """
    if offered_until > until:
        raise ValueError("observation must include the whole offering window")
    offered, done, refused = set(arrivals), set(completions), set(refusals)
    if done & refused or not (done | refused) <= offered:
        raise ValueError("unknown or conflicting terminal obligation")
    if any(not isinstance(t, int) or not 0 <= t < offered_until for t in arrivals.values()):
        raise ValueError("arrival outside the half-open offering window")
    for outcomes in (completions, refusals):
        if any(not isinstance(t, int) or not arrivals[k] <= t <= until for k, t in outcomes.items()):
            raise ValueError("outcome outside observation or before arrival")
    unfinished = offered - done - refused
    return dict(offered=len(offered), completed=len(done), refused=len(refused),
                unfinished=len(unfinished), completed_in_window=sum(t < offered_until for t in completions.values()),
                arrivals_ns=arrivals, completions_ns=completions, refusals_ns=refusals,
                latency_ns={k: t - arrivals[k] for k, t in completions.items()},
                unfinished_age_ns={k: until - arrivals[k] for k in sorted(unfinished)})


def _validate(observation):
    if not isinstance(observation, Observation) or not observation.cohorts:
        raise ValueError("evaluator must return Observation with obligation cohorts")
    for group in observation.cohorts.values():
        counts = [group[k] for k in ("offered", "completed", "refused", "unfinished")]
        if any(type(n) is not int or n < 0 for n in counts) or counts[0] != sum(counts[1:]):
            raise ValueError("offered != completed + refused + unfinished")
    if any(value is not None and (type(value) not in (int, float) or not math.isfinite(value))
           for value in observation.metrics.values()):
        raise ValueError("metrics must be finite numbers or explicitly unavailable (None)")
    return clone(asdict(observation))


def combinations(axes, mode="cartesian", accept=lambda case: True, max_candidates=10_000):
    """Deterministic combinations; pairwise covers achievable pairs after filtering.

    Pairwise is a greedy cover, not a minimal cover or assurance about three-way
    interactions. A caller can always author cases directly instead.
    """
    if mode not in {"cartesian", "pairwise"}:
        raise ValueError("unknown combination mode")
    names, values = list(axes), [list(v) for v in axes.values()]
    if math.prod(map(len, values)) > max_candidates:
        raise ValueError("candidate product is too large; narrow axes or set max_candidates explicitly")
    candidates = [dict(zip(names, row)) for row in product(*values)]
    candidates = list({digest(c): c for c in candidates if accept(c)}.values())
    if mode == "cartesian" or len(names) < 2:
        return candidates
    coverage = [{(a, digest(c[a]), b, digest(c[b])) for a, b in pairs(names, 2)} for c in candidates]
    uncovered = set().union(*coverage)
    selected = []
    while uncovered:
        index = max(range(len(candidates)), key=lambda i: len(coverage[i] & uncovered))
        selected.append(candidates[index])
        uncovered -= coverage[index]
    return selected


def _write(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


class Runner:
    """Serial, incremental receipts; memoization is confined to this runner.

    evaluate(case, seed, diagnostic_path) returns Observation. Exceptions survive
    as error records; an event-budget exception never becomes a modeled stall.
    Output must be new. Evaluators choose which detailed diagnostics to retain.
    """
    def __init__(self, evaluate, output=None, provenance=None):
        self.evaluate, self.output = evaluate, Path(output) if output is not None else None
        self.records = {}
        if self.output:
            self.output.mkdir(parents=True, exist_ok=False)
            _write(self.output / "provenance.json", dict(python=platform.python_version(),
                                                        **(provenance or {})))

    def run(self, case, seed):
        case = clone(case)
        if not isinstance(case, dict) or type(seed) is not int:
            raise ValueError("case must be a JSON dictionary and seed an integer")
        identity = digest([case, seed])
        if identity in self.records:
            return clone(self.records[identity])
        record = dict(id=identity, case=case, seed=seed, status="running", observation=None, exception=None)
        if self.output:
            _write(self.output / (identity + ".json"), record)
        try:
            observed = self.evaluate(clone(case), seed, self.output / (identity + ".evidence") if self.output else None)
            record.update(status="ok", observation=_validate(observed))
        except Exception:
            record.update(status="error", exception=traceback.format_exc())
        self.records[identity] = record
        if self.output:
            _write(self.output / (identity + ".json"), record)
        return clone(record)

    def matched(self, cases, seeds):
        seeds = tuple(seeds)
        if not seeds or len(set(seeds)) != len(seeds):
            raise ValueError("use a nonempty panel of distinct matched seeds")
        return [self.run(case, seed) for case in cases for seed in seeds]


def all_work(record):
    """Conservative search default: every cohort correct, completed, nonempty overall."""
    if record["status"] != "ok":
        return False
    observation = record["observation"]
    groups = observation["cohorts"].values()
    return (not observation["violations"] and sum(g["offered"] for g in groups) > 0
            and all(g["offered"] == g["completed"] for g in groups))


def ramp(runner, base, field, levels, seeds, limits=None):
    """Observe every authored level; no monotonicity or deadlock assumption.

    Limits are metric upper bounds. Model/evaluator errors are unknown, not a
    crossing. Completion is judged after the adapter's explicit drain horizon.
    """
    rows, seeds = [], tuple(seeds)
    for level in levels:
        records = runner.matched([dict(base, **{field: level})], seeds)
        reasons = []
        for r in records:
            if r["status"] != "ok":
                reasons.append("simulator_or_evaluator_error")
                continue
            o = r["observation"]
            if o["violations"]:
                reasons.append("correctness")
            for name, group in o["cohorts"].items():
                for state in ("refused", "unfinished"):
                    if group[state]:
                        reasons.append(f"{name}:{state}")
            for name, bound in (limits or {}).items():
                value = o["metrics"].get(name)
                if value is None:
                    reasons.append(f"{name}:unavailable")
                elif value > bound:
                    reasons.append(f"{name}:limit")
        state = "unknown" if any(r["status"] != "ok" for r in records) else "outside" if reasons else "within"
        rows.append(dict(level=level, state=state, reasons=sorted(set(reasons)), records=[r["id"] for r in records]))
    return dict(field=field, limits=limits or {}, levels=rows,
                first_outside=next((r["level"] for r in rows if r["state"] == "outside"), None))


def _frontier(evaluations):
    feasible = [e for e in evaluations if e["vector"] is not None]
    return [e for e in feasible if not any(
        all(a <= b for a, b in zip(other["vector"], e["vector"])) and
        any(a < b for a, b in zip(other["vector"], e["vector"])) for other in feasible)]


def hill_climb(runner, starts, neighbors, *, seeds, heldout_seeds, objectives,
               validation_cases=lambda case: [case], feasible=all_work, max_cases=32):
    """Bounded Pareto neighborhood search, then untouched seed/scenario validation.

    All objectives are minimized using each panel's worst value. Infeasible or
    dominated regions aren't expanded; this does not establish a global optimum.
    Validation never feeds selection. Include independent starts to cross valleys.
    """
    seeds, heldout_seeds, objectives = tuple(seeds), tuple(heldout_seeds), tuple(objectives)
    if not seeds or not heldout_seeds or set(seeds) & set(heldout_seeds) or not objectives:
        raise ValueError("search requires objectives and disjoint nonempty seed panels")
    if max_cases <= 0:
        raise ValueError("positive search budget required")
    evaluations, expanded = {}, set()

    def assess(case):
        identity = digest(case)
        if identity in evaluations or len(evaluations) >= max_cases:
            return
        records = runner.matched([case], seeds)
        vector = None
        if all(feasible(r) for r in records):
            values = [[r["observation"]["metrics"].get(k) for k in objectives] for r in records]
            if all(v is not None for row in values for v in row):
                vector = [max(row[i] for row in values) for i in range(len(objectives))]
        evaluations[identity] = dict(case=clone(case), vector=vector, records=[r["id"] for r in records])

    starts = list(starts)
    for case in starts:
        assess(case)
    while len(evaluations) < max_cases:
        pending = [e for e in _frontier(evaluations.values()) if digest(e["case"]) not in expanded]
        if not pending:
            break
        for entry in pending:
            expanded.add(digest(entry["case"]))
            for case in neighbors(clone(entry["case"])):
                assess(case)
    selected = _frontier(evaluations.values())
    validation = []
    # Keep the starting competitors on the same held-out panel, even if dominated.
    finalists = {digest(c): c for c in starts + [e["case"] for e in selected]}
    for case in finalists.values():
        cases = list(validation_cases(clone(case)))
        if not cases:
            raise ValueError("held-out case panel cannot be empty")
        records = runner.matched(cases, heldout_seeds)
        validation.append(dict(strategy=case, feasible=all(feasible(r) for r in records),
                               records=[r["id"] for r in records]))
    return dict(objectives=objectives, aggregation="worst across matched seeds",
                seeds=seeds, heldout_seeds=heldout_seeds,
                evaluated=list(evaluations.values()), frontier=selected, validation=validation,
                budget_reached=len(evaluations) >= max_cases)
