# Layout analysis as the design of information access

Research synthesis, 2026-09-14. This is a proposed direction for the spike, not an
Engine module specification. The [source dossier](prior-art.md) assembles the
mechanisms behind the argument; the [worked investigation](case-study.md) carries
them through concrete candidates.

The analyser should choose **stored representations together with programs for
using and maintaining them**. A layout determines which information can be
obtained cheaply, independently, or only after something else has been decoded.
An operation program determines which of those possibilities it uses. The useful
unit of comparison is their combination over a workload and a lifetime.

This gives the earlier work a common purpose. A filter postpones exact values;
a string prefix postpones its tail; a patch stream postpones uncommon bits;
a predictive residual depends on other fields; a directory discovers addresses;
a TuplePack packet arranges decoded information for the next computation.
Each changes the information that must become available, its dependencies, or
the cost of making it available. Plane splitting is one way to create these
choices. Packing, placement, instruction selection and scheduling realize them.

A plausible result is a small family of useful representations and operation
programs, with bounded local fitting and cheap execution selection. Expensive
analysis discovers which structural possibilities deserve that budget. Advice
can improve as telemetry accumulates while old data retains its representation.
The research question is whether this account can produce better decisions at
reasonable analysis cost than fixing storage first and optimizing its accessors.

## 1. Begin with the information needed to finish an operation

Consider a selective string predicate followed by a projection. A compact tag
may prove that a row cannot match. An ordered prefix may reject it or sometimes
settle an ordering comparison. Exact equality can still require length and tail
bytes. A row whose predicate is settled may need other fields for projection.
A known-row projection can bypass the filter entirely. The same stored image
therefore has several legitimate access programs.

[Row-filter-signatures](../row-filter-signatures/design.md) supplies a precise
foundation. Evidence maintains a lower bound L on rows certainly satisfying a
predicate and an upper bound U on rows possibly satisfying it. More evidence is
needed for U minus L. For `(A AND B) OR C`, exact evidence for C can finish some
rows without A or B; a possible match for C cannot. Boolean structure and the
kind of evidence matter before any bytes are placed. SQL NULL semantics must be
preserved rather than treating absence of TRUE as FALSE.

This also explains why field affinity is insufficient. Co-locating A and B has
value when it avoids work that remains necessary *after earlier evidence*.
That value depends on the predicate, output projection, distributions and chosen
program. It is not an intrinsic benefit attached to the field pair. A workload
supplies these demands; an objective values the resulting latency, throughput,
space and maintenance. Asking macro analysis for a universal co-location bonus
would make it predict an interaction that the local analysis should investigate.

The distinction survives aggregation. A rollup that says “some row has A” and
another that says “some row has B” cannot establish that any row has both. If
row 0 satisfies only A and row 1 only B, the marginal summaries cannot reject
`A AND B`; an adequate joint summary can. Replacing a joint summary with separate
marginals can lose this evidence even when every exact row value is preserved. Placement alone does not
change that: co-located marginals remain marginal, and a joint feature remains
joint in its own plane. Price the evidence encoding and its placement separately.

Conversely, surviving few rows does not imply touching few later groups. Under
an IID unresolved-row model, a g-row group is needed with probability
`1 - (1-p)^g`. Clustering changes that relationship. The source studies therefore
contribute three different distributions: what facts settle a row, which rows
still need which information, and how those rows occupy physical groups. A
single selectivity number cannot stand in for all three.

## 2. Follow the dependencies through representation and execution

Use an explicit small graph or candidate description before inventing a general
intermediate language. It should distinguish these relationships:

| Relationship | Example | Consequence of changing it |
| --- | --- | --- |
| Evidence | A prefix or tag bounds possible string matches | Changes which exact work can be omitted; duplicating evidence adds maintenance |
| Reconstruction | A value is a base plus residual, or a common value plus patch | Changes bytes and computation needed for an exact projection |
| Address discovery | A tail locator, rank or preceding block lengths locates payload | Introduces work even when that metadata is not part of the result |
| Execution | Several decoded fields feed a grouped packet and consumer | Changes instructions, live state, fusion and available overlap |
| Maintenance | A logical change invalidates a tag, rollup, predictor or offset | Adds repair work and owner obligations beyond the requested field |

These are not interchangeable edges in a pure expression DAG. An update has
ordering and publication constraints; a read may retain a cursor or decoded
state; alternatives can share work. The graph is an explanatory and experimental
tool, not a claim that a conventional shortest-path extractor will optimize it.

The distinction between a **feature**, its **encoding**, its **plane**, and an
execution **stage** comes directly from the filtering research. Add physical
placement and decoded packet arrangement without collapsing those distinctions.
An exact high byte might already exist in SeriesPack's head plane; moving it
need not create new information. A dense duplicate of it is a micro-index with
storage and repair costs. A plane can be placed next to another plane in the
same allocation. A stage can process several planes. A packet group can bring
decoded fields together without changing any of their stored planes.

Current Ikea makes the last distinction tangible. In the
[TuplePack grouped example](../../../ikea/examples/tuplepack/groups.cpp), fields
A8/B4/C4 occupy two stored bytes. A four-row read of ABC with groups `{2,1}` places
four decoded AB pairs together before C. A GPR consumer updates the four 12-bit
AB values, and an AB-only writer preserves C. The stored image did not change;
the read map, packet arrangement and write map are different choices. This is
already a concrete form of adaptation through rebinding. It is a capability,
not a guaranteed optimization: the current public comparison found grouping
slower for its ordinary GPR consumer, despite benefits in some other paths.

The [Bec256 composition study](../bec256-composition/metadata-findings.md) exposes
a complementary dependency. Population metadata can settle empty/full blocks,
but lengths also locate later bodies. An inactive preceding block's body can be
omitted while its length remains necessary. A retained prefix frame and a fresh
point entry have different costs over the same directory bytes. Folding a
population count can make its interpretation depend on the length. The analyser
must follow the resulting access program, not price a metadata plane in isolation.

Predictive-PFOR adds another form: a predicted field needs its residual and raw
reference fields. Its selected design restricts chains and reference fanout,
which also bounds reconstruction and repair. Compressing two fields together
may help storage and full projections while hurting an isolated point read.
The common question is what the operation must acquire and keep live to finish.

## 3. Search representations and programs together, at more than one scale

The proposed macro/micro division remains useful as a division of scope:

- Workload analysis considers maintained facts, micro-indices, region boundaries
  and objectives across reads and writes. It supplies required behavior, workload
  scenarios, admissible alternatives and constraints.
- Local analysis realizes those alternatives using nested data units, codecs,
  plane composition, widths, capacity, placement and executable operations. It
  supplies complete costs or a frontier, and can return a counterproposal that
  requires a wider choice.

It should not be a one-way pass in which macro permanently chooses every plane
and micro only packs it. Moving a prefix byte changes refinement. Increasing
bucket capacity changes overflow work. Changing a refinement group changes the
value of separating evidence. These decisions cross that neat boundary. The
problem owner determines which semantic and maintenance changes are permitted;
the search keeps the permitted alternatives open until their consequences are
priced. Authored workloads and objectives let us investigate this now, while
production of those inputs from Engine telemetry remains undefined.

A candidate should be compositional. A hash bucket can contain SeriesPack
fingerprints, a TuplePack entry array and a metadata tuple; a metadata field can
itself require a directory interpretation. Each child contributes an actual
representation law and supported operations. Their composition supplies shared
origins, address rules and interpretation. Flattening this to a bag of field
widths loses both feasibility and useful structure.

Candidate transformations have different purposes:

| Transformation | Why propose it? | What must be carried along? |
| --- | --- | --- |
| Reuse, materialize or re-encode evidence | Reject or certify work earlier; retain useful joint facts | Soundness, correlation retained or lost, conditional demand, duplicated bytes and repair |
| Split or merge information planes | Omit groups or share access to the stored facts | Group occupancy, reconstruction, summary dependencies and placement |
| Change encoding or common/exception boundary | Reduce common-path footprint or reconstruction | Actual codec geometry, escape frequency, update fanout |
| Change capacity, origin, stride or padding | Alter overlap, density and overflow | Allocation rounding, start phases, occupancy and address rules |
| Change maps, groups, carrier or traversal state | Fit the consumer without necessarily moving bytes | Issued spans, preserving writes, retained state and execution boundary |

This is a connected candidate space: not every combination is legal, and legal
choices have interacting costs. Small exhaustive cases can reveal useful
transformations and bad pruning rules. Retaining equivalent alternatives, as
in [Cranelift's e-graph work](https://github.com/bytecodealliance/rfcs/blob/main/accepted/cranelift-egraph.md),
is a search precedent. It does not provide the missing reconstruction, effects,
shared-work and contextual-cost algebra. An explicit finite graph is sufficient
until we have evidence that its representation or search order is the obstacle.

The immediate output should be a few explained alternatives: this one omits more
later groups, that one removes a dependent address step, another reduces repair
or footprint. A scalar winner can follow from a supplied objective; unresolved
trade-offs should remain visible. This is more informative than an unexplained
score attached to a dense packing.

## 4. Price the resulting operation in its operating conditions

Hardware fitting connects logical demand to execution through **readiness and
resource use**. Which addresses are known from the request? Which become known
only after a miss or decode? What independent requests or useful computation can
overlap them? Which cache, translation, bandwidth and ownership resources does
the whole operation consume? Loom's available execution conditions influence the
answer, but do not grant it permission to change stored representations.

A split 32B extension whose address is arithmetic from the row ID is different
from a variable tail whose locator is first fetched from the core. Our spatial
consumer measures the former. Adjacent placement might repay extra traffic at
one concurrency and access mix and lose at another. Demand-line geometry is
calculable; normalized latency savings from a spatial diagnostic are not cache
hit probabilities or a reusable discount on whole-record time.

The right role for geometry is candidate generation and explanation. The
[original slot calculation](examples.md#geometry-baseline) identifies boundary
opportunities. The current SeriesPack placement study is a useful counterweight:
centering a 12-bit residual improves one-line incidence, yet did not earn a wire
change in the measured consumers. A geometrically attractive candidate must
survive its reconstruction, writes and complete use.

Cost evidence therefore attaches to a representation/program combination and
its operating context. Keep exact feasibility, exact footprint, demand calculated
for a trace, sampled distribution estimates, measured consumer time and modeled
future behavior distinct. Hardware diagnostics guide comparisons; complete
consumer measurements resolve the important remaining interactions. They need
not identify the prefetcher's internal mechanism to support a conditional choice.

A usable objective can minimize complete-operation time under storage and update
limits, or return a space/time frontier over several workload scenarios. Do not
sum isolated microkernel timings as though their composition were measured:
the retained TuplePack mixed traces are a direct counterexample. Preparation,
shared decoding, branch behavior, execution boundaries and live state can change
the composed result. Counts of necessary work can be additive without wall time
being additive.

Over a stated lifetime, charge acquisition, local fitting, binding, execution and
any transition at their actual event frequencies. Charge retained preparation
once; distinguish persistent bytes from scratch and overlapping migration images.
Use explicit resource limits or a declared conversion between units. Workload
order can affect maintenance and adaptation value even when aggregate invocation
counts match. Unsupplied publication, durability or interference costs can remain
parameters in an authored scenario; they cannot silently become zero-cost facts.

## 5. Learn useful possibilities; fit where their parameters matter

Training and selection are a separate axis from macro and micro. Both scales can
perform expensive discovery and cheap local decisions. The reusable result need
not be a list of fully specified byte layouts. A family such as “compact exact
core, independently readable prefix, sparse exceptions” has locally meaningful
parameters: common widths, prefix length, exception organization, group grain
and operation choices.

The important predictive-PFOR precedent is that historical evidence ranks
**structural hypotheses**, while a new container fits parameters and can decline.
Pooled correlation does not establish usefulness within containers. Likewise,
a collection-wide layout winner can conceal opposing key regions. The target
for training is which possibilities tend to repay investigation, with coverage
and uncertainty, not one supposedly universal width or hardware coefficient.

Reuse different knowledge at different scopes:

| Knowledge | What can reuse it? | What can force reconsideration? |
| --- | --- | --- |
| Semantic transformations and reconstruction laws | Compatible schemas and operations | Changed semantics, codec law or maintenance permission |
| Candidate families and evidence of local usefulness | Related containers or known key regions | Different distributions, demand or missing coverage |
| Geometry and admissibility | Matching resolved layouts and placements | Changed sizes, strides, maps or grants |
| Conditional consumer costs | Demonstrated recipe and operating regime | New consumer, dependency path, hardware or concurrency regime |
| A concrete local fit and binding | Its stored image and compatible operations | New image, invalidated binding assumptions or changed objective |

Local selection checks cheap exact constraints, estimates its demand regime and
tries a bounded set of promising families. Local fitting adjusts their parameters
and prices complete alternatives. Useful statistics include conditional evidence
outcomes, unresolved-group occupancy, joint patch frequency, string lengths,
access ordering and entry mode, not just marginal widths and frequencies.
The fit retains a valid fallback and can decline when uncertainty or overhead
outweighs the possible benefit.

The first offline palette study supports candidate diversity and local pricing,
but it reused rows and captures: it did not validate collection-level learning.
The next test needs independently generated or captured containers, known key
regions and later workload windows. Its reference is the best admitted complete
candidate for each held-out case. Report discovery and local decision cost along
with regret. Exactly pricing bytes, as PFOR does, is not equivalent to exactly
knowing future execution time.

## 6. Improve decisions without requiring historical re-encoding

There are at least four useful actions: keep an image and its binding; rebind the
same image; choose a different representation for newly encoded data; migrate an
existing image. They spend different resources and have different lifetimes.
Grouped TuplePack operations provide a present-day example of the second action.
A better advice revision does not entail the fourth.

Stored descriptors must retain the actual interpretation rather than a policy
name whose meaning changes. SeriesPack's resolved versioned descriptor provides
part of that mechanism; compound identities, mappings and dependencies also
belong to the owning data structure. Advice, logical schema, representation,
coordinate mapping and visible data version are distinct. An old reader must
remain able to interpret its image while a new region adopts a different fit.

Read dependencies also have maintenance consequences. Carry three sets through
a candidate: **logical destinations**, **issued byte effects**, and **dependent
facts or structures requiring repair**. A TuplePack update can preserve bits
outside its logical map while issuing wider stores. A string change can invalidate
a prefix and tag. A moved row can alter rollup membership without changing its
logical value. Stable labels can reduce reference repair while a packed payload
still shifts, as trie-remapping demonstrated.

A migration comparison consequently includes reconstruction, locator and summary
repair, source/destination overlap, rebinding and the owner's publication costs.
Mixed historical layouts introduce dispatch and possibly grouping work during
execution. Reordering by layout is useful only where semantics permit it and
its benefit pays for that work. We can study these choices with explicit owner
assumptions without selecting Engine's transaction or residency machinery.

The direction to test is thus an adaptive design process with a slow-changing
family of stored images and a faster-changing choice of ways to use them. Its
success would be demonstrated by a connected problem where jointly choosing
evidence, representation and operation improves the result, a small learned
family transfers to new regions, and useful adaptation remains possible while
historical bytes stay put. The [next investigation](experiments.md) is organized
to distinguish those claims.
