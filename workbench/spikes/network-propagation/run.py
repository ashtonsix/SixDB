#!/usr/bin/env python3
"""Run paired synthetic scenarios and export a replayable algorithm workbench."""
import argparse
import copy
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import sys
import time

import networkx
from model import Topology, Simulator, plan
from scenarios import campaign, fetch_choice


def clean(value):
    if isinstance(value,float) and not math.isfinite(value): return None
    if isinstance(value,dict): return {str(k):clean(v) for k,v in value.items()}
    if isinstance(value,list): return [clean(v) for v in value]
    return value


def quantile(values,q):
    xs = sorted(math.inf if x is None else x for x in values)
    p = (len(xs)-1)*q
    lo,hi = math.floor(p),math.ceil(p)
    if not math.isfinite(xs[hi]): return None
    return xs[lo]+(p-lo)*(xs[hi]-xs[lo])


def write_json(path,value):
    path.write_text(json.dumps(clean(value),indent=2,sort_keys=True,allow_nan=False)+"\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--output",type=Path,required=True)
    ap.add_argument("--config",type=Path)
    ap.add_argument("--quick",action="store_true")
    ap.add_argument("--only",help="Comma-separated scenario names")
    args = ap.parse_args()
    here = Path(__file__).resolve().parent
    sources = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in here.iterdir() if p.suffix in (".py",".html",".txt")}
    config = json.loads(args.config.read_text()) if args.config else campaign(args.quick)
    if args.only:
        config["jobs"] = [j for j in config["jobs"] if j["name"] in args.only.split(",")]
        if not config["jobs"]: raise ValueError("no matching scenarios")
    out = args.output
    out.mkdir(parents=True,exist_ok=True)
    snapshot = out/"source"
    snapshot.mkdir(exist_ok=True)
    for name in sources:
        (snapshot/name).write_bytes((here/name).read_bytes())
    write_json(out/"config.json",config)
    results, summaries, samples = [], [], []
    start = time.monotonic()
    for job in config["jobs"]:
        topo = Topology(config["topologies"][job["topology"]])
        replay_cache = {}
        for policy in job["policies"]:
            spec = copy.deepcopy(job["spec"])
            planning = None
            if policy in ("static","adaptive","adaptive-unbounded","robust"):
                spec["adaptive"] = policy != "static"
                if policy == "adaptive-unbounded":
                    spec.pop("max_inflight_objects",None)
            if spec.get("mode") == "fetch":
                spec = fetch_choice(topo,spec,policy)
                route = plan(topo,spec,"direct")
            else:
                if policy == "robust":
                    from resilience import select
                    from planner import optimize
                    planning = select(topo,spec,[x["route"] for x in optimize(topo,spec)["candidates"]])
                    route = planning["route"]
                else:
                    route = plan(topo,spec,"balanced" if policy in ("static","adaptive","adaptive-unbounded") else policy)
            runs = []
            for seed in config["seed_list"]:
                cache_key = (json.dumps(spec,sort_keys=True),tuple(sorted(route.items())),seed)
                if cache_key not in replay_cache:
                    replay_cache[cache_key] = Simulator(topo,spec,route,seed,trace=seed==config["seed_list"][0]).run()
                run = replay_cache[cache_key]
                runs.append(run)
                for row in run["rows"]:
                    samples.append(dict(scenario=job["name"],policy=policy,seed=seed,**row))
            rows = [x for run in runs for x in run["rows"]]
            n = len(rows)
            total_usd = sum(sum(r["costs"].values()) for r in runs)
            total_wire = sum(r["wire_bytes"] for r in runs)
            summary = dict(scenario=job["name"],policy=policy,topology=job["topology"],mode=spec["mode"],
                           samples=n,source=spec["source"],size_bytes=spec["size_bytes"],chunk_bytes=spec["chunk_bytes"],
                           mtu=spec["mtu"],rate_per_s=spec["rate_per_s"],deadline_us=spec["deadline_us"],
                           complete=sum(r["last_us"] is not None for r in rows),
                           deadline_pct=100*sum(r["deadline_met"] for r in rows)/n,
                           usd_per_object=total_usd/n,usd_per_GiB=total_usd/(n*spec["size_bytes"])*2**30,
                           wire_amplification=total_wire/(n*spec["size_bytes"]),
                           retries=sum(r["retries"] for r in runs),repairs=sum(r["repairs"] for r in runs),
                           duplicates=sum(r["duplicates"] for r in runs),drops=sum(r["drops"] for r in runs))
            summary.update(rejected=sum(r["rejected"] for r in runs),
                           peak_active=max(r["peak_active"] for r in runs),
                           reserved_payload_bytes=max(r["reserved_payload_bytes"] for r in runs),
                           rewrites=sum(x["kind"]=="rewrite" for r in runs for x in r["controller_events"]))
            for metric,field in [("delivery","last_us"),("control","control_us"),("confirmed","confirmed_us"),("first","first_us"),("remote","first_remote_copy_us")]:
                for percentile in (50,90,99,99.9):
                    summary[f"{metric}_p{percentile}_us"] = quantile([r[field] for r in rows],percentile/100)
            summaries.append(summary)
            from planner import depths, optimize
            summary["max_depth"], summary["total_depth"] = depths(topo,spec,route)
            if policy in ("balanced","latency","static","adaptive","adaptive-unbounded"):
                planning = optimize(topo,spec)
            results.append(dict(summary=summary,spec=spec,route=route,runs=runs,planning=planning))
            print(f"{job['name']:29s} {policy:14s} complete={summary['complete']}/{n} deadline={summary['deadline_pct']:.1f}% p99={summary['delivery_p99_us']} cost/GiB={summary['usd_per_GiB']:.4f}",flush=True)
    write_json(out/"results.json",results)
    for name,rows in [("summary.csv",summaries),("samples.csv",samples)]:
        with (out/name).open("w",newline="") as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator="\n")
            w.writeheader()
            w.writerows(clean(rows))
    write_json(out/"provenance.json",dict(kind="synthetic simulation, not measured cloud latency",python=sys.version,
                                         networkx=networkx.__version__,platform=platform.platform(),source_sha256=sources,
                                         config_sha256=hashlib.sha256((out/"config.json").read_bytes()).hexdigest(),
                                         elapsed_wall_seconds=time.monotonic()-start,seeds=config["seed_list"],
                                         samples=len(samples),runs=sum(len(r["runs"]) for r in results)))
    from report import report
    report(out)


if __name__ == "__main__": main()
