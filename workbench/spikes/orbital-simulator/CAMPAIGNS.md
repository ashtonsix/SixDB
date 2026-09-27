# Asking the simulator many related questions

The newest [composition campaign](demanding_campaign.py) exercises a genuinely
deferred first read at a registered old cut, independent foreground work, checker
and source recovery, decision recovery and tight checker memory. Its cases reuse
the same authored program with standalone and prepared-quorum admission. Missing
or mismatching required checks are incomplete-progress controls, not successful
abort handling. [Retention notes](RETENTION.md) distinguish what is composed from
the remaining reclamation, authority and physical-object questions.

Run it from a source capture so ongoing edits cannot change the experiment:

```sh
orb -m ubuntu python3 workbench/tools/capture.py create build/captures/my-checked-composition
orb -m ubuntu python3 build/captures/my-checked-composition/workbench/spikes/orbital-simulator/demanding_campaign.py \
  --output build/experiments/orbital-simulator/my-checked-composition
```

The receipt retains per-cohort offers, completions, refusals and unfinished work,
as well as declared inputs whose event-relative trigger never occurred. Missing
milestones are coverage gaps, not evidence that the unexercised fault was survived.
The same distinction applies when introducing new cases through the general
campaign API below.

The retained pass adds six continuing-write cases to the default 66-history
matrix: `Case(point_count=48, point_interval_ns=100_000, until_ns=8_000_000)`
with both values of `replicated` and seeds 1/7/19. These use the same `evaluate`
function through `Runner`; the exact case dictionaries and source identity are
in [the selected evidence](evidence/checked-composition.json).

[campaigns.py](campaigns.py) composes ordinary case dictionaries with a callable
evaluator. It knows neither actors nor topology. [flow_campaign.py](flow_campaign.py)
is a complete example using the existing dataflow actors and `World`.
[traffic_campaign.py](traffic_campaign.py) uses transaction actors; the
[adversarial adapter](adversity.py) exercises existing admission/composed paths.
All three share the same physical runtime and evaluator interface.

```python
from campaigns import Runner, combinations, ramp
from flow_campaign import evaluate, provenance

runner = Runner(evaluate, output="build/my-campaign", provenance=provenance())
cases = combinations(dict(placement=["source", "destination", "consumer"],
                          enhance=[False, True]))
records = runner.matched(cases, seeds=[1, 7])
load = ramp(runner, {"placement": "source", "enhance": True},
            "period_ns", [4000, 800, 600, 400, 100], seeds=[1, 7],
            limits={"foreground_max_ns": 20_000})
```

Run from this directory or add it to Python's import path. `mode="pairwise"`
selects a deterministic greedy cover of feasible pairs; `accept=predicate` filters
impossible combinations first. This saves runs but misses higher-order interactions.
Handwritten cases fit the same runner. The candidate-product bound protects against
an accidental combinatorial explosion and can be explicitly enlarged.

The runner writes an initial receipt, then replaces it with each completed result.
It reuses identical case/seed results within that runner. The output directory must
be new; there is no cross-version cache or implicit resume. Source hashes and Python
version identify the run. Use the existing [source capture](../../tools/README.md#captured-experiment-runs)
when sources may change; hashes alone cannot recover source bytes.

## An evaluator owns the meaning

`evaluate(case, seed, diagnostic_path) -> Observation(cohorts, metrics, violations,
details)` constructs and runs the model. Case dictionaries are JSON-compatible;
builders, incident predicates and observers remain ordinary Python functions.
`diagnostic_path` is either `None` or a not-yet-created directory for selected
traces/choices. The flow adapter preserves incomplete/incorrect runs and exceptions;
successful cases can request `diagnostics=True`.

Each cohort supplies `offered`, `completed`, `refused`, `unfinished`, conserving
offered obligations. The optional `cohort()` helper accepts stable obligation IDs
mapped to absolute arrival/completion/refusal times and retains latency samples,
offering-window completions and unfinished ages. Count authored arrivals even when
no actor received them. Retry attempts and temporary resource refusals are separate
from terminal application refusals. Cohorts are separate obligations/stages, not
necessarily populations whose throughput can be added.

An independent observer reports wrong results in `violations`. Numerical metrics
are finite values or `None` for unavailable/censored results. Details can preserve
per-class outcomes, wait reasons and model assumptions. Runner status `ok` means
evaluation finished, not that the system succeeded. Exceptions have status `error`;
event-budget exhaustion is a simulator limit, never evidence of system deadlock.

`ramp()` evaluates every level, even after a crossing, and retains reversals.
Its explicit boundary is any incorrect/refused/unfinished work or exceeded metric
limit. An exception yields `unknown`. A crossing depends on the offered window,
drain allowance and chosen limits; it is not automatically a capacity estimate.

## Search without rewarding lost work

`hill_climb(runner, starts, neighbors, seeds=..., heldout_seeds=...,
objectives=..., validation_cases=..., max_cases=...)` explores neighborhoods of
the current Pareto alternatives. The default feasibility check requires correct
completion of every offered obligation in every cohort. A caller can supply a
different explicit constraint. All objectives are minimized, conservatively using
the worst value across matched seeds. Missing metrics cannot win.

Only feasible nondominated neighborhoods expand; multiple starts help cross valleys.
This bounded search establishes no global optimum. The final alternatives **and
starting competitors** are then evaluated on disjoint seeds and caller-authored
held-out case mixes. Those outcomes do not feed selection. Changing only seeds
tests event/random choices, not generality across workloads or topologies.

## The first flow campaign

From the repository root on the Mac:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-simulator/flow_campaign.py \
  --output build/experiments/orbital-simulator/my-campaign
orb -m ubuntu python3 -m unittest discover \
  -s workbench/spikes/orbital-simulator -p test_campaigns.py
```

The study compares all three relay placements with raw/shared filtering. It varies
open-loop foreground arrival period, worker-pause duration, batch size, in-flight
window, retry interval and asymmetric WAN links. A fixed offering interval followed
by a bounded drain separates throughput from delayed completion. Planned arrival
to completion includes handler queuing; the older actor-local latency remains in
details. Late-window throughput/backlog and a tenfold longer offering interval
challenge conclusions drawn from a short burst. Pauses longer than observation
leave censored obligations, not a declared deadlock. Drain of work outstanding at
worker resume is reported separately from later arrivals and offering-window drain.

All costs are synthetic. These campaigns exercise comparison machinery and expose
trade-offs in these bindings; they do not select a production placement or promise
SixDB throughput. Full receipts, samples and diagnostics remain in ignored output.
The adjacent findings describe selected comparisons; repeat the command to regenerate.

## Execution, contention and recovery campaigns

```sh
orb -m ubuntu python3 workbench/spikes/orbital-simulator/traffic_campaign.py \
  --output build/experiments/orbital-simulator/my-traffic-campaign
```

`traffic.evaluate` accepts `{"config": {...}, "strategy": {...}}`; the frozen
`Config` and `Strategy` dataclasses list the options. `traffic_campaign.evaluator`
also accepts `load_interval_ns`, deriving the offered count from `offering_ns`,
and top-level severity/memory axes for `ramp`. For example:

```python
from campaigns import Runner
from traffic_campaign import evaluator, provenance
from adversity import cases, evaluate as adversarial

r = Runner(evaluator, provenance=provenance())
result = r.run({"config": {"workload": "global-scan", "topology": "wan",
    "count": 100, "interval_ns": 2_000_000, "slow_every": 100,
    "drain_ns": 800_000_000}, "strategy": {"ordering": "mv"}}, seed=19)
fault_results = Runner(adversarial).matched(cases(), seeds=[1, 7, 19])
```

The built-in study covers blind and read-dependent points, hot updates, scans with
narrow output, maximum-selected updates, wide updates, conditional/no-op effects,
bridges, transfers and complete replacements. LAN, MAN, asymmetric paths and rare
WAN work overlap a continuing local stream. Strategy knobs change ticket lifetime,
client credits/class lanes, journal batching, waiter wakeup, retries, preparation
order and compute quanta. Supersession is a separate application-enabled experiment:
a fully installed replacement can make an older pending effect irrelevant to a
particular cell read. It cannot erase that effect's other obligations or older views.

The observer reconstructs snapshots and effects independently, binds installed
values to durable decisions, follows committed journals across lost callbacks,
and requires scored client responses to follow installed outcomes. Unknown or
unfinished requests remain in the authored denominator. Power-cycle recovery
reconstructs actors from journals; arbitrary process-only restart is unsupported
in this binding because old writes would need an additional fence/reconciliation.
The original admission fixture separately tests that late-write problem.

This is an execution study with prepared single authorities, not a durability
quorum or election model. Metadata costs use an explicit, uncalibrated traversal
coefficient; Python interpreter overhead is not simulated CPU. Initial source
state, working continuations and retained journals consume modeled capacity.
There is no history reclamation, so an indefinitely extended run eventually fills
storage/memory even below its service-rate limit. Finite-horizon overload and
retention exhaustion must not be presented as one sustainable-throughput number.

For stalled or starved work, `diagnose.symptoms(observation, quiet_ns)` reports
quiet cohorts and old individual obligations, even while another cohort progresses.
The same helper accepts a runner receipt via `diagnose.py RECEIPT --quiet-ns N`.
The interval is an explicit diagnostic choice; a quiet WAN wait can be legitimate.
Use retained waits/causal traces and `World.explain(op)` to investigate. An event
budget error remains a simulator limitation; a lack of completions alone never
proves a deadlock.

## Protocol fidelity and composition

The historical traffic campaign predates the corrected reservation lifecycle.
[fidelity_campaign.py](fidelity_campaign.py) runs each source tree in a separate
process, checking identical authored inputs and refusing a mixed-source summary:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-simulator/fidelity_campaign.py \
  --old-source build/captures/orbital-gauntlet/workbench/spikes/orbital-simulator \
  --output build/experiments/orbital-simulator/my-fidelity-comparison
```

The corrected model journals requests before deciding which waiters can proceed.
It keeps reservation, minimum announcement, position fixing and release separate.
`Shard.apply(request)` folds an agreed input and returns newly enabled replies;
`logical_state()` includes waiting requests, captured reads and ordering bounds.
These pure operations contain no remote observation or physical clock. The actor
adapter supplies agreed input and performs actual I/O.

[replicated_campaign.py](replicated_campaign.py) obtains those inputs from actual
prepared-leader quorum writes and individual witness receipts. It compares three
consumers, including their full ordering state and protocol outputs at each common
prefix, while application checks independently reconstruct snapshots and outcomes.
The full-body log, coordinator-local decisions and device-reset recovery remain
explicit bounds; this does not substitute for the producer-frontier admission path.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-simulator/replicated_campaign.py \
  --output build/experiments/orbital-simulator/my-replicated-campaign \
  --evidence build/experiments/orbital-simulator/my-replicated-summary.json
orb -m ubuntu python3 workbench/spikes/orbital-simulator/release_campaign.py \
  --output build/experiments/orbital-simulator/my-local-release-comparison
orb -m ubuntu python3 workbench/spikes/orbital-simulator/recovery_footprint.py \
  --output build/experiments/orbital-simulator/my-recovery-footprint.json
```

The release comparison removes only the explicit release round after fixing a
position locally. Every minimum must still be known before choosing the immutable
position, and computation still waits for every fix acknowledgement. Both strict
and candidate policies use the corrected queue/read rules. Each source/configuration
is retained with its results; changing protocol and changing a broken baseline are
different comparisons.

The recovery comparison runs the same healthy history before selecting whole-journal
or record-at-a-time reopening. It includes a newly offered write after reset; an old
completed cohort cannot conceal failed recovery. Required retained history still
grows in both modes. [RETENTION.md](RETENTION.md) distinguishes temporary recovery
footprint, reclamation and the capacity needed to finish admitted transactions.
