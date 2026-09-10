# Composition design after the first executable probe

**Current implementation direction, 2026-09-10.** The integer and heterogeneous
exercises have now supplied enough evidence to implement Ikea's basic parts.
The [closing assessment](../ikea-heterogeneous/closing.md) consolidates the
decisions: shared operation authoring, explicit native kernels, independently
admitted and bound inputs, and curated compilation regions. Carry coordinate,
dependency, lifetime and effect obligations into the interfaces; choose concrete
carriers, grains and policies for each use. Exact production APIs and Engine
integration remain work to do, not prerequisites for another basic-parts probe.

The synthesis below records the first probe and its then-open questions. Its
authoring model remains the working direction; the later evidence and closure
supersede its forward-looking probe suggestions.

2026-09-09. Discussion synthesis requested by Ashton after the completed
[Ikea blocks probe](../ikea-blocks/README.md). This updates the
[second sketches](sketches-2.md), using the [executable review](predictor-review.md)
and retained evidence. Following Ashton's instruction to consolidate, this is
the working design baseline for subsequent probes. The authoring model and
separation of responsibilities below are provisional choices to carry forward;
the explicitly open comparisons remain open. Concrete C++ interfaces, carrier
families and production schemas have not been selected.

**Later evidence, 2026-09-09:** the [integer composition exercise](../ikea-integers/composition/README.md)
now tests reuse of one decoded value by two consumers, separate tail format and
parent placement, and explicit compiled-program admission. Statements below
about demonstrated mechanisms describe the first probe. The follow-up keeps
its own findings and limitations; it does not settle general carrier allocation,
progressive filtering, or Engine substitution.

**Later consolidation, 2026-09-10:** the
[granularity investigation](operation-granularity.md#what-the-heterogeneous-probe-now-adds)
incorporates the heterogeneous metadata/bitset probe: cursor authoring and
selected compiled regions coexist, while retained metadata and parent locality
have concrete costs. The first-probe observations below remain historical;
the follow-up does not settle Engine-visible nested substitution.

Ashton subsequently selected packed integers and then heterogeneous bitset
metadata. The [probe connection](#next-probe-packed-integers) below preserves
the original motivation; PFoR-like reconstruction was not part of those probes.

**We have support for a common composition mechanism and some concrete boundary
choices. We do not yet have evidence that it handles the defining difficult
cases ergonomically.** The successful example has one source, a unary chain,
an explicit loop and a scalar reduction. Native handoff is demonstrated;
deep substitution, reused values and integration effects are not.

## Status of the consolidation

| Status | What to carry forward |
| --- | --- |
| Constraints from the brief | Native ISA-specific bodies; the manual-fusion/inline/CPS distribution; semantic substitution through generic interfaces; rich Engine inspection with cheap execution; independently represented segments; explicit masks, effects and lifetimes. |
| Working authoring choice | Recordable functions compose supported operations. Named components may publish structure and delegate to those functions. Declarations add implementation and rewrite knowledge that recording cannot supply. These are complementary responsibilities, not three mandatory descriptions of the same work. |
| Demonstrated mechanisms | Shared native bodies in inline/CPS execution, compatible native immediate handoff, dependency-driven lowering within a unary vocabulary, and attachment to contiguous/strided placement. Evidence is bounded by the probe. |
| Open comparisons | Carrier roles/families versus selected compiled regions; child discovery and dependency representation; symbolic mask/progress operations; prepared-plan versus segment attachment; mutation and suspension ownership; build/text costs across many recipes. |

An upcoming probe should challenge the working choices when an actual edit
becomes awkward. It need not implement every open seam, and a working choice
is not a requirement to generalise the first probe's classes or lowerer.

## What the probe changed

### A shared composition function is a credible centre

The author writes a short function over recordable operations. Its explicit
loop body runs with either symbolic values or native operands. Native feature
bodies are shared by inline execution and separately compiled continuation
wrappers. The later lowerer assembles reusable stages from dependencies rather
than recognising complete prewritten recipes.

This is meaningful support for separating composition from native bodies.
It is bounded support: adding a new primitive still crosses the recorder,
inline executor, stage registration and lowerer. The probe has not established
that those authoring chores can stay small across a useful vocabulary.

A's named component and B's free function use that same mechanism and produce
the same graph. Their difference here is packaging and child discovery, not
two competing execution architectures. We should stop treating them as such.
The value of a named component's hierarchy remains open because the source
is only one opaque child marker.

### Erased attachment can coexist with native intermediate values

The [ABI controls](../ikea-blocks/abi/README.md) and
[composition stages](../ikea-blocks/composition/README.md#the-actual-hot-sequence-and-its-costs)
demonstrate immediate scalar/native-vector handoffs through opaque calls.
The composition's compatible `musttail` wrappers carry YMM0 on x86 and two
native Q arguments on ARM without an intermediate payload store/reload in
the inspected baseline. The outer call uses erased binding metadata.

The tested wide `std::expected` and mixed scalar/vector aggregate returns
materialise under ordinary opaque return ABIs. The small inline success
controls remove that representation. The design consequence is to distinguish
checked admission, native immediate handoff and intentionally materialised
endpoints. It is not a blanket ban on `expected` or aggregate source types.
Native argument shape and the actual compiled boundary matter.

### Register handoff does not make CPS equivalent in cost to inlining

These are medians from the retained hardware CSVs, recomputed for this synthesis:

| Short resident analysis, ns per tile | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | --- | --- | --- |
| Authored inline loop | 2.910 | 3.879 | 3.599 |
| Separate reusable CPS stages | 3.480 | 4.454 | 4.338 |
| CPS with load/features combined | 3.337 | 4.448 | 4.223 |

Separate CPS costs about 15–21% more here. This is the total implementation
difference, including dispatch, loop mechanics and lost optimisation across
boundaries; it does not isolate a universal price per continuation hop. Local
fusion barely helps on Granite Rapids. Widening feature extraction from 256
to 512 positions loses on both x86 targets. See the
[hardware findings and workload limits](../ikea-blocks/README.md#hardware-findings-2026-09-09).

Execution grain and fusion boundaries therefore need to remain selectable
independently of stored block types and logical work. This supports retaining
all three execution buckets. Build-time and binary-size savings over many
recipes were not measured, so the experiment cannot yet establish the main
economic case for CPS, nor reject it for losing this runtime comparison.

The current common signature also carries dead operands. A richer scalar
model saves/restores RBX on x86; the observed code does not isolate whether
packing, signature pressure or another allocation choice caused that cost.
There is no established universal carrier budget.

### Binding needs implementation facts as well as format identity

Contiguous and strided attachments reuse bodies without a new block type or
pipeline per segment. This is a real separation of placement and computation,
but not a test of nested formats or a complete prepared-plan/cheap-attach split.
The prototype's `prepare` still combines source admission and graph lowering.

The codecs expose another useful seam: logical encoded extent can be smaller
than the implementation's actual write footprint, and a reader can require
padding beyond the logical body. External metadata also participates in
interpreting headless bytes. Such requirements must be satisfied by the
containing allocation/view and kept valid during use. A fast implementation
cannot silently strengthen the preconditions of a container call.

This also sharpens future MVCC integration: reporting the returned logical
length is not necessarily sufficient to cover modified bytes. Actual physical
write coverage and logical record/summary changes remain different outputs.
The probe exposes the requirement; it does not implement that integration.

### Equivalence and empirical usefulness need different records

Combining load and features preserves the fixed estimator. Selecting a
different estimator changes the function being evaluated; its measured quality
can justify a policy choice, but does not establish an equality rewrite.
An estimated size is not a safe allocation bound. The predictor's structural
holdout also changes the apparent quality tradeoff relative to natural data.

Engine needs distinct records for semantic equivalence, applicability and
contextual evidence about cost or quality. Approximate requests may admit
several estimators under an explicit quality contract; they must not silently
become identical exact kernels in an equivalence graph.

## Consolidated authoring model

Use B's function over supported operations as the provisional way to author an
ordinary composition. Retain A as an optional named component that publishes
children and delegates to that function. Retain C's declarations where they
add knowledge: primitive semantics, an opaque implementation's applicability,
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
to omit every inactive input. The reused `values` local is intentional: this
small composition already exceeds the current lowerer's lifetime support.
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
facts. These remain requirements from the brief; the stride experiment only
exercises a small part of them.

## Provisional answers and authoring map

The original organising principles survive. They now have more concrete
implications: preserve semantic identity across execution choices; separate
layout, meaning and grain; keep native obligations local; expose boundary and
lifetime costs; retain the meaning and coverage of evidence.

| Seam / authoring task | Provisional answer | Unsettled part |
| --- | --- | --- |
| Add a composition of existing operations | Write one recordable function; reuse registered bodies/stages. A named wrapper is optional. | Reuse, masks and symbolic control must fit without central recipe cases. |
| Add a native implementation | Keep the body ISA-specific. Associate it with a semantic contract, applicability and compatible wrapper(s). | How few declarations and wrapper variants suffice? |
| Attach execution to data | Establish applicable layout, resource and lifetime facts before repeated hot calls; retain their owners. | Separate reusable plan preparation from cheap attachment across genuinely different representations. |
| Substitute an implementation | Preserve results, permitted accesses/effects and requested observation/progress behaviour. Discharge additional requirements at binding or use an explicit adapter. | Richer effects and evidence contracts are untested. |
| Cross an erased boundary | Erase metadata separately from live computational values. Match native ABI families; make carrier bridges explicit. | Carrier allocation, family count and value lifetimes. |
| Select inline/fused/CPS execution | Keep logical work stable and map to the selected executable explicitly. An inline route may be a selected compiled composition. | Many-recipe build/text cost and economical region boundaries. |
| Add a physical child or transform | Publish its description, dependency edges and operation expansion. | Real deep substitution, ragged placement, auxiliary inputs and row coordinates. |

For the information architecture, organise discovery around physical formats
and placements; operation contracts and native implementations; compositions
and lowering; container children/expansions; and integration drivers/adapters.
Semantic/recording headers should not import native bodies merely to obtain
operation identities. This is a map of responsibilities, not a directory move.

Keep the data vocabulary from the charter. The probe supports distinguishing
block/placement/native grain, but does not settle plane, record or variable-width
usage. For code, keep kernel as a semantic computational contract, implementation
as its realised body/composition, and stage as an occurrence in a pipeline.
A CPS wrapper is a callable implementation boundary: it need not correspond
one-to-one with a logical stage, block or scheduling unit.

## Retire these as general solutions

- **A versus B as competing architectures.** The actual comparison is named
  packaging/discovery around a common composition function. Keep that ergonomic
  question, retire the architectural tournament.
- **Recognising whole recipes as runtime composition.** Keep explicit mappings
  for selected compiled regions, but ordinary new CPS combinations must be
  assembled from supported dependencies and reusable implementations.
- **A materialised wide result as the mandatory internal interchange.** It
  imposes traffic in the tested opaque calls that compatible native handoffs
  avoid. Materialisation remains appropriate at an actual storage/output or
  lifetime boundary.
- **A block's size choosing its native execution width or TU.** Traversal can
  tile and group it independently; the wider feature result actually lost here.
- **Unary chains as the boundary of straight-through CPS.** A value with two
  consumers can have a straight-through execution order. Rejecting it is this
  lowerer's limitation, not a reason to require a general control-flow VM.

Do not declare a single common signature, explicit implementation catalogues,
or compiled regions dead. Their operating regions remain open. Likewise,
successful native handoff does not establish masks, Loom suspension, deep
rewiring, or the scalability of the proposed CPS strategy.

## Promising next probes

These are historical candidate questions from the first synthesis. They remain
available when a concrete implementation needs them, not a required sequence
or a reason to extend the completed basic-data-structure probes.

**Straight-through value reuse: the tightest execution question.** The existing
[two sketches](value-reuse-sketches.md) hold the work fixed and compare bounded
carrier allocation with an inline region behind a continuation boundary.
They expose one native value consumed twice and a scalar kept across intervening
work. Perform the authoring edits, show the read/write/preserve declarations
and inspect the real wrappers. Compare a few related recipes to discover
whether register-slot variations multiply compiled code. This is more useful
than another unary predictor variant. Both approaches may survive in different
execution buckets; the issue is whether ordinary reuse forces compilation or
unrequested materialisation.

**A nested replacement in one of two coexisting segment representations: the
largest missing architectural signal.** Keep one outer read/filter contract.
Give one actual representation an encoded child inside a transform with an
auxiliary source, and retain another actual representation alongside it.
Replace only the deep child in a candidate schema; separately replace a read
pipeline fragment over unchanged bytes. Show exactly what the component
author, composition author and Engine caller edit. Compare explicit named
child access with schema-derived source handles while using the same recordable
function underneath. The questions are whether dependencies and affected parent
contracts remain discoverable, and whether both actual representations share
implementation code and a cheap call. Start with worked pseudocode; no new
codec or migration system is needed to expose a missing interface.

**Progressive filtering with retained values: the missing planner seam.** Compare
direct exact filtering with a signature-then-exact route over the same logical
request. Reuse the loaded value for filtering and projection, pass in a
prefilter, and exercise the empty-mask completion path. The signature's positive
result carries residual work, not exact truth. Let a transform need some
inactive inputs so mask dependency closure is visible. Preserve an optional
observation boundary, then adopt another prepared revision at a declared safe
progress boundary without modifying the live cursor's table. Compare an
explicit refinement call with a driver-owned observation point; neither
requires equivalence search on every call. The
[row-signature findings](../row-filter-signatures/FINDINGS.md) supply relevant
evidence semantics. Implement only the seam selected for investigation.

**Mutation or suspension: a different axis when integration ergonomics becomes
the question.** A variable-length replacement can force relocation, physical
write coverage and logical summary invalidation to differ. Alternatively, an
auxiliary read can force buffer acquisition and suspension while native values
are live. Each would test whether local bodies plus narrow drivers remain
credible. Do not combine both merely to make the next probe more comprehensive;
use the existing [aggregate work](../aggregate-maintenance/CONCLUSIONS.md) and
Calico Xmem/Loom reading when choosing that axis.

## Next probe: packed integers

Ashton's selected direction is a packed-integer probe, subsequently serving
both a PFoR-like structure and metadata for bitsets, and providing the substrate
for progressive filtering and nested substitution. Ashton will drive the
concrete scope. The alternatives above are available questions to bring into
that work, not prerequisites or a competing implementation sequence.

The important test is whether the primitive can acquire these uses through
local composition and context-specific implementations. PFoR and bitset
metadata are customers that challenge the boundary; neither defines the
primitive's universal meaning or the architecture of Ikea.

| Relationship to expose as the probe develops | Design question it answers |
| --- | --- |
| Packed bytes to decoded integer values | Which facts describe the physical format, placement and admitted access, and which belong to a native implementation? Packed width, block extent and native grain must remain independently expressible. No particular widths or geometry are selected here. |
| Decoded integers to a PFoR-like reconstruction | Where do base/patch or other reconstruction dependencies enter, and which operations can use residuals directly under stated conditions? The integer body should not need to know the enclosing container's whole lifecycle. |
| The same substrate to bitset metadata | Can the customer supply its metadata meaning, placement and consumer without adopting the reconstruction customer's interfaces? Metadata positions may index bitsets or groups; they must not silently acquire record-row coordinates. |
| Decoded values to successive consumers | Can filtering and another use retain values across stages? Keep bounded carriers and selected inline regions available; do not silently resolve the question by materialising every result or compiling every recipe. |
| A nested child to alternative realisations | Can an author expose a replaceable edge and its dependent facts while the outer contract stays stable? Keep actual per-segment representation, candidate migration and execution-only rewrites distinct. |
| Partial evidence to further work | What does an intermediate result establish, over which coordinates and grain, with what residual obligations? A coarse or transformed-domain result needs an explicit relationship to exact container-level filtering. |

Sharing the substrate does not require identical kernels in every context.
Keep specialised fused implementations eligible when they preserve the same
logical contract and required observations. Where the semantic contracts differ
(for example, packed-value decoding and reconstructed-value filtering), expose
the relationship rather than conflating their identities.

For collaboration, bring the author-facing code and a concrete extension or
substitution to review while the seam can still change. We will examine local
obligations, shared code, declarations and wrappers added, retained values,
admitted accesses, and Engine's ability to inspect or replace the work. Compiler
and timing evidence matter when those boundaries impose a cost. This task's
remit remains composition and ergonomics, not general implementation approval.
The first probe's fixed tile loop, feature packing and common stage signature
are evidence artifacts, not defaults for the integer probe.
