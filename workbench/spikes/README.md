# Investigations

Start with the [research notebook](../notebook/README.md) to find related work
by question. This catalog covers the surviving studies, grouped by what they
help explain. Their findings and evidence remain useful after implementation
moves to a module or an experiment concludes; the rows are not a list of active
projects.

Current APIs belong in module guides. Read a study's findings before its old
plan or launcher, and keep the measured operation, units, source epoch and
operating conditions attached to its numbers. [Retired investigations](../notebook/retired-spikes.md)
locates removed source trees and verified recovery.

## Indexes, routing and maintenance

[Cross-study reading route](../notebook/indexes-and-maintenance.md) — logical versus
physical order, duplicate keys, summary maintenance and complete index consumers.

| Study | What remains useful |
| --- | --- |
| [Segment directory](segment-directory/README.md) | Graduated into `engine::directory`. Evidence for ordered leaves at every layer and a deepening root, against flat roots, natural partitions and inline or index hash pages, on counters, flat-image and Orbital timings. |
| [Trie remapping](trie-remapping/README.md) | Concluded comparison of natural regions, ordered gaps and block order; column movement and conversion remain consequential. |
| [Aggregate maintenance](aggregate-maintenance/README.md) | Count/sum coalescing, construction costs, dirty buffers and no-filter controls. Connects directly to directory pending aggregates. |
| [Row-filter signatures](row-filter-signatures/README.md) | Concluded resident scan and semantic models; useful regions for block summaries and expensive predicates, with direct-column negatives. |
| [Regexp lowering](regexp-lowering/README.md) | Concluded exact/necessary lowering, survivor bounds and factored filters over real corpora. Logical counts are not timing gains. |

## Packed representations and planning

[Cross-study reading route](../notebook/representations-and-planning.md) — current
author capabilities, mixed-workload layout choices, filtering, conversion and plans.

| Study | What remains useful |
| --- | --- |
| [Tuple layout](tuple-layout/README.md) | Point access, scans, checked mutation and layout search behind current TuplePack; analyser work remains experimental. |
| [Layout analyser](layout-analyser/README.md) | Connected objectives, metadata/plane/placement alternatives, shortlists and worked consumer investigations. |
| [StreamPack](streampack/README.md) | Variable-byte geometry and sidecars; comparative evidence plus production Ikea/Loom/Orbital consumers. |
| [Intervals](intervals/README.md) | Maintained Intervals graduation across StreamPack, Directory and Frame; external filters, sparse checkpoints and complete consumer qualification. |
| [Intervals search](intervals-search/README.md) | The shared native search step: 64-byte span count, exact short arrays, readable-window grant; varying-count mispredicts; group run buffers and run-start bitmap against per-block search. |
| [Boundlist](boundlist/README.md) | Retired implementation; historical geometry/ISA portfolio and exact recovery. Maintained geometry is Intervals; compact ownership is StreamPack. |
| [Bec256 composition](bec256-composition/README.md) | Complete analysis/encoding consumers, prepared windows and alternatives to isolated refill comparisons. |
| [NEON Local transpose](neon-local-transpose/README.md) | Promoted select-form and four-tile Local residual transposes: 1.5–2.9× NEON pure-tail reads, 1.1–1.8× writes; four-tile mixed reads 1.1–1.7× (k9–16); `bulk_arm` re-measured and kept. |
| [NEON sparse TuplePack packets](neon-tuplepack-masked/README.md) | Promoted window load for half-selected 32/64-row NEON packets: one-byte sums 2.6–3.3×, trusted updates 1.8–2.2×; scatter side unchanged. |
| [Packed filters](packed-filters/README.md) | Direct packed comparisons, native controls and early exits; production contracts in Ikea. |
| [Filter composition](filter-composition/README.md) | Fused/shared/CPS alternatives, compiler expansion and runtime costs across AVX2, AVX-512 and NEON. |
| [Ikea composition](ikea-composition/README.md) | Question map into integer/bitset/heterogeneous probes, ABI, grain, value reuse and SeriesPack history. |
| [Ikea Frame, phase 2](frame-phase-2/README.md) | Reopened Frame research: [fused and composed stages](frame-phase-2/STAGES.md), [mechanisms and tiered targets](frame-phase-2/MECHANISMS.md), [proposed partition](frame-phase-2/PARTITION.md). |
| [Ikea Frame, phase 1](frame-phase-1/README.md) | Closed. SeriesPack/TuplePack container research: [stocktake and remaining map](frame-phase-1/INVENTORY.md), real integer programs, selective scans, multi-field execution and mutation/ownership costs; no maintained Frame API yet. |

## Hashtable choices and frontiers

[Maintained benchmark question map](../benchmarks/hashtable/README.md#find-the-result-that-answers-your-question)
connects actual peers, memory budgets, construction, reads/writes and Loom consumers.
[Ikea usage](../../ikea/docs/hashtable/usage.md) owns the current contract.

| Study | What remains useful |
| --- | --- |
| [Owning multimap](hashtable/README.md) | Unpromoted frontier and four comparison panels; maintained set/map has moved to Ikea. |
| [K-way traversal](hashtable-merge/README.md) | Ordered traversal across sources and its independent costs. |
| [Wide values](hashtable-wide-values/README.md) | Promoted borrowed-inline reads plus the remaining slot-backed alternative; retain staged ID/payload follow-up as well as synchronous losses. |
| [Boost adoption assessment](hashtable-boost-adoption/README.md) | Dependency/footprint and ownership seams; memory-matched and mixed-hit diagnosis. No migration selected. |
| [Predictability specialization](hashtable-primary-branchless/README.md) | Promoted predictable/unpredictable getters and caller-owned Auto; uniform branchlessness was rejected. |
| [Swiss overflow decisions](hashtable-swiss/README.md) | Completed tag/continuation, logical-group-width and ISA metadata-batch decisions; independent evidence, offline reports and recovery in one home. |

## Execution, code generation and hardware

[Cross-study reading route](../notebook/execution-and-codegen.md) — independent memory
work, native versus scheduled execution, service quanta, call friction and compile cost.

| Study | What remains useful |
| --- | --- |
| [Loom execution](loom-execution/README.md) | Direct/manual/Ring, worker channels, service gaps, affinity-crossing joins and bounded pressure; broader than the graduated Ring interface. |
| [Hardware hints](loom-hardware-policy/README.md) | Prototype evidence behind native partial-field learning and hint specialization. |
| [Locality and execution](loom-locality-execution/README.md) | Direct/Ring selection, charged sampling and contiguous demand; captured identity-bearing API is historical. |
| [Call boundaries](call-boundaries/README.md) | Real caller/callee ABI, owning returns, frames and later Ring-dispatch consumers. |
| [Assembly calls](assembly-calls/README.md) | Runnable assembly-to-Ikea/Loom calls, ownership handling and complete-operation review. |
| [Executable placement](executable-placement/README.md) | Unchanged instructions, relinking effects and byte-identical link recovery. |
| [Memory characterisation](memory-characterisation/README.md) | Demand/concurrency/hint evidence and corrected adjacency results; links actual PMU limits. |
| [Ikea CPU comparison](ikea-cpu-comparison/README.md) | October 2026 six-CPU snapshot and price model, rather than a current hardware recommendation. |
| [Ikea on Neoverse V3](ikea-neoverse-v3/README.md) | Focused native platform comparison with its captured source/conditions. |

## Allocation, Objects and local lifetimes

[Cross-study reading route](../notebook/memory-and-lifetimes.md) — placement, physical
slack, retained versions, shared interpretation and current complete consumers.

| Study | What remains useful |
| --- | --- |
| [Memory descriptors](memory-descriptors/README.md) | Address geometry, placement/packing models, grouping and virtual growth; distinct prototypes, not current allocator contracts. |
| [Allocation palettes](allocation-palettes/README.md) | Palette/class geometry, slack and complete allocation choices behind production work. |
| [Reuse and temperature](allocation-reuse-temperature/README.md) | Cheap reuse selection and temperature for cold-first trim; ranked queues and candidate caches lost. |
| [Live-tail allocation](allocation-live-tail/README.md) | Group reclamation, live tails and complete physical drain. |
| [Orbital hot paths](orbital-hot-paths/README.md) | Attributed allocate/release, cold growth, Lane fan-in and catalog lookup costs on Zen 5/Graviton, with patch variants and a forward look at the planned submodules. |
| [Object integration and MVCC history](orbital-mvcc/README.md) | Current production consequence checks, cross-module outcomes, metadata/ABI and historical performance negatives. Timing drivers are retired. |
| [Embedded views](orbital-views/README.md) | Earlier copy/COW/take, readiness, growth and intent comparisons; current interfaces live in Orbital. |
| [Local handoff](orbital-local-handoff/README.md) | Same-host thread/worker handoff and hardware calibration. |

## Durability and protocol models

[Orbital questions](../notebook/orbital-ideas.md) and its
[historical reference map](../notebook/orbital/reference/README.md) connect these
studies to current runtime and formal-model owners.

| Study | What remains useful |
| --- | --- |
| [CFT and durable commit](cft-commit-latency/README.md) | Independent network/persistence measurements, PLP and commit comparisons; simulator costs do not replace them. |
| [Reservation policies](orbital-reservation-policy/README.md) | Completed queue-rule alternatives, counterexamples and continuing-workload progress under modeled assumptions. |
| [Prepared-root fusion](orbital-root-fusion/README.md) | Eligible chosen-operation fusion, retained material and recovery checks; not generic root acquisition. |

## Work on an investigation

Before creating a study or substantially changing its approach, follow the
[prior-work instructions](../../AGENTS.md#read-prior-work-before-starting-an-experiment).
Apply the relevant findings to the starting point and make the new question explicit.

Keep one stable home named after the question. Code, literature, experiments and
findings can develop together; one README is enough to begin. A loose idea can
stay in the [notebook](../notebook/ideas.md). There is no required study template.

[Building and editor setup](BUILDING.md) gives the small CMake example and selected
target workflow. [Tools](../tools/README.md), [datasets](../datasets/README.md) and
[peers](../peers/README.md) supply reusable operating pieces. When work supersedes
an implementation, keep its useful findings and recovery with the successor
rather than maintaining a second live API.
