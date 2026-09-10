# One producer, two consumers, straight-through execution

2026-09-09. Competing pseudocode following the
[linear-lowering review](predictor-review.md). These are discussion designs,
not an implementation request, an interface selection or compiler evidence.
They keep the body, authoring and execution questions together.

Both alternatives remain open in the [working design](design.md). Ashton
will drive the next probe through packed integers and their customers; the
estimator example here is a replaceable way to expose these lifetimes, not
work that must be implemented before the integer probe can proceed.

The predictor probe currently rejects all reused values. Use one small
analysis exercise to expose the limitation: accumulate per-tile disagreement
between two estimators over the same loaded bits. This is an observable scalar
result with the existing sum driver. Disagreement is merely an analysis result;
it does not establish either estimator's accuracy or select a compression
policy. The particular models and feature sets are replaceable point probes.

## Same authored work in both alternatives

```cpp
return ops.sum_tiles(source, [&](auto tile) {
    auto bits = ops.load(tile);
    auto f0 = ops.features(bits);
    auto p0 = ops.model(BaseModel{}, f0);
    auto f1 = ops.features_with_transitions(bits);
    auto p1 = ops.model(TransitionModel{}, f1);
    return ops.abs_difference(p0, p1);
});
```

This has one straight-through execution order: load, features, model, features,
model, difference. `bits` remains live for the second feature consumer; `p0`
remains live while computing `p1`. A separate optimisation may share common
feature computation, but ordinary composition should first have a way to
express these dependencies without silently materialising them.

## A. Reusable stages with a bounded carrier mapping

Keep native bodies unchanged. A small family of compatible stage wrappers
exposes which live roles it reads, writes and preserves. For this exercise,
the family has native bits, a working scalar word and a saved scalar word,
alongside the existing cursor, source pointer and accumulator.

```text
stage                  reads                 writes              keeps live
Load                   tile                  bits
Features               bits                  work                bits
BaseModel              work                  saved               bits
TransitionFeatures     bits                  work                saved
TransitionModel        work                  work                saved
AbsoluteDifference     saved, work           work
SumCompletion          accumulator, work     final scalar
```

These are semantic roles and one proposed physical allocation, not argument
names every kernel author must adopt. The BaseModel body still accepts its
feature value and returns a scalar; its wrapper routes that result to `saved`.
Other uses can route the same body's result to `work`. Feature bodies still
use explicit native intrinsics. No body learns graph traversal or Engine policy.

The preparation seam validates that the chosen wrappers' signatures and
read/write/preserve rules implement the recorded dependencies. It chooses a
supported family or diagnoses an unsupported lifetime. The generated program
contains reusable wrappers; it does not compile every graph revision. A small
field-list macro or adapter template could generate consistent signatures and
forwarding. The authoring cost of those declarations must be shown.

**Strong objection:** wrapper choices can multiply with result placement,
native width and live roles. A family broad enough for many graphs may reserve
registers unnecessarily; many narrow families may consume code size and make
binding difficult. Encoding role allocation directly into every native body
would defeat local author reasoning.

**Defence to test:** use a few explicit families and reusable adapters, with
allocation handled during preparation. This example needs only one additional
live scalar slot. Whether that remains cheap is a compiler question, not a
consequence of calling the fields scalars. Compare packed versus separately
passed feature fields independently of their logical contract.

## B. An inline composition region behind a reusable continuation boundary

Use the same feature/model bodies in a normal function whose locals expose
the value reuse to the compiler. Publish its decomposition for analysis and
provide a CPS wrapper for the resulting scalar region.

```cpp
inline scalar disagreement_native(native_bits bits) {
    auto p0 = base_model(features_native(bits));
    auto p1 = transition_model(transition_features_native(bits));
    return abs_difference(p0, p1);
}

// Declaration ties the implementation to the same exposed composition.
implementation Disagreement(bits)
    body disagreement_native
    expansion the_authored_feature_model_difference_region;

// A continuation wrapper calls the body and forwards the scalar result.
```

Here `native_bits` denotes a concrete target-native type in each implementation;
it is not a portable SIMD abstraction. The wrapper and its chosen compatible
signature remain explicit. Merely ending the body with a scalar result does
not prove that the surrounding CPS family stops carrying unused vectors.

**Strong objection:** every new region can require another compilation and
registration. Binding an arbitrary new composition through this route can
turn the large majority of pipelines into compiled combinations, undermining
the stated build-time and binary-size motivation for CPS. Exposing a
decomposition also risks duplicating the native function's semantics.

**Defence to test:** reserve compiled regions for selected compositions and
reuse their leaf bodies. Let ordinary recipes use A where available; measure
the actual edit and compilation cost when adding this region. This is the
inlining bucket, not a reason to make manual fusion compulsory.

The comparison is about who must describe lifetimes, where code variants
multiply, and what an ordinary composition edit costs. It does not select a
universal solution. Both alternatives preserve the logical operation identity;
their implementation applicability and costs are separate facts for Engine.
