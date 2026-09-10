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

## Plans that keep improving

Ashton's proposed Engine model retains query plans and equivalence graphs
long-term, accumulating improvements rather than only caching an executable.
For a large scan, periodic probes could change the plan used for remaining
work during the query. The [Ikea composition discussion](../spikes/ikea-composition/sketches.md#clarification-analysis-and-execution-surfaces)
draws out the interface consequence: deep inspection and substitution during
analysis must coexist with a small bound call during execution. Semantic
equivalence, applicability and conditional cost evidence have different
validity rules. This is a design direction, not an implemented optimiser.

The [Bec256 exercise](../spikes/ikea-composition/probes/ikea-blocks/README.md#hardware-findings-2026-09-09)
adds a concrete constraint: native register handoff can coexist with inspectable
composition, but dispatch and carrier allocation have observable costs. Its
limited lowerer does not yet handle a value consumed twice; this is distinct
from branching control flow. The [value-reuse sketches](../spikes/ikea-composition/value-reuse-sketches.md)
keep that authoring and execution question open rather than turning the first
successful codec measurements into a production interface.

The [consolidated Ikea design](../spikes/ikea-composition/design.md) carries
forward a shared composition mechanism with optional named structure and
explicit implementation/equivalence knowledge. Ashton's next packed-integer
probe brings reconstruction toward the open questions of progressive filtering
and nested substitution, while preserving different actual representations
across segments. Its [12-bit exercise](../spikes/ikea-composition/probes/ikea-integers/composition/README.md)
reuses one decoded native value for both filtering and summation. A third parent
placement using the same tail makes the separation between child format and
parent placement concrete. The [locality audit](../spikes/ikea-composition/probes/ikea-integers/locality/README.md)
adds a composition obligation: child-local reads do not imply a local parent
read, and reachable tile residues belong to the placement contract. The probe's
same-wire [reader comparison](../spikes/ikea-composition/probes/ikea-integers/measurements.md#two-readers-over-the-continuous-wire)
also favours different implementations for small resident data and large
independent-read traces; locality legality does not rank execution choices. Its
explicit carrier and fixed expansion mapping do not yet establish a general
lowerer or progressive-filter interface.

The [integer review on 2026-09-10](../spikes/ikea-composition/probes/ikea-integers/composition/README.md#direction-after-review-2026-09-10)
sharpens the factorisation target: useful inlined combinations inside coarser
substitutable regions, rather than a continuation boundary at every operation.
Internal native grains should meet directly where compatible, with adaptation
pushed outward. Block reflection may also name semantic obligations such as
sorted unique appends; dynamic evidence and mutation effects still need their
own concrete contracts. These are steering questions, not selected interfaces.

The [heterogeneous bitset/metadata exercise](../spikes/ikea-composition/probes/ikea-heterogeneous/README.md)
now tests those boundaries with dependent addresses: a requested range can need
predecessor lengths that are not themselves requested bitsets. Direct offsets
and packed lengths share the same immutable BEC bodies, while native metadata
frames outlive individual BEC pair calls. This makes region sharing, retained
state and ownership obligations concrete without selecting a universal cursor.

Its [masked algebra extension](../spikes/ikea-composition/probes/ikea-heterogeneous/operations/README.md)
adds two independently configured sources: unary metadata strategies can bind
outside a curated decoder/Boolean region without multiplying every layout pair.
That cut has a real materialised-frame cost. Two BEC decoder inputs can also be
the two operands at one ordinal, rather than two output ordinals; physical grain
does not determine logical progress. Whole-window size prediction separately
exercises how an enclosing layout's overhead changes a child's byte estimate
into a useful, still fallible decision.

Ashton [closed that exercise](../spikes/ikea-composition/probes/ikea-heterogeneous/closing.md) on
2026-09-10: the probes now give Ikea enough direction for implementation of its
basic parts. The closing assessment carries the concrete composition and
obligation choices forward; further integration questions are not prerequisites.
