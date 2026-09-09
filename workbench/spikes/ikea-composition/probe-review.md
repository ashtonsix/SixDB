# Reorienting the concrete composition probe

2026-09-09. Ashton's correction of this task's reviewer/collaborator role.
This records a failed design exercise and the next discussion, not an accepted
interface or a general review of the codecs.

The restart's first executable artifact now has a separate
[composition review](predictor-review.md). It demonstrates native body reuse
and a register handoff, while exposing a whole-recipe binding restriction.

## What this task failed to do

The first `ikea-blocks` implementation treated the sketches as optional context.
It materialised plain/compressed children into a fixed `Pair256`, then selected
ordinary function pointers over that stored aggregate. This task reinforced
that path with advice about validation, failure guarantees and overloads.
It did not require a concrete comparison of the authoring alternatives or
challenge the memory boundary against the agreed composition requirements.

That failure was this reviewer's responsibility. Passing correctness checks,
honestly labelling materialisation, and documenting local extension costs did
not answer the design question. The implementation has since been deleted on
Ashton's instruction. These observations refer to the version read before
deletion; no endorsement of its interfaces carries into the restart.

| Agreed concern | What the first probe left unexamined |
| --- | --- |
| Separate physical layout, logical work and execution grain | A fixed pair aggregate also defined the SIMD operation boundary; adding another size/context had no worked authoring path. |
| Preserve native intermediate values across erased composition | The common interface required stored decoded values before the selected kernel. No competing handoff was shown. |
| Reuse bodies across inline and straight-through CPS execution | CPS and fusion were deferred; ordinary function pointers were the only composed execution mechanism. |
| Let Engine inspect and replace nested parts | A closed variant offered local dispatch, but no exposed operation expansion or rewriteable composition. |
| Keep the hot body local and cheap | Checked decode, result transport and materialisation shaped the interface before their placement or ABI cost was challenged. |
| Compare authoring experience | Moving branches into overloads changed one implementation; it did not compare the sketches' different ways of expressing the same work. |

The response is to make those design choices concrete in a small exercise.
It is not to require a complete planner, all integration hooks, or a chosen
framework before kernel work can proceed.

## Restart: the next proposed comparison

Ashton's updated brief in **Open Ikea blocks spike** removes compressed
decode/operate/encode algebra, requires competitive native codecs, separates
data definitions from free-standing compute, and adds an analyser-facing
compressed-size predictor. That predictor is distinct from an encoder's
early-exit threshold policy. Naming, fitting and measurement remain with that
task and its collaborators.

The proposed composition exercise uses that predictor: read input bits,
extract features, apply a fixed model, and accumulate estimates over a larger
bitset. The model/features/format are parameters of this point probe, not
Ikea-wide requirements. Compare two spellings of the same work:

- **A, named components:** the author declares the child and exposes the
  operation expansion. Show the declarations and the enclosing traversal.
- **B, recordable functions:** the author composes existing operations in an
  ordinary function. Show the supported symbolic operations and how traversal
  and shared values appear to the recorder.

The next review should examine actual author code and an actual extension
diff. For example, substitute a feature implementation or change the enclosing
repetition without editing the native bitwise body. Identify declarations an
author repeats and machinery they must understand. A rule-based alternative
can be added if it offers a materially different answer; none is selected.

### First reply and adversarial feedback

The direct channel with **Open Ikea blocks spike** was restored on 2026-09-09.
The task returned these two initial pseudocode shapes:

```cpp
// A: a named analysis component with an exposed expansion.
component BitsetAnalysis {
    child source;
    model_id model;
    expose PredictEncodedBytes =
        Tile(source, 256).Map(Load -> Features -> FixedModel).Sum;
};

// B: an ordinary composition over recordable operations.
analyse(ops, source, model) {
    return ops.repeat256(source, [&](tile) {
        return ops.accumulate(
            ops.model(ops.features(ops.load(tile)), model));
    });
}
```

These are sketches, not runnable APIs. Both propose one native feature body
shared by inline and CPS wrappers. The proposed common CPS signature carries
native input bits, scalar features and an accumulator; actual signature and
code-generation evidence are still outstanding.

The strongest objection to A is coherence between the declared child and its
expansion: the hierarchy alone does not expose cross-operation value reuse.
Its defence is that explicit children make replacement discoverable. That
must be weighed against the edits required for an ordinary new composition.
`BitsetAnalysis` also needs to be clearly an analysis composition: changing
its model must not imply changing a segment's physical representation.

The strongest objection to B is the hidden work in `repeat256` and
`accumulate`: the recorder must represent repetition, state and supported
control flow, and runtime rewrites must bind reusable stages. Templates alone
do not supply that capability. Its defence is that the composition is ordinary
local code with a restricted operation vocabulary; the exercise must expose
where that restriction becomes awkward.

Feedback sent back requests two concrete authoring changes: alternate the
source's contiguous/strided access, and replace Load + Features with a fused
feature implementation for the same model. Show the declarations edited,
stages reused and structure Engine can inspect in each spelling. The review
also challenges the distinction between semantic model grain and SIMD width:
a 512-bit implementation producing two 256-position estimates must preserve
the per-block feature definitions, including adjacency boundaries. The
repetition owner, accumulator lifetime and restart of the straight-through
stage table must be shown. This is a request for a bounded design probe, not
approval of either proposal or of an implementation.

## Specific objections to bring to the sketches

**Does tiling quietly become the format or kernel identity?** A free-standing
512-bit body can serve several layouts after an access adapter establishes
the appropriate native operands. Covering 512 positions does not itself
establish contiguous readable bytes. Position mapping, extent and layout
requirements belong at that seam. A few static repetitions and a runtime
outer loop are alternatives to adding a new implementation TU for every size.
The awkward case is a valid enclosing layout that cannot use the same load.

**Where do the values go between unlike stages?** Draw the actual SIMD and
scalar carriers through load, features, model and consumer. A proposed CPS
binding must explain compatible signatures and calling conventions. A small
flattened ABI family can carry the SIMD operand plus selected scalar fields,
but its unused/live argument pressure is a real cost. A fused native region
followed by scalar continuation makes another boundary with an explicit owner;
it must not become an excuse to make fusion mandatory for most compositions.
Neither option is established by writing `bind(..., cps)` in pseudocode.
The same native body should appear in inline and continuation wrappers.
BytePack's cursor/completion protocol remains the primary prior.

**Did validation merely move into a hidden per-tile call?** Checked admission
and trusted hot execution need a stated validity scope and an owner for the
layout, bounds and external facts. Do not silently strengthen the old checked
API's preconditions while calling the replacement substitutable. Conversely,
the checked API need not dictate every inner result or error carrier. The
compiler probes should examine the actual proposed seam, including callers.

**What exactly is equivalent?** Factoring or fusing one fixed predictor model
is a candidate implementation substitution under its numeric contract.
Choosing different weights is a different estimator unless an enclosing
contract explicitly permits that choice. Predicted size is empirical evidence,
not a capacity bound or a sound pruning proof. Model identity, target encoding,
coverage and cost/accuracy evidence must survive analysis without becoming
policy inside the feature body. This connects to
[plans that keep improving](../../notebook/ideas.md#plans-that-keep-improving).

**Can Engine change a deep part while the call stays simple?** A small schema
and operation sketch can show an exposed child, a plan-only rewrite over the
actual bytes, and a physical replacement that needs re-encoding/publication.
Different segments may retain different actual representations indefinitely.
Prepared calls attach the applicable resources and assumptions; they do not
run an optimiser for every tile. The exercise needs to show these seams, not
implement Engine.

The predictor does not exercise prefilter-dependent work omission, transform
dependencies, mutation effects or Loom suspension. Those remain open questions
in the charter. A useful result here will narrow an authoring or execution
choice; it will not certify composition as solved.
