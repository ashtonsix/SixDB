# Representations and planning

[Ikea's source guide](../../ikea/docs/source.md) and
[consumer guide](../../ikea/docs/consumers.md) are the starting points for current
capabilities. Research below explains choices and alternatives; captured spike
formats and proposed analysers are not additional production contracts.

## Match the representation to useful work

| Question | Route through the evidence |
| --- | --- |
| What should Frame expose for computation over encoded data? | [Frame's architectural review with Engine](../spikes/ikea-frame/REVIEW.md): retained structure, selected residual algebra, schema fidelity, cross-column alternatives and mutation lifecycles; research directions, not an implemented module |
| Row layout, point access, mixed reads/writes and scan batching | [Tuple-layout viability](../spikes/tuple-layout/viability.md), [implementation decisions](../spikes/tuple-layout/implementation-decisions.md), then [current TuplePack](../../ikea/docs/tuplepack/usage.md); NEON sparse packets in [NEON sparse TuplePack packets](../spikes/neon-tuplepack-masked/README.md) |
| Planes, projections, metadata and placement selected together | [Layout-analyser findings](../spikes/layout-analyser/findings.md), [connected case study](../spikes/layout-analyser/case-study.md), [executable reference](../spikes/layout-analyser/tuplepack-reference/README.md) |
| Variable bytes, offsets, sidecars and mutation | [StreamPack recommendation](../spikes/streampack/RECOMMENDATION.md), [results](../spikes/streampack/RESULTS.md), [current usage](../../ikea/docs/streampack/usage.md) |
| Shared interval geometry across value storage, routing and encoded frames | [Maintained Intervals](../../ikea/docs/intervals/usage.md) and [graduation](../spikes/intervals/GRADUATION.md): StreamPack owns compact retirement; sparse spacing is independent of execution grain |
| Ordered boundaries versus value storage | [Boundlist recommendation](../spikes/boundlist/RECOMMENDATION.md), [ownership](../spikes/boundlist/OWNERSHIP.md), [consumer portfolio](../spikes/boundlist/PORTFOLIO.md) |
| NEON cost of Local bit-plane residuals | [NEON Local transpose](../spikes/neon-local-transpose/README.md), including its [preset re-measure](../spikes/neon-local-transpose/README.md#preset-follow-up) (keep `bulk_arm`) and [mixed reads](../spikes/neon-local-transpose/README.md#mixed-local-reads) |
| Predictive coding and complete analysis/encoding work | [Bec256 composition](../spikes/bec256-composition/README.md), [residual-bound questions](pfor-bounds.md) |

Physical tile size, execution grain, working lanes and result representation
can vary independently. [SeriesPack history](../spikes/ikea-composition/seriespack-history.md)
retains the cases where a direct authored body matched its control yet both lost
to materialization. A layout analyser must charge complete mixed workloads;
isolated kernel costs can choose the wrong layout.

## Filter before decoding, or decode and consume?

[Packed-filter results](../spikes/packed-filters/RESULTS.md) separate dependent
latency from independent throughput. Their
[early-exit qualification](../spikes/packed-filters/EARLY-GRADUATION.md) separates
stopping work from layout and fusion. [Filter composition](../spikes/filter-composition/README.md)
contains the compiler/runtime tradeoffs, corrected throughput traces and ISA
differences behind the maintained [filter contract](../../ikea/docs/filters.md).

For strings and general Boolean expressions, read
[regexp conclusions](../spikes/regexp-lowering/CONCLUSIONS.md) beside
[row-signature findings](../spikes/row-filter-signatures/FINDINGS.md).
Avoided RE2 calls or Boolean evaluations are counts, not measured speedups.
Cheap resident columns can favor direct evaluation while expensive text or
joint block summaries justify an extra filter. The
[secondary-summary note](secondary-summaries.md) separates pruning from estimation
and approximate answers.

[Frame's complete head consumers](../spikes/ikea-frame/filter/FINDINGS.md) compare
logical H8/H16, residual heads, exact block bounds and quantised window bounds
with actual native/Polars controls. Logical H16 helps predicted Jena queries but
adds two bytes/row; RegionID and the losing window-bound program rule out a
universal evidence layout. [Point integration](../spikes/ikea-frame/point/INTEGRATION.md)
preserves native-prefix gains while retaining the substantial native-array gap.

## Conversion, append growth and changing interpretation

[Object integration outcomes](../spikes/orbital-mvcc/OUTCOMES.md) connect actual
SeriesPack/TuplePack conversion, old readers and changed Part counts to current
Object checks. The [performance evidence map](../spikes/orbital-mvcc/performance/README.md)
locates append-copy grain, shared-prefix construction and conversion capacity
findings. Charge construction, simultaneous source/target ownership, later
consumption and retirement; a smaller target body alone does not price conversion.

Keep shared immutable geometry separate from per-realization counts and backing.
The [composed Shape study](../spikes/orbital-mvcc/metadata/composed/README.md)
includes equal byte extents with unequal logical counts. Its
[Schema construction negative](../spikes/orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
saved resident metadata but made repeated tiny Tuple construction roughly nine
times slower. Shrinking metadata is useful only when it serves performance and
sensible ownership. [Memory and lifetimes](memory-and-lifetimes.md) covers the
related physical accounting.

## Plans that keep improving

Ashton's proposed Engine model retains plans and equivalence graphs, accumulating
improvements rather than only caching an executable. Periodic probes could
change how the remaining work of a large scan runs. The
[analysis/execution discussion](../spikes/ikea-composition/sketches.md#clarification-analysis-and-execution-surfaces)
and [composition design](../spikes/ikea-composition/design.md) explore the split
between deep inspection during analysis and a small bound execution call.
This remains a design direction, not an implemented optimiser.

[Value semantics and integration](../spikes/ikea-composition/semantics-and-integration.md)
keeps semantic laws, resource borrowing and mutation consequences visible across
substitution. An encoded-domain comparison does not justify arithmetic there.
Floating-point addition can change with regrouping even without fast-math;
the [aggregate semantics note](../spikes/aggregate-maintenance/design.md#separate-the-aggregate-semantics)
retains the numeric question and ReproBLAS lead.

Empirical cost evidence has different validity rules from semantic equivalence.
[Executable-placement findings](../spikes/executable-placement/README.md) show
that relinking unchanged instructions can change their measured cost.
[Execution and code generation](execution-and-codegen.md) connects those effects
to composition boundaries, code size and compiler resources.
