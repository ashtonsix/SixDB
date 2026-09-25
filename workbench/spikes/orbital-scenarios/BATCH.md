# Component-owned provisional protection

2026-09-25. Follow-up to Ashton's coordinated-retry proposal. The useful change
is to make a provisional reservation belong to an arbitration component and its
generation, rather than the first individual requester to establish it. This
removes the ownership obstacle in the [three-transaction example](FINDINGS.md).
It does not, by itself, choose a distributed collection or verdict protocol.

Two policies and nearby choices are executable in the same workbench:

- `batch-reset` replaces all component reservations at every nonempty retry cut.
- `batch-hold` defers a new cut while a remote verdict is pending. Ordinary work
  outside reserved scopes and already-authorized C2 may continue.
- Between cuts, an optional fast lane retries requests with no current granted
  blocker and no excluding component fence. Eligibility is not a promise that
  competing retries in that epoch all succeed.
- An optional local solver removes the remote delay when every current claim
  and every member's coordinator is on the same shard.

These are model policies, not edits to the design brief.

## Keep three graph meanings separate

A graph with both directions for every conflict has the same SCCs as the
connected components of the underlying undirected graph: every undirected path
can be traversed both ways. That partition is a useful **arbitration scope**.
It is not a partition into actual deadlocks. See the definitions of
[connected components](https://networkx.org/documentation/stable/reference/algorithms/generated/networkx.algorithms.components.connected_components.html)
and [strong connectivity](https://networkx.org/documentation/stable/reference/algorithms/generated/networkx.algorithms.components.strongly_connected_components.html).

The model therefore keeps these distinct:

1. **Scope overlap:** which key ranges the component must reserve. Existing
   reservation semantics exclude even read/read overlap, so scope construction
   conservatively merges those overlaps too. If component reservations instead
   become mode-aware, that rule can be relaxed explicitly.
2. **Incompatibility:** which transactions cannot receive their current requested
   protections together. Read/read and same-transaction protection are compatible.
   This is the undirected graph used for subset selection.
3. **Directed waits:** which ready parts wait on which granted protections. These
   retain the distinction between a convoy, ordinary queued contention and a
   wait cycle. Pending arbitration fences are displayed separately, not counted
   as a granted-lock deadlock.

Merge by immutable transaction identity across shards before treating components
as independent. A shard can report {T1,T2} and another {T2,T3}; together these
are one decision scope. Parts and dependency generations must remain attached
to those transaction vertices: discarding one part can invalidate descendants
on another shard, and the costs are not just vertex counts.

## Executable interpretation and its limits

A retry cut considers **every ready part with a previous attempt**, ignoring
ordinary per-epoch capacity and retry backoff. Requests compatible with both the
retry cohort and existing grants execute immediately. Other requests fail and
contribute to arbitration. Existing grants are never cleared by a retry cut.

The collector merges all currently known ready/prepared claims needed to close
the component, including existing holders and untried ready parts. This prevents
selection from ignoring a holder just because it is not in the retry queue.
Each component reserves the union of its key scopes. No new acquisition inside
that scope is permitted until selection, except to the selected transactions
after the verdict. C2 uses existing grants and continues to drain.

Selection retains irrevocable C2 participants, then anchors the oldest compatible
transaction. Remaining compatible vertices are chosen by age or retained work.
`bounded` attempts maximum cardinality **subject to those anchors** only when
there are at most 18 optional vertices, with a 20,000-search-node budget and a
greedy incumbent. Otherwise it uses the greedy result. It does not solve a
minimum-discard problem or claim an unconstrained maximum independent set.
Maximum independent set is itself a hard optimization problem;
[NetworkX's algorithm documentation](https://networkx.org/documentation/stable/reference/algorithms/generated/networkx.algorithms.approximation.clique.maximum_independent_set.html)
describes that boundary. No NetworkX dependency was added here.

Only losing **granted parts that conflict with selected claims** are invalidated,
plus their transitive dependents. Losing queued work is simply deferred.
An ancestor and its descendant named in one verdict restart at the ancestor;
the descendant must be rediscovered. Generation/known-part checks reject stale
verdicts, and a participant that acquired C2 authority cannot be invalidated.
Component fences are released when their selected owners complete, or replaced
at an allowed subsequent cut.

The following are deliberate strong abstractions:

- A retry cut is an **atomic complete multishard snapshot**. The implementation
  walks known simulator state; it does not implement distributed entrainment,
  collection closure, version negotiation or merge/split ownership.
- `arbitration_us` is an aggregate collection/solve/return delay. All remote
  components captured together use that delay. Verdict application and dependent
  invalidation are atomic across shards. There is no partial delivery or failure.
- The cut is an additional abstract retry input between ordinary shard epochs.
  All retry attempts, claims, edges and search nodes are counted, but constructing
  the graph and processing the mass retry do **not consume simulated CPU time**.
  Faster/more frequent cuts therefore cannot establish a throughput prediction.
- `batch-hold` currently defers the next **global cut**, even for a newly appearing
  unrelated component. Per-component rolling rounds could avoid that coupling;
  this model does not establish their merge or authority rules.
- The oldest priority is authored scenario arrival metadata plus transaction ID.
  An implementation needs a stable agreed total priority, not synchronized wall
  clocks. Discovery is still an incrementally revealed, fully authored DAG.

## Observations

The retained [45-run comparison and nine graph probes](evidence/batch-comparison.json)
use synthetic time, finite inputs, one workload seed and no cloud resources.

**Reservation lifetime is necessary for the verdict to be useful.** On the
three-transaction fixture, with cuts every 500 µs and 1,200 µs arbitration:

| Reservation policy | Complete by 8,000 µs | Applied / stale verdicts | Discarded work |
| --- | ---: | ---: | ---: |
| Replace each cut | 0 / 3 | 0 / 13 | 0 |
| Keep the pending round | 3 / 3 | 3 / 0 | 1 |

Keeping the round completes at 4,900 µs. At 100 µs arbitration both policies
complete at 2,100 µs. The precise boundary at equal cut/verdict times depends on
the event tie rule; replacing a generation faster than it can return is the
substantive failure. A durable generation fence must outlive collection, solving,
return and application, or accept compatible refreshes without restarting it.

**Remote arbitration is unnecessary for some ordinary contention.** Forty local
writes to one key complete at 18,300 µs through the remote 1,200 µs path, versus
3,900 µs with entirely local components resolved in their shard. Both use
`batch-hold`, 500 µs cuts and the same fast-retry policy. No prepared work is
discarded. The original `older` baseline takes 7,200 µs, but also has different
retry backoff; it is not the isolated comparison for the local-solver effect.

**Components need not stay small.** In the 80-transaction hotspot workload,
the 1,200 µs held-round/age case reaches a 72-transaction component. It completes
16/80 by 20,000 µs; resetting reaches only 7/80. The held round repairs the
specific verdict-lifetime failure, not the overall throughput problem.
There are still 64 unfinished transactions in that held-round observation.

**An eager retry lane can discard more useful preparation.** At 100 µs
arbitration, held rounds and the work heuristic, disabling the lane completes
41/80 and discards 81 work units; enabling it completes 34/80 and discards 175.
For the age heuristic the corresponding counts are 37 versus 36 completions.
These are one-seed observations, not a general argument against early retries.
They show why "can acquire now" and "likely to finish without being invalidated"
are different admission criteria.

**Do not send an expanded clique unnecessarily.** For 256 writers on one key,
there are 65,280 directed conflict arcs, but only 256 transaction-to-key claims.
A chain of the same size has one connected component too, yet the anchored
greedy solver selects 128 compatible vertices. A star with its center as the
oldest anchor selects only that center. Scope size alone does not determine
solver difficulty or useful parallelism. The implementation expands adjacency
locally for this bounded spike; incidence counts are not measured wire bytes.

## Design direction suggested by the experiment

Retain component-owned provisional protection, with a stable generation during
arbitration. Keep frequent ordinary retries separate from less frequent
coordinated conflict resolution. Resolve a component locally when its authority
and claims are local; carry key/object incidences, directed blockers, dependency
generations, C2 state and relevant work weights into distributed arbitration.

Start selection with a deterministic feasible answer and a progress anchor;
spend bounded extra work improving it. Optimizing every large component exactly
is unnecessary for testing the mechanism. The anchor's persistence across retries
matters, and its cost is visible on the star example. Fairness under continuous
arrivals and dynamic cross-component discovery remains unproved.

Before treating this as a distributed protocol, model how the arbitrator knows
the component is closed across all entrained shards, how concurrent components
merge, where the verdict becomes authoritative, and how new arrivals wait for a
later generation without repeatedly extending the current one. Late verdicts
must be rejected or safely rebased; authorizing C2 must exclude invalidation.
Those are next modeling questions, not demonstrated errors in the brief.

## Reproduce

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_batch.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/study_batch.py \
  --output build/orbital-scenarios/batch-comparison.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py \
  --preset reservation --policy batch-hold \
  --output build/orbital-scenarios/batch-replay.json
```

The browser exposes the two policies, cut period, arbitration delay, selection
heuristic, early retry lane and local solver. Dotted timeline lines mark retry
cuts; the component status and trace show selections, invalidations and stale
verdicts. Existing replay/save paths use the same engine. The original 28 trace
digests in `evidence/comparison.json` are still checked unchanged; that evidence
describes the initial sources in Git commit `4b509ee`.
