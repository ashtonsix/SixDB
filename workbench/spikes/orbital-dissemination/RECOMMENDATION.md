# Provisional dissemination recommendation

The evidence supports an overall direction: **schedule typed delivery and work
obligations over finite shared resources, choosing representation, execution
placement and routes together**. Use a small set of constrained routes with
local delegation, per-obligation delivery knowledge and explicit repair capacity.
Keep latency-critical release dependencies separate from the completion of every
background subscriber. This is a composed recommendation for Orbital LEAD to
assess, not an adopted protocol, universal API or completed production design.

## The proposed composition

**Start with the required outcome, not the recipient graph.** An application
declares usable inputs, outputs, cut/version, required coverage, legal transforms
and release/effect authority. Distinguish interchangeable senders from independent
contributions, optional consumers from required subscribers, and query-serving
work from mandatory repeated execution/checking. The network layer cannot infer
a reduction law, silently weaken durability or turn an absent partition into an
empty result. These distinctions belong in the routing/scheduling input even
when the eventual interface is small.

**Carry obligations through route changes.** Stable logical identity and a
coverage/custody ledger should survive sockets, trees and physical host movement.
Separate received, durably retained, applied, replied and retired knowledge as
the workload requires. Use explicit subscription cuts with a retained snapshot/
tail or an equivalent complete-coverage mechanism for new recipients. Forwarders
may acknowledge custody only when they can keep or reconstruct what their claim
promises. Downstream branch failures remain owed; one trunk ACK does not complete
all leaves. Keep a bounded active-send window separate from historical retention.

**Prefer constrained bulk routes and a few useful alternatives.** Generate cheap
regional/provider trunks with shallow local distribution, checking actual shared
CPU/NIC/uplink capacity and correlated exposure. Allow short direct paths for
small required evidence or latency-sensitive recipients when they earn their
cost. Edmonds is a cost baseline/candidate generator; shortest paths and direct
fanout are useful comparands. Constrain depth/fanout and balance the resulting
service demand. Select routes at a grain suited to bytes, bursts and batching,
not just equal message counts over an arbitrarily long interval. Local delegation
can reduce global route detail, but still owns coverage, finite resources and
failure handling.

**Make transformation and origin part of that choice.** Consider raw bytes,
requests, references, selectors, projections, partial aggregates, code/continuations
and derived state as different legal ways to satisfy an obligation. Price cold
inputs, interpreter/dictionary versions, encoding, validation and retention as
well as outgoing bytes. Choose per recipient: a resident consumer may prefer a
tiny predicate request; a nonresident one may benefit enormously from selected
rows. Edge-origin execution/distribution can avoid a cloud-egress leg that later
relay routing cannot remove. Include the response and repair directions, plus
fixed/request/tiered economic terms, before choosing a provider gateway.

**Control admission, work grain and lifetime at the real resources.** Packet
batching and semantic aggregation are separate operations. Use bounded queues,
output/catch-up credits and sufficiently short stopping points; measure actual
retirement after cancellation. Protect the small critical work path from long
nonpreemptible extension/scan jobs. Core or host separation is an explicit
capacity allocation with costs, not a free speedup. Read-serving should partition
exact logical coverage across usable replicas within each shard and reduce across
shards; whole-query placement remains useful for small or locality-sensitive
requests. Bound hedges and duplicated execution by spare capacity and observed
benefit. Do not confuse failed/background work with a low network bill.

**Separate fast flow control from slower placement decisions.** Per-path packet
work, send credits, receiver pacing and observed receipts can react without moving
authority. Route portfolios, batch/fragment sizes and transform placement change
at a coarser grain; replica/witness relocation additionally pays for state,
warmup, old/new overlap and any necessary authority transfer. Use actual delayed
observations, hysteresis and a predicted benefit horizon. Cheap flow claims or
network probes may be useful inputs, but they cannot establish remote persistence
or application readiness by themselves. This control architecture is a proposal;
only bounded pieces are implemented in the spike.

## What the evidence is strong enough to choose

The following conclusions recur across authored counterexamples, current
source/measurement evidence and independent executable checks:

| Choose as a design direction | Why |
| --- | --- |
| All-offered outcome accounting plus independent-cohort metrics | Better completed p99 and fewer transmitted bytes repeatedly hide lost or starved obligations. Healthy aggregate results can hide a damaged producer or background service. |
| Physical shared-resource and byte-lifetime accounting | Logical roles/trees do not reveal NIC, packet CPU, active work, reply incast or retention bottlenecks. |
| Distinct packet, execution, storage, publication and retention grains | Batching saves packet work; small work units reduce blocking; result/cancellation lifetime persists beyond the request handler. One grain cannot optimize all of them. |
| Exact scoped identity, coverage and evidence | Retry, frontiers, empty results, joins and movement fail without it. Sender ranking and a new socket do not resolve effect ambiguity. |
| Constrained routing candidates and representation-aware placement | Unbounded noise creates chains; the cheapest short tree can still overload; enhancement wins or loses with residency, output size and edge direction. |
| Separate future policy changes from existing obligations | An adaptive policy, new replica or changed route cannot erase an existing prefix hole, missing result or historical retention debt. |

These support the composition above. They do not select the number of trees,
batch deadline, queue size, hash/codec, congestion controller or topology for all
deployments. Such constants would overfit the authored workloads.

## Alternatives to reject as defaults

- **A single unconstrained minimum-cost tree or noise family as the latency
  optimizer.** Cost, dependency depth, shared-resource load and completion
  semantics are different objectives. Preserve the algorithm as a baseline.
- **Blanket redundant fanout or unconditional read hedging.** Both can trigger
  queue/retry amplification and reduce successful service. Redundancy still has
  a role when its timely independent copy repays its actual resource demand.
- **Enhance every message at one fixed relay.** It can add latency and bytes
  when bases are already resident, or move large output across the costly edge.
  Optional sidecars also consume resources after losing the race.
- **Treat retries or deterministic sender selection as the delivery protocol.**
  They cannot restore destroyed unique information, prove ambiguous external
  effects, or discover all new required recipients.
- **One global health bit or always-early proposals justified by healthy latency.**
  The [observed timing comparison](PROPOSAL-TIMING.md) shows retained early holes,
  expired warnings and false alarms defeating the intended benefit. The current
  eligibility-before-proposal rule should remain the reference until the
  conditional-entry safety/locality argument and benefit are established.
- **Optimizing the named follower layout in isolation.** Its useful idea is
  concurrent work. Also examine preparation, streaming, delayed irreversible
  order allocation, integrated payload/evidence layouts and independent receiver
  work. None requires that exact role placement.

## What prevents a more specific production recommendation

The survey has converged on those directions; the unresolved questions are now
specific. The simulator has no measured joint distribution of network, persistence,
OS/sandbox and application service at the target workload. The 170/250µs path is
still a synthetic sensitivity study, and the exact post-persistent effect boundary
is not yet a contract. Current CFT measurements constrain calibration but cannot
be added as marginal percentiles to prove that endpoint.

The early conditional proposal, admission/handoff and recovery evidence are not
a composed implemented consensus protocol. A safe mode switch cannot be inferred
from a timing simulation. Likewise, the implemented adaptive watchdog is not a
continuous route/placement controller, and static MTU comparisons plus ideal-code
arithmetic are not actual packet-selective recovery, FEC or secure transport.

Those gaps block a defensible choice of **one production algorithm, deployment
topology, parameter set or SLO claim**. They do not block recommending the
composition above or using the retained counterexamples to judge a concrete
proposal. The [catalog](CATALOG.md) preserves breadth, [findings](FINDINGS.md)
give the strongest numerical signals, and [source-bound evidence](evidence/20260926/README.md)
lets LEAD review exactly what each conclusion rests on.
