# Ideas worth returning to

Loose questions, promising mechanisms, paper links, and surprises. A heading
and a few sentences can be enough, especially why something caught our
attention. Longer thoughts may get their own file. Notes can remain notes
indefinitely, inform several investigations, or acquire a link to a spike.

## Networking around the consensus obligation

The [consensus-networking note](consensus-networking.md) replaces the retired
synthetic routing spike. Revisit routing, propagation/write overlap, scheduling
and repair once the consensus and durability obligations are clearer. It keeps
useful counterexamples and archive recovery pointers; the measured CFT/PLP work
remains an independent source of evidence.

## Sketches, filters, and histograms have different jobs

Ashton's [secondary-summary question](secondary-summaries.md) is deliberately
separate from hierarchical `min/max/count/sum` maintenance. Proving absence,
estimating selectivity, and returning an approximate answer impose different
contracts. This distinction should survive any temptation to share an update
mechanism. The note preserves the initial questions and reading; the meaning
of “min-sketches” is still open.

## When less logical work costs more CPU

The [aggregate-maintenance findings](../spikes/aggregate-maintenance/FINDINGS.md)
changed the initial intuition: coalescing removed 71% of higher-stratum
adjustments under uniform writes, yet constructing location-addressed runs
made the measured cycle about 24 times as costly as updating resident arrays.
The sort/coalesce/expand/allocation path is an attribution target, not a proven
explanation. This may connect to other batch-built representations: charge
their construction and temporary state alongside the downstream work saved.
Physical write cost and retained history could change the balance here.

Ashton's [shared buffer and dirty-prefix filter](../spikes/aggregate-maintenance/shared-dirty-buffer.md)
responds by keeping ingestion closer to an append. Its useful distinction is
idempotent dirtiness: a shallow mark can become read-only for the rest of an
epoch, unlike an exact counter. That makes repeated writes versus first
transitions a useful distinction when thinking about shared metadata elsewhere.
The [first measurements](../spikes/aggregate-maintenance/dirty-buffer/FINDINGS.md)
support that distinction for hot locations, but also find a cheaper buffer
without a filter. Clean-read avoidance has to repay both marking and reset;
the right amount of metadata can depend on the workload rather than being
intrinsic to the buffered representation.

## Determinism constrains legal regrouping

Disabling fast-math does not make floating-point addition associative. Delta
coalescing, parallel reductions, and replay can choose different groupings.
The [aggregate design note](../spikes/aggregate-maintenance/design.md#separate-the-aggregate-semantics)
keeps this unresolved and links ReproBLAS as a possible reference. The broader
question is which numeric contracts let several execution and maintenance
orders produce the intended result, and what that costs.

The [Ikea semantic/integration proposal](../spikes/ikea-composition/semantics-and-integration.md)
connects this to binding: value domains, operation laws and physical encodings
are different descriptions. An order transform can justify an encoded-domain
comparison without justifying arithmetic there. Mutation composition likewise
needs both physical byte coverage and semantic consequences under the selected
numeric rules.

The [Calico overview](calico.md) is a cross-cutting map of earlier work.
Question-specific reading stays beside the question it informs.

## Plans that keep improving

Ashton's proposed Engine model retains query plans and equivalence graphs
long-term, accumulating improvements rather than only caching an executable.
For a large scan, periodic probes could change the plan used for remaining
work during the query. The [Ikea composition discussion](../spikes/ikea-composition/sketches.md#clarification-analysis-and-execution-surfaces)
draws out the interface consequence: deep inspection and substitution during
analysis must coexist with a small bound call during execution. Semantic
equivalence, applicability and conditional cost evidence have different
validity rules. This is a design direction, not an implemented optimiser.

The [composition design](../spikes/ikea-composition/design.md) and
[integration proposal](../spikes/ikea-composition/semantics-and-integration.md)
make three reusable questions concrete: where useful compiled regions should
begin/end; how semantic laws, borrowed resources and mutation effects survive
substitution; and which evidence remains valid when a retained plan changes.
Their probes and chronology live with the investigation.

The [SeriesPack findings](../spikes/ikea-composition/native-regions/findings.md)
add a useful separation: physical tile size, execution grain, working lanes and
result representation can vary independently. An authored and direct body can
match while both lose to materialization. Revisit these choices when a new
consumer changes the useful work, rather than deriving them from storage geometry.

The [checked-point layout experiment](../spikes/executable-placement/evidence/checked-point-layout-20260911/summary.md)
adds an executable-context qualification: relinking moved the cost of unchanged
kernel and caller instructions, and restoring code placement recovered much of
that movement. A retained plan's semantic equivalences and its empirical cost
evidence can therefore need different invalidation rules.


## Retain enough to change one link input

Two SeriesPack tasks independently spent time reconstructing unchanged callers
and Google Benchmark archives after an idle worker expired. Source and binary
hashes let the [layout follow-up](../spikes/executable-placement/layout-recovery.md)
recover byte-identical baseline relinks, but retaining selected link inputs
could have avoided that work. An opt-in helper could collect one executable's
objects, archives, response files and link command into its existing artifact
bundle. Its useful boundary is reproducible relinking of that executable;
physical source transforms and measurement interpretation remain with the study.
This is a tooling opportunity, not a reason to retain every build directory.
