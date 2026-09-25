#!/usr/bin/env python3
"""Bounded retry-cut, delay and selection comparisons; synthetic inputs only."""

import argparse
import copy
import json
from pathlib import Path

from batch import independent_set
from run import execute, model_identity
from scenarios import local_queue, reservation_cycle, workload


def configuration(s, mode, period, delay, solver="age", fast=True):
    s = copy.deepcopy(s)
    s["policy"].update({"yield": mode, "batch_us": period, "arbitration_us": delay,
                         "solver": solver, "fast_retries": fast})
    return s


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for name, scenario in (("reservation", reservation_cycle()), ("hotspot80", workload())):
        settings = [(mode, period, delay, "age", True)
                    for mode in ("batch-reset", "batch-hold")
                    for period in (250, 500, 1500) for delay in (100, 500, 1200)] if name == "reservation" else [
                        (mode, 500, delay, solver, fast)
                        for mode in ("batch-reset", "batch-hold") for delay in (100, 1200)
                        for solver in ("age", "work", "bounded") for fast in (False, True)]
        for mode, period, delay, solver, fast in settings:
            s = configuration(scenario, mode, period, delay, solver, fast)
            result = execute(s)
            rows.append({"scenario": name, "policy": mode, "period_us": period, "arbitration_us": delay,
                         "solver": solver, "fast_retries": fast, "scenario_sha256": result["scenario_sha256"],
                         "trace_sha256": result["trace_sha256"], "stop_reason": result["stop_reason"], **result["summary"]})
    for local in (False, True):
        s = configuration(local_queue(), "batch-hold", 500, 1200)
        s["policy"]["local_solver"] = local
        result = execute(s)
        rows.append({"scenario": "local40", "policy": "batch-hold", "period_us": 500, "arbitration_us": 1200,
                     "solver": "age", "fast_retries": True, "local_solver": local,
                     "scenario_sha256": result["scenario_sha256"], "trace_sha256": result["trace_sha256"],
                     "stop_reason": result["stop_reason"], **result["summary"]})
    baseline = execute(local_queue())
    rows.append({"scenario": "local40", "policy": "older", "scenario_sha256": baseline["scenario_sha256"],
                 "trace_sha256": baseline["trace_sha256"], "stop_reason": baseline["stop_reason"], **baseline["summary"]})
    shapes = []
    for n in (16, 64, 256):
        for shape in ("clique", "star", "chain"):
            graph = {v: set() for v in range(n)}
            for a in range(n):
                for b in range(a + 1, n):
                    if shape == "clique" or (shape == "star" and a == 0) or (shape == "chain" and b == a + 1):
                        graph[a].add(b)
                        graph[b].add(a)
            chosen, visits = independent_set(graph, graph, lambda v: v, dict.fromkeys(graph, 1), set(), "bounded")
            edges = sum(map(len, graph.values())) // 2
            shapes.append({"shape": shape, "transactions": n, "conflict_components": 1,
                           "undirected_edges": edges, "bidirectional_arcs": 2 * edges,
                           "key_claim_incidences": n if shape == "clique" else 2 * (n - 1),
                           "selected_with_oldest_anchor": len(chosen), "search_nodes": visits})
    result = {"model_files_sha256": model_identity(), "max_steps": 2000,
              "assumptions": "Atomic complete multishard retry cut; aggregate arbitration delay; atomic verdict application; no batch CPU charge.",
              "comparisons": rows, "graph_shapes": shapes}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"Wrote {len(rows)} run comparisons and {len(shapes)} graph probes")


if __name__ == "__main__":
    main()
