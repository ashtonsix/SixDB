# Prior art and evidence boundaries

Reading and integration notes, 2026-09-13. Local observations are tied to their
captured consumers and environments. External mechanisms are candidate ideas;
their optimality or performance claims do not automatically transfer to SixDB.

## TuplePack search and actual operations

The [bounded reference](tuplepack-reference/README.md) and
[detailed search reading](tuplepack-search.md) move here from the TuplePack
investigation, with Ikea's ownership handoff. They are continuing layout-analysis
research. The original runtime, reporting code and immutable evidence remain in
[tuple-layout](../tuple-layout/README.md).

The reference enumerates 2,816 legal two-/three-byte placements of widths
`[1,7,3,5]`, preserving byte order, bit edges and gaps. Seven ordered read/write
operations expose density, co-access, extraction and preservation trade-offs.
It can select a structurally diverse 28-layout subset, ingest context-bound
costs, choose recipes under a reuse horizon, and calculate conditional migration
break-even. Its 12 checks establish this finite enumeration and arithmetic;
they establish no general CPU predictor or production search space.

The retained [V2 mixed-plan evidence](../tuple-layout/evidence/mixed-v2/provenance.json)
compares the same 56 plans (28 layouts × two uniform recipe families). At 50%
writes the isolated-cost choice measured 5.102 ns/invocation versus a subset best
of 3.087; at 90%, 6.131 versus 3.718—about 65% regret. At 5% writes, regret was
about 0.3%. This is a concrete counterexample to treating isolated timings as a
complete mixed-plan chooser, not an estimate for today's kernels on every host.
The arbitrary per-operation hybrid recipes admitted by the Python tool were
not what this particular measurement exhaustively tested.

An offline replay while opening this spike also checked the later
[final-v2 capture](../tuple-layout/evidence/final-v2/provenance.json): observed
subset regret was 0.09%, 44.50% and 14.09% at 5%, 50% and 90% writes respectively,
again over 56 plans. Keep this later capture separate from `mixed-v2`; the
qualitative counterexample survives with different magnitudes. Replaying reports
is not a new timing run, and observed near-ties imply no statistical separation.

The [placement scan](../tuple-layout/viability.md) compares a 16B projection in
its own dense plane against the same projection in 64B rows. It demonstrates
the importance of placement under full scans; inactive-row correctness checks
do not supply a sparse-scan performance curve. The detailed search note proposes
larger 48/60B layouts, padding, and four 12B planes in a 64-row tile.

Current [TuplePack reference](../../../ikea/docs/tuplepack/reference.md) and
[owner integration](../../../ikea/docs/integration.md) are stronger sources for
capability than historical prototype prose. Ordered maps, preserving neighbors,
actual issued spans, logical destinations and maintenance reads differ. Physical
unit, decoded packet, occupied row extent and stride are separate dimensions.

## SeriesPack and filtering planes

[SeriesPack representation](../../../ikea/docs/seriespack/representation.md)
separates optional highest-byte heads from the remaining payload. Origins and
tile strides permit independent or interleaved placement. The
[expression leaves](../../../ikea/include/ikea/seriespack/author/expression.h)
can address individual pieces; full values need their reconstruction dependencies.
Local eight-row tiles and striped residuals have different whole-tile occupancy.
The `compact`/bulk presets are starting policies, and recovery uses the actual
versioned descriptor. Cacheline tile spacing rounds strides, not allocation bases.

The [head-projection study](../seriespack-head-projection/README.md) and
[reader experiment](../seriespack-head-projection/reader/README.md) investigate
these sources and placements. They do not supply an automatic choice of which
evidence should be separated. [Composition granularity](../ikea-composition/operation-granularity.md)
also distinguishes the information evaluated together from the native execution
grain: a larger logical Boolean expression need not materialize every intermediate.

[Row-filter-signatures](../row-filter-signatures/README.md) is especially relevant:
one feature/encoding/plane/stage distinction, equal-budget `1×32`, `2×16`, `4×8`
planes, Boolean composition, and progressive refinement. Certain matches and
possible matches define an unresolved set; surviving rows and rows needing more
evidence are not interchangeable. Boolean grouping determines whether a plane
can settle a clause. Nonempty later groups matter even at low row selectivity.

Its [findings](../row-filter-signatures/FINDINGS.md) include a case where skipping
34.9% of second-stage 16-row groups still cost 0.937 ns/row versus 0.684 for eager
evaluation. Separate planar rollups also beat interleaved placement with the same
information and candidate counts in their measured case. These were warmed,
pinned local ARM Linux VM experiments; logical bytes counted are not measured
DRAM traffic. A cheap exact-predicate bypass remains an essential control.

## Three-array and related Calico evidence

[Three-array](../../../../calico/workbench/prototypes/three-array/README.md)
separates a 4B fingerprint/flags filter, an exact 48/64B hot core and cold payload/
patches. Known-row access bypasses the filter. Its
[report](../../../../calico/workbench/prototypes/three-array/REPORT.md) uses
monolithic controls with and without the same fingerprint information, showing
that much selective-scan benefit comes from the compact independently readable
filter stream. Cold full-point access can lose to a monolithic layout; 48→64B
padding had no consistent benefit on that local 128B-line machine.

This is evidence for separating successive information needs, and for comparing
costs at fixed results. The synthetic string fingerprint, simple distributions
and absent production mutation/transaction machinery limit transfer. Keep the
Calico implementation and evidence there. The
[point-layout study](../../../../calico/workbench/prototypes/point-layout/REPORT.md)
adds update trade-offs to read clustering.

## Predictive-PFOR: training is not selection

Calico's [collection/container design](../../../../calico/workbench/prototypes/predictive-pfor/DESIGN.md)
asks whether trying a relationship tends to help an independently selected
container. Pooled row correlation is not that question: correlated container
means may hide locally independent residuals, while useful local relationships
can have different parameters. Expensive historical analysis folds local fitting
outcomes into reusable hypotheses; local fitting can decline a suggested relation.

The later [selected experiment](../../../../calico/workbench/prototypes/predictive-pfor/selection/SPEC.md)
narrows the algorithm: historical utility ranks field/reference/form hypotheses;
parameters are fitted anew locally. Up to four attempts use bounded training and
validation blocks, followed by exact full-container pricing including metadata.
Its selected numeric form has no reference chains. Do not accidentally import
the earlier prototype's six-attempt default, slope seeds or depth-two graph as
the later selected design.

The transferable mechanism is **reusable structural hypotheses plus bounded local
fit and independently priced acceptance**. The [selection report](../../../../calico/workbench/prototypes/predictive-pfor/selection/REPORT.md)
separates acquisition from fitting and reports access costs as well as bytes.
A compression win does not imply an isolated point-read win. Its immutable prior,
schema/revision checks, history/cold paths and explicit refresh uncertainty are
useful precedents, not a selected layout palette or automatic drift policy.

For this spike, exact representation size can often be priced at selection time;
future mixed execution cost cannot be verified exactly in the same way. That
difference limits how directly the PFOR acceptance rule transfers.

## Hardware and historical representation evidence

[Memory-characterisation](../memory-characterisation/README.md) retains
hardware geometry, random independent-chain, ordered-stream and spatial-fetch
diagnostics. Its [method](../memory-characterisation/METHOD.md),
[findings](../memory-characterisation/FINDINGS.md) and
[lookup](../memory-characterisation/lookup.json) own qualification. Zen adjacency
samples motivate 96/128B consumer comparisons; they time a particular target load
after a particular demand/history and delay. Normalized cold/trained/hot savings
are neither hit probabilities nor a 128B coherence guarantee. Local ARM VM line
geometry differs from the EC2 64B-line cases. MLP and ordered-stream thresholds
are different experiments and are not hardware queue-size discoveries.

Loom retains responsibility for these diagnostics. This spike owns derived
consumer models and sends contradictory hardware interpretations back to that
source. Missing or incompatible evidence leaves alternatives open.

[Trie-remapping findings](../trie-remapping/FINDINGS.md) show stable labels
reducing reference repair while packed-rank columns can still move; gaps must
reach payload placement to remove those shifts. The resident model excluded
publication, retained readers, WAL and replication. The
[maintenance review](../tuple-layout/maintenance-review.md) and current Ikea
integration guide identify summary and physical-effect obligations to carry
into cost scenarios without selecting a transaction/allocator API.

## External mechanisms

The [transferred reading note](tuplepack-search.md#useful-prior-mechanisms-with-their-limits)
owns the detailed HYRISE/PAX/H2O/ByteStore/HyPer discussion. These primary sources
were revisited while opening this spike; the proposed applications below are
our inferences, not claims made by those papers about SixDB.

| Source | Mechanism relevant here | Transfer limit / experiment |
| --- | --- | --- |
| [HYRISE — Grund et al., 2010](https://www.vldb.org/pvldb/vol4/p105-grund.pdf) | Workload-driven vertical groups and geometry-aware cache-cost search | Additive/order-invariant partition assumptions do not justify pruning TuplePack code/recipe alternatives; use as a generator and measure regret |
| [PAX — Ailamaki et al., 2001](https://www.vldb.org/conf/2001/P169.pdf) | Attribute minipages inside a bounded page, with ordinal reconstruction | A precedent for plane placement, not a choice of Engine page or SIMD grain |
| [H2O — Alagiannis et al., 2014](https://stratos.seas.harvard.edu/sites/g/files/omnuum4611/files/stratos/files/h2o.pdf) | Joint layout/access adaptation, specialized operators, transformation costs | Rebinding versus migrating bytes and heterogeneous historical layouts need their own SixDB accounting |
| [ByteStore — Zhang et al., 2022](https://arxiv.org/abs/2209.00220) | Byte-oriented representations and empirical scan profiles for an advisor | A scan-weighting assumption is not a full read/write workload; include updates and complete consumers |
| [Automatic Physical Design Tuning: Workload as a Sequence — Agrawal et al., 2006](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/SequenceTuning_Sig06.pdf) | Query/update order can change which indexes/materialized structures repay construction and maintenance | Supports a future macro objective and transition scenarios; its SQL-server structures/model do not supply Engine micro costs |
| [Cranelift e-graph RFC — Bytecode Alliance, 2022](https://github.com/bytecodealliance/rfcs/blob/main/accepted/cranelift-egraph.md) | Retain equivalent expressions so cooperating rewrites need not commit through a fixed pass order | Test explicit alternatives first; representation changes require reconstruction, effects and context-aware shared-cost accounting |
| [Swiss Tables design notes — Abseil](https://abseil.io/about/design/swisstables) | Separate compact metadata filters candidates before exact key equality | Motivates fingerprint/entry dependencies; its seven-bit H2/control encoding and probing rules are not the proposed rope-bucket format |

Further reading should answer a live experiment: how to select a representative
palette, extract with shared/contextual costs, or value region-specific physical
design over time. A broad bibliography alone will not resolve those questions.
