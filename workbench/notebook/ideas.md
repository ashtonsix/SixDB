# Ideas worth returning to

Loose questions, promising mechanisms, paper links, and surprises. A heading
and a few sentences can be enough, especially why something caught our
attention. Longer thoughts may get their own file. Notes can remain notes
indefinitely, inform several investigations, or acquire a link to a spike.

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

The [Calico overview](calico.md) is a cross-cutting map of earlier work.
Question-specific reading stays beside the question it informs.
