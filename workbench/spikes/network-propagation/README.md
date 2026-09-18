# Network propagation and request routing

How should a sender choose routes, relays and forwarding chunks to deliver data
quickly without paying for unnecessary expensive copies?

This is an **executable synthetic model**, with a standalone HTML workbench.
It compares identical delivery obligations under explicit prices, capacities,
delays, offered traffic and failures. It sends no live traffic. The current
Orbital brief supplies motivating examples; the model does not select a
consensus protocol or infer persistence, admission, clearance or read semantics
from transport delivery.

Start with the [findings](FINDINGS.md) and the
[interactive workbench](evidence/20260918/index.html). The
[model and assumptions](MODEL.md) explain the equations, accounting and limits.
The [decision framework](DECISIONS.md) separates obligations, observations,
resource limits and the costs of changing routes.

Two workloads keep the routing question separate from the authority question:

- **Propagation:** one immutable payload becomes available at every named
  recipient. Compare direct fanout, shortest nominal paths, a directed
  price-only baseline, a price/latency search, and policies that repair and
  rewrite routes after observed stalls. Robust selection evaluates declared
  capacity and forwarding-failure cases before choosing an initial tree.
- **Fetch:** an eligible peer or object service receives a request and returns
  the same object. Compare each holder and two price/deadline selectors.
  Eligibility and possession of the correct object are supplied inputs.

## Run and change the model

Run on Linux; prefix these commands with `orb -m ubuntu` in the macOS workspace.
The model is Python, so it does not configure or rebuild the C++ project.

```sh
pip3 install --target build/network-propagation/deps \
  -r workbench/spikes/network-propagation/requirements.txt
env PYTHONPATH=build/network-propagation/deps \
  python3 workbench/spikes/network-propagation/check.py
env PYTHONPATH=build/network-propagation/deps \
  python3 workbench/spikes/network-propagation/run.py \
  --config workbench/spikes/network-propagation/evidence/20260918/config.json \
  --output build/network-propagation/run
```

Use `--quick` for a small apparatus/example run. Use
`--only regional-bulk,global-bulk` to restrict the default campaign. Each run
writes its full `config.json`, samples, summaries, first-object traces,
resource/charge ledgers, provenance, figures and `index.html`.

The workbench's **Export scenario JSON** button produces an editable input:

```sh
env PYTHONPATH=build/network-propagation/deps \
  python3 workbench/spikes/network-propagation/run.py \
  --config scenario.json --output build/network-propagation/custom
```

Open the resulting HTML directly, or serve its directory locally. Its controls
explore recorded results; exporting and rerunning executes changed assumptions.
Figures and HTML can be regenerated from retained results without simulation:

```sh
env PYTHONPATH=build/network-propagation/deps \
  python3 workbench/spikes/network-propagation/report.py OUTPUT_DIRECTORY
```

## Where to add an algorithm

[model.py](model.py) separates topology, route construction and simulation.
`plan(topology, spec, policy)` returns a mapping from recipient/relay node to
its incoming directed edge. `validate_plan` checks rooted reachability and
acyclicity before replay. Add a policy there and list it in a job's `policies`.
The same simulator then supplies contention, framing, loss, repair and prices.
[planner.py](planner.py) searches a price/latency frontier using a separate
planning workload. [resilience.py](resilience.py) scores declared uncertainty
and failure cases. Neither receives the evaluation failure schedule, capacity
changes or random draws. The online controller in `Simulator` changes routes
only after modeled observations; commands, duplicates and controller CPU cost
remain in the ledger.

[scenarios.py](scenarios.py) supplies editable regional, global, edge-origin
and eligible-holder fixtures. A custom JSON can instead supply arbitrary
directed edges and shared capacity pools. Each edge declares its modeled
locations; residency constraints filter routes before optimization.

[check.py](check.py) contains hand-solvable conservation and causality checks,
including a tiny exhaustive price/latency oracle, pipeline/branch crossover,
observation timing, possession checks, shared-cut inference, admission rejection
and preservation of known burst/repair policies. It tests the apparatus,
not Orbital's future network stack.

Useful adjacent evidence: the [CFT network measurements](../cft-commit-latency/az-findings.md)
motivate latency scales and placement sensitivity, while the
[CFT cliff investigation](../cft-commit-latency/commit/cliff.md) motivates
keeping offered load, queueing and deadline failures visible. Their measured
percentiles are not inputs that this model adds together.

## Retained comparison

The retained run has 47 scenarios and 252 scenario/policy records. The standalone
HTML selects 130 contrasting records; `selection.json` records the exact choice.
The full input config and summary cover the entire run. Bulky outcome samples,
full traces, resource ledgers, robust design trials and source snapshots are in
the archive referenced by [artifact.json](evidence/20260918/artifact.json).
Recover it with the [artifact tool](../../tools/artifacts.md), then regenerate
using `report.py` as above. Numerical sources were captured at run start;
`render-source/` captures the renderer used for the final report. Both have
separate hash manifests. No AWS machines or live traffic were used.
