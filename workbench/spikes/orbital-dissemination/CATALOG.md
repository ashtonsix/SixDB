# Catalog of recorded dissemination ideas

2026-09-26. **223 source-linked entries**, with related entries cross-referenced
rather than presented as 223 independent inventions. This survey covers the
network/dissemination remit broadly: writes, reads, extensions, representation,
work placement, transport, economics, observation, delivery and recovery.
Orbital LEAD owns integration into the brief. An entry does not select a design.
The separate [provisional recommendation](RECOMMENDATION.md) states where the
evidence has converged and which choices should remain open.

The survey was expanded after missing the specific old idea that **edge-origin
broadcast can avoid the first expensive cloud-egress leg**. MINING is a useful
discovery guide, not an exhaustive record. The expanded reading followed original
drafts and archived sources, including inline questions and negative findings,
rather than treating the guide's summaries as the whole idea space.

Every entry has explicit exploration: the generalized question, when the idea
can help, and a limiting cost, counterexample or failure condition. Each also
distinguishes supporting evidence from unresolved work. Some have new executable
comparisons; some have existing local measurements or source inspection; others
have analytical exploration only. **Catalogued, explored and demonstrated are
different statements.** This is not a claim that every idea is implemented,
measured, valid in every composition, or ready for the brief.

## The catalog

| Entries | Owning catalog | What is retained |
| --- | --- | --- |
| U01–U26 | [Current user remit](SOURCE-SURVEY-REMIT.md#current-conversation-distinct-ideas-and-their-exploration) | Every substantive initial suggestion and correction, the generalization request, edge omission and health-guided proposal timing. |
| P01–P05, R01–R08 | [Placement vision and further source checks](SOURCE-SURVEY-REMIT.md#pitch-sources-and-what-they-add) | Multicloud placement/provisioning, derivation, sovereignty constraints, pruning, adaptive representations, additional scratch/design sources and inherited-contract traps. |
| N01–N80 | [Networking history](SOURCE-SURVEY-NETWORK.md) | Origin-dependent economics; regional/provider trunks; NAT, private links and blob/peer cost; transport, MTU/FEC/codecs, identities/security, trees and non-tree routing, partial observations, control, physical/semantic credits, measurement. |
| W01–W51 | [Reads, execution and representation](SOURCE-SURVEY-WORKLOADS.md) | Coherent coverage, within-shard serving, shared work, filtering, summaries, retained plans, layout/address dependencies, work grain, backpressure, cancellation, extension effects and complete HTAP/ELT histories. |
| D01–D40 | [Delivery and recovery](SOURCE-SURVEY-DELIVERY.md) | Identity and incarnation, receipt/effect ambiguity, joins, frontiers, detector symptoms, retained dependencies, handoff, independent recovery paths, SOS exports and PITR lineage. |
| A01–A13 | [Additional archive ideas](SOURCE-SURVEY-ARCHIVE.md) | Hierarchical shortlists, symbolic reduction, consumer-liveness cancellation, shrinking intermediates, executable/shared work schedules, steering, quiescent reclaim, live-state and error boundaries. |

The source inventories in those files say **which prose was read in full, which
implementation sections were inspected, and what was excluded**. Historical
claims and current code status are kept separate, including the documented
align-before-XOR implementation conflict, incomplete remote-fault/runtime paths,
leaf-only interchange and the retired optimizer's invalid durable-latency
comparison. A historical build plan or design's imperative prose is source
material, not a new instruction or an inherited contract.

## Source coverage and limits

The inspected corpus includes all five Orbital stale-draft files and MINING;
the current module/workbench guides; the SixDB notebook and all existing spike
entry points with relevant deeper research; the full authored 21-read/17-write
scenario catalog; all recovery S1–S26; original Consurgent v0.4/v0.5 designs and
v1 thesis/build narratives; the current pitch; and selected Calico architecture,
runtime, memory, wire, codec, scheduling and recovery sources. Additional scratch,
symbolic-algebra, Frame and ChainVM archives were read to check that a networking
keyword search had not hidden execution/dissemination ideas. The retired
network-propagation prose was inspected directly at Git commit `68b8733`.

This is a bounded, auditable survey of those sources, **not a claim to have
searched every private notebook, old conversation, repository revision, cited
paper or file on Ashton's computer**. Full source and raw benchmark archives
were not all rerun. Marketing/biographical/fundraising material, financial-system
mechanics and detailed unrelated ISA kernel algorithms are explicitly outside
the network remit; their network-facing ideas remain in the catalog. Named
source inventories and [source-manifest hashes](survey-source-manifest.json)
make the boundary inspectable.

## What the explorations reveal

The organizing problem is **jointly choosing work, representation, placement,
release, routing and retention with partial information**. A fixed graph of
unchanging messages hides too much. [Design space](DESIGN-SPACE.md) develops
the general formulation; these connections emerged across the sources:

- **Origin and result placement change the economic problem.** A cheap relay
  after cloud egress is different from starting there. Replies, metadata and
  repair have their own directions and tariffs. Fixed facilities and account
  tiers couple messages beyond an edge-weight sum.
- **Many sources and many receivers describe different obligations.** Equivalent
  copies, independent contributions, mandatory checking, selected execution and
  observer delivery need separate counts and evidence. Sender choice cannot
  settle ambiguous external effects.
- **Message enhancement is a family of legal transformations.** Selectors,
  projections, partial aggregates, code references, dictionaries and continuations
  can save or add work. Residency, version/cut, exact coverage and interpretation
  lifetimes determine whether the representation is useful.
- **Less logical work is not automatically less physical work.** Batching,
  coalescing, caches and filters must repay construction, metadata, reset, lookup
  and cancellation costs. Packet, storage, scheduling and publication grains can
  differ. One long active job can defeat queue priority.
- **Adaptation changes the work that comes next.** Delayed or false observations,
  shared bottlenecks, route-state distribution, old in-flight obligations and
  cold migration costs can erase the expected gain. A future strict proposal
  does not repair an earlier prefix hole.
- **Delivery survives route and host changes.** Joins, handoff, replay and rescue
  require discovery, retained bytes, interpreters, scoped receipts and authority.
  A warm cache or small signed digest is not automatically an independent,
  usable recovery recipe.

These are exploration results, not a roadmap or a requirement to combine every
historical mechanism. Several alternatives conflict or serve different workloads.

## Executable evidence and analytical boundaries

| Exploration | New evidence and limits |
| --- | --- |
| All cardinalities, writes, frontiers, read scale-out and joins | [Main findings](FINDINGS.md), 95 cases; source-bound simulator with finite resources and supplied semantic facts. |
| Early-entry holes and independent-producer interference | Four [prefix cases](evidence/20260926/prefix.json); [proposal timing](PROPOSAL-TIMING.md) adds observed adaptive policies, warnings, flapping and windows. |
| Optional/blocking enhancement and packet retries | Six [relay controls](evidence/20260926/relays.json), plus 40 [exact-selection comparisons](ENHANCEMENT.md). |
| Constrained route portfolios and equivalent senders | 34 [adaptation cases](ADAPTATION.md), including install cost, packet/byte overload, selection granularity, failure and lost notices. |
| Shared foreground, reads, extension output and cancellation | 12 [mixed cases](MIXED.md); foreground is a durable-notification proxy, not the brief's witness path. |
| Edge/cloud origin, providers, direct hashes, replies, capacity and failure | 40 [edge cases and explicit economic boundaries](EDGE.md); illustrative tariffs, not provider quotes. |
| MTU, packet work, repair and FEC | [Transport sensitivity](TRANSPORT.md); packet/resource simulations and separately labeled coding arithmetic, not a transport implementation. |
| Coherent cuts, effect ambiguity, membership and authority | Meaningful [checks](check.py) and existing source probes; trusted inputs in a probe do not constitute an implemented distributed protocol. |

The ledger deliberately keeps unresolved empirical questions visible: actual
transport and sandbox costs; measured same-core/cross-core/NUMA paths; continuous
graph/placement controllers; cross-query reuse; learned codecs and live upgrades;
correlated failure and recovery bootstrap; and full application/consensus
composition. They have analytical treatment and counterexamples here. Their
absence from executable coverage is not hidden behind a claim that “all ideas
were tested.” [Evidence provenance](evidence/20260926/README.md) gives exact
source hashes, cohort sizes, commands and limitations for what was run.
