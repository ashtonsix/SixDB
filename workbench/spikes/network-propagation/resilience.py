"""Declared design ensembles for robust routing; never evaluation-event foresight."""
import copy
import math

from model import Simulator
from planner import identity, percentile, planning_spec


def select(topo, spec, routes):
    design = planning_spec(spec)
    design["messages"] = max(spec.get("robust_messages",8),32 if spec.get("burst") else 1)
    seed = spec.get("robust_seed",314159)
    # Identical fault domains and workloads for every candidate. Equal design
    # weights are sensitivity cases, not probabilities of real-world failures.
    cases = [("nominal",{}), ("capacity-haircut",dict(capacity_scale=spec.get("capacity_scale",1)*spec.get("uncertain_capacity_scale",.7)))]
    for source in spec.get("design_failure_sources",[]):
        cases.append(("egress-blackhole:"+source,dict(failure=dict(source=source,start_us=0,kinds=["data"]))))
    evaluated=[]
    for key in sorted({identity(p) for p in routes}):
        route=dict(key)
        trials=[]
        for name,changes in cases:
            trial=copy.deepcopy(design)
            trial.update(changes)
            r=Simulator(topo,trial,route,seed).run()
            n=len(r["rows"])
            trials.append(dict(case=name,offered=n,rejected=r["rejected"],
                               complete=sum(x["last_us"] is not None for x in r["rows"]),
                               deadline_misses=sum(not x["deadline_met"] for x in r["rows"]),
                               p90_us=percentile([x["last_us"] for x in r["rows"]],.9),
                               usd_per_offered=sum(r["costs"].values())/n,
                               repairs=r["repairs"],duplicates=r["duplicates"],rewrites=sum(x["kind"]=="rewrite" for x in r["controller_events"])))
        risk=(max(1-x["complete"]/x["offered"] for x in trials),
              max(x["deadline_misses"]/x["offered"] for x in trials))
        evaluated.append(dict(route=route,risk=risk,trials=trials,
                              latency_us=max(x["p90_us"] for x in trials),
                              design_mean_usd=sum(x["usd_per_offered"] for x in trials)/len(trials)))
    best_risk=min(x["risk"] for x in evaluated)
    eligible=[x for x in evaluated if x["risk"]==best_risk]
    fastest=min(x["latency_us"] for x in eligible)
    finite=math.isfinite(fastest)
    budget=fastest*(1+spec.get("planner_slack",.1)) if finite else None
    within=[x for x in eligible if budget is None or x["latency_us"]<=budget]
    selected=min(within,key=lambda x:(x["design_mean_usd"],x["latency_us"],identity(x["route"])))
    frontier=[x for x in eligible if not any(y["design_mean_usd"]<=x["design_mean_usd"] and y["latency_us"]<=x["latency_us"] and
                (y["design_mean_usd"]<x["design_mean_usd"] or y["latency_us"]<x["latency_us"]) for y in eligible)]
    return dict(route=selected["route"],selected=selected,frontier=frontier,candidates=evaluated,
                ensemble=[dict(name=n,changes=c) for n,c in cases],planning_seed=seed,planning_messages=design["messages"],
                latency_budget_us=budget,status="finite bound found" if finite else "no finite latency bound found",
                objective="Lexicographic worst design noncompletion fraction, worst design deadline-miss fraction; then minimum design-mean variable price within the declared allowance of best worst-design p90. All offered objects count. Equal design weights are not probabilities. Fixed provisioned costs excluded.")
