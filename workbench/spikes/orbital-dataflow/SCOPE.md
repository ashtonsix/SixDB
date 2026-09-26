# Scope and framing

2026-09-26. This is broad design exploration before detailed interfaces. Orbital
LEAD owns brief integration; the spike proposes choices and records counterexamples.
The current [brief](../../../orbital/BRIEF.md) supplies the working application
boundary and transaction rules, not a requirement to implement a new runtime.

The useful unit is an application-defined computation with dependencies,
required coverage, state and observable effects. A static map/shuffle/reduce DAG
is one case. Conditional requests, changing fanout, feedback, long-lived change
processing and durable application workflows also matter. Even an acyclic
operator graph can have cyclic waits through retained bytes or publication.

## What to explore

| Question | Comparands and material counterexamples |
| --- | --- |
| How should work and bytes meet? | Colocation, gather, broadcast, repartition, semijoin and code/request shipping; charge lookup/build state, repeated execution and both traffic directions. A replicated dimension or hot key can reverse the winner. |
| Where should execution boundaries fall? | Fused local regions, bounded channels, shared intermediates, materialized exchange and durable continuation. Operator, task, packet, transaction and checkpoint boundaries need not coincide. |
| What justifies combination or pruning? | Exact bag/set laws, stable ordering, floating-point contracts, top-k bounds and approximate evidence. Missing contributions, NULL, duplicates or deletes can invalidate a plausible optimization. |
| How do feedback and continuing change finish useful work? | Application-defined iterations/fixpoints and bounded epochs/windows; corrections, negation, deletes and external observations. Quiet queues and a transport ACK cannot supply semantic completeness. |
| How does parallel work remain one coherent result? | One retained snapshot across late source discovery, exact logical coverage across replicas, deterministic contribution identities, complete effect envelopes and transaction-scoped verification. |
| How does finite capacity affect the graph? | Build/probe skew, slow consumers, output expansion, replay retention, checkpoint IO, cancellation and control work. A memory-safe queue can still prevent any completion. |
| What survives failure or adaptation? | Recompute from retained inputs, replay a materialized boundary or resume explicit state; distinguish duplicate transport, duplicate execution and duplicate effects. Repartitioning needs state and outstanding-work ownership. |
| Which applications warrant the machinery? | HTAP/ELT joins and refreshes, CDC, recursive queries/graphs, shared feature/inference work, application callbacks and externally visible backend actions. Simple point operations should retain a cheap direct path. |

## Ownership and independent kinds of progress

Engine defines SQL meaning, query plans, logical identities and the atomic
publication unit. A non-database application supplies its corresponding meaning.
Loom places/schedules physical work and manages resident resources. Orbital
provides opaque transport, durable objects, thread/IO lifecycle and the agreed
ordering mechanisms described in the brief. An operator implementation need not
become an Orbital concept, and a kernel stage need not become a Loom task.

Keep application iteration time, transaction position `c`, shard epoch and
delivery progress distinct. A recursive read can take several execution rounds
and shard epochs while observing one `c`. Application completeness, successful
execution, verification, recoverable decision, externally visible publication
and retirement are separate facts. The eventual interface may combine some
facts where an application contract proves equivalence; it must not assume it.

Current transactions can discover additional sources at their fixed position,
subject to complete read dependencies and retained versions. They cannot add
published effect authorities outside their declared envelope. Dynamically
allocated private shuffle bytes or execution partitions are not automatically
new application effects. Internal handlers join the parent transaction and
cannot require its publication to produce an input to that publication.
Checked native execution currently has a shard-local source/effect boundary;
general distributed checked-native expansion is outside that selected lane.

Page residency and logical access coverage are also distinct: a page-fault
mechanism sees first touch, not every later load. The application projection or
declared access context must make the exposed neighboring bytes legitimate.
The OS utility exploration owns the mapping and access-control mechanisms.

## What the probes can establish

Use exact finite inputs and independent semantic oracles for transformations,
authored failure histories for invariants and deterministic finite-resource
models for costs and stalls. Retain rejected, blocked and unfinished work as
outcomes. Synthetic time and price coefficients are explanatory inputs, not
measured performance. Reuse the dissemination study for transport/routing;
there is no reason to reimplement a whole network simulator here.

The desired outcome is a small recommended composition and explicit reasons
to choose alternatives. A universal graph language, scheduler ABI, automatic
optimizer, high-fidelity OS simulator or new transaction protocol is not needed
to answer these questions. Any such choice requires stronger evidence later.
