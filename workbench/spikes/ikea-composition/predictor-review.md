# Predictor composition: first executable review

2026-09-09. Review of the `ikea-blocks/composition` artifact following the
[reorientation](probe-review.md). Scope is composition, authoring and boundary
costs. This is neither a codec correctness review nor implementation approval.
The artifact was still being developed during inspection; findings below name
the inspected mechanisms so subsequent revisions can answer them explicitly.

**Revision follow-up:** the subsequent linear lowerer resolves the whole-recipe
binding restriction below within its supported vocabulary. Inline applicability
and semantic header dependencies have also been corrected. The current
composition boundary is live value reuse; see the follow-up at the end and
the [competing reuse sketches](value-reuse-sketches.md).

## What the exercise now demonstrates

The [authoring code](probes/ikea-blocks/composition/authoring.h) records an explicit
tile loop and reduction. [Native feature bodies](probes/ikea-blocks/composition/native_features.h)
are shared by the inline executor and separately compiled CPS stage wrappers.
The [driver](probes/ikea-blocks/composition/driver.cpp) owns repetition and the
accumulator; the body sees native bits. A prepared call erases binding metadata
without requiring the intermediate bits to be materialised.

The clean captured run `20260909T115108.439335Z` has a complete receipt, and
its source hashes for `stage.h`, `driver.cpp`, the four stage TUs and
`native_features.h` matched the inspected files. Its release AArch64 load is:

```asm
ldp q0, q1, [x1]       // input tile bytes -> native arguments
ldr x4, [x0], #8       // next stage and cursor
br  x4
```

The feature stage consumes `v0`/`v1`, produces packed scalar features in `w3`,
and tail-forwards without a stack buffer. Model and completion also avoid an
intermediate buffer in that capture. Inspected x86 AVX2/Zen5-targeted stage
output likewise hands `ymm0` from load to features through indirect jumps.
This is useful evidence for this signature, not a guarantee for arbitrary
carriers, bodies or register pressure. No new runtime measurements were run
by this reviewer. The earlier `20260909T114914.118055Z` native capture contains
sanitizer instrumentation and must not supply release handoff-cost claims.
The sibling's [runner](probes/ikea-blocks/composition/run.py) owns reproduction.

## Findings on the first executable version

**Recording a composition does not yet make it bindable.** In the inspected
[prepare.cpp](probes/ikea-blocks/composition/prepare.cpp), two static instruction
arrays describe complete recipes. Preparation reconstructs the canonical
graph, compares for equality, then tries its one Load + Features fusion.
Everything else is rejected. Adding a legal composition consequently requires
editing the central binder and supplying another whole-program route.
The nodes are inspectable, but they do not yet drive stage assembly.
This is the main unresolved promise of runtime composition and plan rewiring.

**A and B currently collapse to one recording design.** `NamedAnalysis::expose`
and `analyse` contain the same operation sequence. A adds an object, stored
source/model and a child accessor. Both use the same recorder, inline executor
and binder. That is legitimate evidence about a named wrapper versus a free
function; it does not establish two different composition systems. The
proposed benefit of named child ownership needs an extension where it changes
discovery or edits, or the comparison should explicitly retain this narrower
conclusion.

**The inline route does not follow the selected rewritten graph.** Preparation
accepts the fused graph for inline execution, but the inline entry calls the
original authoring function and ignores `binding.program`. The compiler may
fuse that work and the result may be equivalent; selection of this graph is
not what chooses the inline implementation. Separate logical recipe identity
from selected execution, or supply an explicit mapping. Equality of outputs
does not demonstrate that the requested rewrite controlled execution.

**Changing stride is a useful but narrow source substitution.** Contiguous
and stride-48 segments reuse the same inner body and driver. `SourceChild`
is still an opaque representation/path pair, and `Graph` contains one source.
The [inline executor](probes/ikea-blocks/composition/inline_ops.h) ignores the source
argument and consumes its one attached binding. An auxiliary source or a
replaceable nested child is therefore a real extension boundary. Keeping
different source identities alive does not by itself demonstrate Engine's
deep inspection or substitution of container compositions.

## Costs and ownership exposed by the code

- The [common CPS signature](probes/ikea-blocks/composition/stage.h) keeps native
  bits live through the scalar model and completion after their last consumer.
  The inspected stages avoid spills, but preserving dead operands restricts
  available registers. The outer driver also initialises otherwise unused
  native arguments each tile and pays a call/return per tile. Those costs
  belong in comparisons with inlining and fused regions.
- `graph.h` includes `native_features.h` to obtain feature identity. Operation
  semantics and native implementation dependencies are not yet separated in
  the information architecture. A target-independent contract can be shared
  without importing all native bodies into graph/authoring TUs.
- A genuinely new primitive touches the operation vocabulary, recorder,
  inline adapter, native body, stage declaration/wrapper and binding support.
  Several separate TUs are deliberate evidence machinery; distinguish that
  harness cost from the authoring cost the design would require routinely.
- The table demonstrates straight-through `musttail`, but not the bounded,
  aligned completion/early-exit protocol described in the charter. Prefilter
  work omission and transform dependencies remain unexercised here.

## Next bounded question sent to the collaborating task

**Can a new composition of supported operations bind without adding another
whole-program table or central special case?** One useful continuation is a
small linker for the existing straight-line vocabulary, with explicit carrier
and port requirements. Reuse the current native bodies and loop driver.
The linker must account for dependencies and live values; mapping op names to
function pointers alone would hide the same problem at a different seam.

Alternatively, retain this artifact as an ABI/control experiment and make the
next authoring sketch expose that missing extension. Neither path requires a
general optimiser or simultaneous implementation of all schema/integration
questions. The observation to preserve is where a local composition edit
stops being local, and whether a competing interface improves that experience.

## Follow-up: node-driven linear lowering

The revised [lower.cpp](probes/ikea-blocks/composition/lower.cpp) walks dependencies,
checks feature/carrier requirements and emits runtime-owned stage entries.
The newly authored transition estimator and its fused form run through this
lowerer without a whole-program table; reversing graph node storage exercises
dependency-based ordering. This resolves the earlier whole-recipe criticism
for the admitted linear vocabulary. New primitive registration remains
central, and the vocabulary still admits one source, one loop and one scalar
sum. This is a narrower result than general graph composition.

Preparation now rejects inline recipes without an explicit selected compiled
implementation. [contracts.h](probes/ikea-blocks/composition/contracts.h) separates
operation identities from native implementation headers. Those earlier
criticisms are resolved. A/B's scope is now explicitly packaging and child
discovery over a shared recorder, with deeper source substitution unresolved.

The retained [release evidence](probes/ikea-blocks/composition/evidence/release-20260909)
shows the baseline native handoff described above. Its richer x86 transition
model saves/restores RBX on the nontrivial path while extracting a packed
field; there is no vector payload spill in the inspected code. That identifies
an actual cost without isolating whether dead signature operands, the packed
field extraction or another allocation decision caused it. A scalar carrier
comparison would be needed to attribute or remove that cost. The review did
not rerun codec checks or measurements.

The strongest remaining composition restriction is `users[id] > 1`: every
reused value is rejected. A producer with two consumers creates branching data
dependencies, but those consumers can run in sequence with no branching
control flow. Such reuse is part of the original brief, not a request for
ChainVM's general control flow. The ABI already preserves native bits through
later stages. The next question is how an author exposes reuse and how a
bounded carrier mapping preserves the additional live values. The linked
sketches compare that with an inline region while retaining the same work.
