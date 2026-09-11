# Current composition design

This is the maintained design direction from the completed basic-data-structure
probes and subsequent SeriesPack work, consolidated through 2026-09-11. Those
probes support implementing Ikea's basic parts. The [SeriesPack introduction](../../../ikea/seriespack.md)
explains the implemented component; its [extension guide](../../../ikea/seriespack/extending.md)
maps current implementation work. The [semantic and integration seams](semantics-and-integration.md)
remain open without blocking that work. Concrete APIs
develop with their callers; the
[owned probes](README.md#evidence-by-question) supply evidence and limits, not
class hierarchies or templates to copy wholesale.

## Organising principles

- Preserve logical work across manual fusion, inlining and CPS. Keep ISA and
  microarchitecture choices explicit inside native bodies. Shared algorithmic
  code should serve different wrappers and composition contexts.
- Separate physical format and placement, semantic domain and coordinates,
  native grain, observation points and scheduling boundaries. A block can have
  several customers and several appropriate kernels.
- Keep kernel-author obligations local. Composition supplies dependencies;
  admission and binding establish applicable resources and implementations;
  drivers own traversal, lifetime and specified integration duties.
- Make erasure, materialisation and retention costs explicit. Neither cursor
  syntax nor a native handoff guarantees that other live values stay in registers.
- Preserve the meaning and validity of evidence. Equivalence, implementation
  applicability and empirical cost/quality are distinct kinds of knowledge.

## Authoring model

Use a function over supported operations to author an ordinary composition.
An optional named component publishes children and delegates to that function.
Use declarations where they add knowledge: primitive semantics, an opaque implementation's applicability,
or a conditional equivalence. Do not require a separate hand-written expansion
for a composition the recorder already exposes. The recording vocabulary is
explicit: it does not trace arbitrary C++ or abstract native instructions.

The native author supplies operands, validity assumptions, allowed accesses
and effects, and the result's meaning. Composition describes dependencies;
binding matches implementations, carriers and resource facts; a driver owns
the chosen traversal and integration duties. Exact driver boundaries remain
open, especially when a call must expose progress or suspend.

Illustrative pseudocode, with unresolved read/mask contracts deliberately visible:

```cpp
// source.values is a symbolic child during recording and an attached operand
// during execution; it does not perform a schema lookup in the hot body.
auto select(Ops& ops, Source source, Rows rows, Mask active, Predicate p) {
    auto values = ops.read(source.values, rows, active);
    auto keep = ops.test(values, active, p);
    return ops.project(values, keep);
}

// Optional named packaging: publish children and reuse the same expansion.
component ValuesContainer {
    child<ValueSource> values;
    expose Select(rows, active, p) = select(ops, children, rows, active, p);
}

// Additional knowledge, supplied only for this specialised alternative.
implementation Select(fragment)
    body selected_native_select
    implements record(select, fragment)
    when layout_and_target_requirements;
```

`read` would have to resolve transform dependencies; `active` is not permission
to omit every inactive input. The reused `values` local is intentional. The integer probe demonstrates
bounded native reuse; general lifetime allocation remains open.
The declaration identifies a semantic obligation; it does not prove that a
hand-written native implementation satisfies it.

Three views should stay distinct, with explicit links between them:

| View | What it represents |
| --- | --- |
| Actual representation | Nested physical components, placement, parameters and auxiliary dependencies belonging to a segment's visible bytes. Engine owns the schema; Ikea supplies component contracts. |
| Operation composition | Logical values, dependencies, masks, effects and any requested observations. It can be recorded from authoring code and rewritten over the actual representation. |
| Selected execution | Implementations, carriers, reusable stages or compiled regions, plus a driver and attached resources. It implements the selected work under admitted conditions. |

A physical child replacement constructs a candidate representation and may
require migration. A plan rewrite changes execution over existing bytes.
Neither operation changes the actual representation of sibling segments.
Preferred future representation and selected executable revision are separate
facts. These remain requirements from the brief. Independently bound
heterogeneous operands demonstrate part of the separation; Engine's schema
discovery and migration mechanisms remain open.

## Authoring and interface responsibilities

These are starting responsibilities, not a mandatory registration framework.

| Authoring task | Direction to carry forward | Evidence or remaining question |
| --- | --- | --- |
| Compose existing operations | Write one recordable function; optional named packaging delegates to it. Keep semantic descriptions independent of native headers. | [Shared authoring](probes/ikea-blocks/composition/README.md); arbitrary C++ tracing is not implied. |
| Add a native kernel | Supply an explicit target body, operand/result meaning, applicability, admitted accesses and effects. Associate compatible wrappers with the same logical work. | [ABI controls](probes/ikea-blocks/abi/README.md); exact wrapper/carrier families remain contextual. |
| Attach data and select execution | Admit a source once, keep its owner and validity facts live in the enclosing invocation, then bind an operation and invoke it cheaply. Bind each operand's representation and reader independently. | [Masked algebra](probes/ikea-heterogeneous/operations/README.md) implements this split; SeriesPack views/endpoints borrow storage and do not themselves retain an owner. The original range probe still revalidates on preparation. |
| Substitute a reader or region | Preserve results, coordinates, effects and required observations. Establish extra implementation requirements in the binding or an explicit adapter. | Packed metadata readers vary over unchanged bytes; general nested discovery and rewrite admission remain open. |
| Choose a compilation boundary | Keep useful combinations inline inside curated regions; share independently compiled work where the tradeoff pays. Do not equate semantic nodes with calls. | [Granularity investigation](operation-granularity.md); majority-CPS build economics across many recipes remain unmeasured. |
| Add a physical child or transform | Expose its physical description, placement and dependencies separately from operation expansion. Re-establish enclosing guarantees after replacement. | [Body/tail composition](probes/ikea-integers/composition/README.md), [parent locality witness](probes/ikea-heterogeneous/locality.md); deeper transformed and variable-width compositions remain open. |

For discovery, distinguish formats/views and placements, operation contracts and
native implementations, container children and expansions, binding/lowering,
and integration drivers. Blocks naturally permit free-standing operations;
containers expose contracts over possible realisations. This is a responsibility
map, not a selected production directory hierarchy.

## Curated regions, carriers and grain

Cursor authoring and selected compiled regions work together: the cursor locates
work and retains unused state; the region determines where compilation stops.
The heterogeneous probe factors unary metadata choices from six heavy
source/operator kernels and retains selected inline combinations as controls.
This supports reducing useful specialisation products without demanding a
separate continuation after every operation.

Keep the three intended execution buckets: manual fusion for the critical few,
inlining when source maintenance matters, and CPS for the large majority where
build time and binary size matter. The probes demonstrate native handoff and
selected region sharing; they do not establish a universal CPS ABI or its
many-recipe economics. A whole-graph mapping is appropriate for an explicitly
selected compiled region, not as the general mechanism for arbitrary pipelines.

Erase binding metadata independently of live computational values. Compatible
immediate native handoffs can avoid wide aggregate return traffic. A stored
chunk or frame can also be an intentional boundary with a real lifetime need.
The masked-algebra frame resolver materialises 64 bytes per retained source;
it does not provide a free native iterator. Inline execution can itself spill.
See [boundary evidence](probes/ikea-heterogeneous/notes/operations-boundaries.md).

Native grain follows the selected implementation and the consumer's semantics.
Two BEC decoder inputs may be two operands at one ordinal, yielding one output
slice, or adjacent slices from one source. Logical progress must retain that
identity. On the measured V2 comparison, reducing output grain reduced text and
stack pressure but lost throughput in the affected dense/mixed cases. The
[grain comparison](probes/ikea-heterogeneous/operations/measurements.md#native-grain-the-smaller-kernel-loses-throughput-here)
supports the local default, not a universal grain or a rule based on spill count.

SeriesPack makes further choices concrete: physical packet size, native working
width, output-store width and reduction finalization need not move together.
[Grouping and deferred sums](native-regions/spectrum.md) improve
the same authored consumer without changing its representation; selected
[short output stores](../seriespack-range-execution/stores.md) remove large
losses while keeping the native working values. An admitted read window can
also exceed the selected logical output, provided a
[partial sink](native-regions/partial-materialization.md) preserves original
coordinates and exact writes. These are contextual implementation choices,
not reasons to specialize the semantic operation or introduce a second graph.

The [BEC length-child substitution](../bec-packed-metadata/findings.md) puts a
current SeriesPack native region inside an existing metadata/body consumer.
Admission retains the owner, child placement and true logical length; the
enclosing cursor supplies required checkpoint predecessors and retains native
metadata lanes across body operations. On Zen this broadly preserves the specialized
provider's whole-count performance and modestly improves on ordinary
materialization, with larger gains at the partial logical tail. The much larger
isolated refill gain reinforces measuring the enclosing operation. This is
evidence for composing a known-geometry child; schema-driven discovery and
arbitrary nested rewrites remain open.

The [shared head-projection experiment](../seriespack-head-projection/findings.md) raises
the corresponding output question. Its shared pass improves dense encodes but
regresses some independently strided head placements. Producing an owned group
once while each child sink maps that group through its own placement is a
promising response; it has not been measured. Sharing computation need not
require all consumers to adopt the same contiguous traversal region.

Driver boundaries also have to preserve facts already established about the
work. The [ordinary-reader integration](../seriespack-range-execution/integration.md)
improves many shifted reads but shows how combining edge traversal and complete
runs can add register preservation and return-side work to previously cheap
calls, even without vector spills. Separating those paths recovers much of the
complete-request loss; tiny suffixes remain costly, and generated Local edge
code still tests larger regions that the caller has already excluded. Making
those facts useful to shared expression evaluation remains an implementation
question. Compare ordinary calls and code/build cost when selecting a boundary;
a fast body or a diagnostic with stronger admissions does not settle it.

## Performance and maintenance

Repeatedly good inner kernels becoming expensive ordinary operations directs
attention to the shared boundary and traversal. Prefer a simple driver for a
whole admitted family, removing general machinery that its hot calls do not
need. Establish useful facts at admission or binding and preserve them through
shared native bodies and expression evaluation. This should reduce the context
a kernel author must manage while preserving arbitrary legal ranges, independent
child placement, exact output and effect obligations. The
[ordinary-reader experiments](../seriespack-range-execution/integration.md)
show why more dispatch paths and good isolated bodies are insufficient.

The retained [decode](call-boundaries/decoder.md) and
[encode](call-boundaries/encoder.md) callback changes demonstrate
a useful separation: checked facades keep capacity and validation obligations,
while trusted endpoints receive only the arguments their work consumes. Removing
unused capacity from those boundaries eliminates caller aggregate copies across
whole operation families without changing the wire or the ordinary facade.
The measured effect varies by caller and machine; that evidence supports the
simplification without assigning every timing change to an additive ABI cost.

The [checked-point layout comparison](../executable-placement/evidence/checked-point-layout-20260911/summary.md)
also shows that unchanged kernel and timed caller instructions can have different
costs after relinking. Restoring their earlier placement removes much of one
control regression while preserving the simpler checked call's benefit. Cost
evidence therefore belongs to the selected executable and caller context;
semantic equivalence can remain valid when a cost observation needs revisiting.

Assess primitive cost together with useful compound consumers and maintenance
cost. A modest residual primitive gap can be acceptable when native composition
recovers more than it costs. Ashton's example of roughly 40% is an illustration,
not a universal cutoff, a performance target or evidence of that recovery.
Compare equivalent compound work with the strongest practical alternative,
including materialization where useful; count traversal, masks, joins, output
and required effects. The [current all-profile composition review](native-regions/evidence/delivery-composition-20260911/review.md)
finds 48 wins and 96 losses for basic tile-authored compare-and-sum against
reused-scratch materialization. Selecting the fastest registered native
grain/carrier alternative gives 110 wins and 34 losses; it is measured selection,
not an automatic planner or a complete search. Zen Local12 still loses 1.791×
with its best registered AVX512 alternative. The authored function boundary
adds no material penalty in these cases, but that does not establish a good
execution strategy. Keep materialization available, and preserve independent
choices of physical geometry, execution grain and result finalization. Recovery
for other consumers remains a hypothesis until measured.

Avoidable boundary or traversal overhead remains worth removing, especially
when the change simplifies the implementation. Large unexplained gaps warrant
bounded investigations with a concrete question about the shared mechanism.
Record the remaining gap and what a probe establishes; when there is no
compelling shared fix, redirect work toward useful consumers instead of making
primitive parity a prerequisite for development. Keep source, generated code
and build costs alongside runtime evidence when selecting what to retain.

## Obligations at the seams

| Fact | Responsibility and consequence |
| --- | --- |
| Representation and resource validity | Source admission checks actual owner association, framing and readable extents. Construction/caller context supplies intended meaning and immutable snapshots; a const pointer alone cannot prove them. |
| Selection and logical coordinates | The mask names original positions on all operands. A sparse selection does not compact them or permit omission of required predecessor metadata. A coarse candidate bit is not exact row truth. |
| Access and placement | Logical encoded bytes, an implementation's readable window and its physical footprint differ. Legal children do not automatically meet the enclosing locality promise. |
| Output and effects | Complete masked output and selected-only writes have different postconditions. Clearing and retained state belong to the driver that promises them. Logical return length is not complete physical write coverage. |
| Lifetime | The enclosing invocation keeps admitted owners live; a binding may borrow them. SeriesPack expressions additionally borrow named view objects. Values needed by another immediate consumer or across another producer must remain valid; suspension or delayed use may require explicit storage. |
| Observation and equivalence | Substitution must preserve required progress/evidence as well as final values. A different estimator, including sampled versus full evaluation, is not an equality rewrite merely because it serves the same policy question. |

The [masked operation contract](probes/ikea-heterogeneous/operations/README.md#selection-identity-and-output-obligations)
shows these obligations concretely. Actual byte coverage for MVCC and logical
summary deltas remain separate integration outputs; the probes have not
implemented those consumers.

Storage analysis is likewise compositional: a child body estimate acquires
metadata, readable suffix and allocation costs at its enclosing layout. The
[whole-window analyser](probes/ikea-heterogeneous/operations/analyser-findings.md)
keeps model/scan identity and byte scope explicit. Estimated savings are neither
safe capacity nor an exact savings guarantee, and byte preference alone does
not establish conversion or query-time payoff.

## Working vocabulary

| Data unit | Meaning |
| --- | --- |
| Block | Composable primitive with a defined physical layout; often tileable or hierarchically composable. |
| Plane | Repeating pattern of blocks, uniform or ragged; repetition does not imply physical contiguity. |
| Container | Contract exposing operations and getters/setters over possible physical realisations. |
| Segment | Engine data-structure node with at most `2^16` local positions, bundling record/summary containers. This bounds neither its logical key interval nor its byte size. |
| Partition | Loom-owned storage unit holding contiguous bytes from multiple blocks. |
| Record / tuple / struct | Related logical/physical groupings; distinctions remain to be worked through with row/column and variable-width callers. A record-slice is proposed as a view over selected records in a segment. |

For code, use operation for requested work, kernel for a named computational
contract, implementation/body for its realisation, stage for an occurrence in a
composition, binding for admitted operands and implementation choices, adapter
for a particular conversion, and driver for traversal and execution lifecycle.
A stage, call, observation and suspension need not coincide. These names do not
impose a common ownership hierarchy.

## Open interfaces

The following remain design choices to develop with actual callers, rather than
a required sequence of new probes:

- Deep child/dependency discovery, serialised composition descriptions and
  admission of nested substitutions; separate per-segment actual bytes,
  candidate migration and execution-only rewrites throughout.
- Progressive evidence with predicate identity, coordinate/granularity mapping,
  validity and residual obligations; adopting improved Engine plans at safe
  progress boundaries without analysis in every hot call.
- General carrier allocation and delayed value reuse, compatible CPS families,
  and build/text scaling across many recipes. The
  [value-reuse sketches](value-reuse-sketches.md) and
  [granularity alternatives](operation-granularity.md) retain useful comparisons.
- Mutation, summary/MVCC effects and publication; Loom buffer acquisition,
  prefetch scheduling, cancellation and suspension with live state. The
  [new design proposal](semantics-and-integration.md) develops semantic ownership,
  local effects and selected suspension boundaries, with alternatives and
  counterexamples. Only the segment position bound is newly committed; the
  proposed interfaces are not established by the earlier block probes.

The [original brief](brief.md), [earlier sketches](sketches-2.md) and
[probe reviews](predictor-review.md) preserve how the direction changed.
Probe-specific measurements and limitations stay with the probes; the
[heterogeneous closeout](probes/ikea-heterogeneous/closing.md) is its dated
campaign assessment, while this page owns the current composition model.
