# A real analysis composition, in two authoring spellings

2026-09-09. This probes [sketches A and B](../../ikea-composition/sketches-2.md)
with useful work: **sum a fixed BEC byte-size estimate over independent
256-position tiles**. It compares authoring, graph inspection, native inline
execution and reusable continuation stages. It does not implement Engine,
choose an Ikea interface, or endorse the predictor as a compression policy.

The result so far is narrow: **A and B differ in packaging and child discovery;
they expose the same operation vocabulary and record the same graph.** Both
can use a selected inline recipe or a runtime-lowered linear CPS program.
The actual native handoffs keep bits and scalar fields in registers. The CPS
dispatch and its wider live signature still have costs that inline fusion avoids.

## What the author writes

[authoring.h](authoring.h) contains the two runnable forms. A introduces
`NamedAnalysis<Source>` with a published `source` child, a model tag and an
`expose(ops)` method. B is `analyse(ops, source, model)`. Both bodies say:

```cpp
return ops.sum_tiles(source, [&](auto tile) {
    auto bits = ops.load(tile);
    auto fields = ops.features(bits);
    return ops.model(model, fields);
});
```

`Recorder` supplies symbolic tile/value handles. It records the lambda once,
with a symbolic tile index, and explicitly creates `Repeat256`, `TileIndex` and
the zero-initialized `Sum` reduction. `InlineOps` supplies native operands and
runs the same authored loop body for actual tiles. Binding replaces the symbolic
source with an `AttachedSource` marker; no strings, schema lookup or admission
checks are constructed inside the prepared hot call.

The source has an actual representation identity and an opaque child path,
for example `202 / field.membership.child.tiles`. Recording and operation
identities are target-independent: [contracts.h](contracts.h), [graph.h](graph.h)
and the authoring header do not import intrinsics or the stage ABI.

| Spelling | Concrete gain | Strongest objection exposed here |
| --- | --- | --- |
| A: named analysis | `analysis.child()` provides a discoverable source edge; an author can copy the component and replace `source`. | The author publishes a child and separately keeps its enclosing expansion coherent. This example is only one opaque edge: it does not establish deep physical-child replacement or cross-child reuse. |
| B: function over operations | Existing operations compose in an ordinary function without introducing a new component type. | The body is ordinary C++ only within the supported vocabulary. The loop must be `sum_tiles`; arbitrary C++ branching, loops over sampled values and retained intermediate reuse are not recorded automatically. |

Both share the second restriction. This example provides no evidence that one
spelling is a superior architecture. There is no actual schema tree beneath
the opaque `SourceChild`, no auxiliary second source, prefilter-dependent
omission, mutation protocol or suspension mechanism in this probe.

## Actual graph lowering, not whole-recipe recognition

[lower.cpp](lower.cpp) walks dependencies starting at the sum's body value,
checks each operation's required carrier/feature contract, and appends its
reusable compiled stage to a runtime-owned instruction vector. Node storage
order is irrelevant. `Repeat256` and `TileIndex` belong to the outer driver;
`Sum` supplies the final scalar completion stage.

The narrow lowerer supports one linear tile body and a scalar sum. It rejects
cycles, out-of-range dependencies, unsupported operations, unreachable work,
multiple-consumer data dependencies and reused values whose lifetime it cannot
represent. Such dependencies can execute as straight-through control flow;
their rejection is a limitation of this lowerer and fixed carrier allocation,
not evidence that they inherently require control-flow machinery. It does not
quietly allocate temporary payload buffers to accommodate them. The small
64-node limit is a probe bound, not an Ikea graph limit.

The normal body lowers to `Load → Features2 → Model2 → Done`. Applying
`fuse_load_features(graph)` changes the local load/feature fragment to one
`LoadFeatures2` operation, preserving source, feature contract, model identity
and arithmetic. The lowerer maps that node to a fused compiled wrapper.
It does not choose between prewritten whole-pipeline tables.

`analyse_transitions`, a newly authored function in the same header, demonstrates
another legal composition:

```cpp
return ops.sum_tiles(source, [&](auto tile) {
    auto fields = ops.features_transitions(ops.load(tile));
    return ops.model(TransitionModel{}, fields);
});
```

This lowers through the same code using separately registered feature/model
stages. Tests reverse all node storage indices before lowering this graph and
also execute its fused variant. There is no new driver or whole-recipe table.
The changed model is a **different estimator**, not an equivalence rewrite.
Trying to supply only two features to the transition model fails admission.

The inline side is intentionally more restricted. The two compiled inline
entries cover the known, unfused two-feature recipe. The binder explicitly
maps that lowered sequence to the selected implementation. It **rejects** fused
graphs and other estimators for inline execution; it does not accept a graph
then silently ignore its chosen operations. An additional selected inline
implementation would be an explicit compilation choice. General CPS lowering
and selected inline recipes are different execution mechanisms here.

## Two concrete edits

**Contiguous to interleaved access.** A's `source` member or B's source argument
is obtained from the other actual segment description. The description changes
`Layout::contiguous, stride=32` to `Layout::strided, stride=48`, with matching
representation identity and bytes. The adapter attaches `first/count/stride`.
The authored body and every native feature/model stage remain the same. This
does not reinterpret the old bytes or migrate other segments. Both actual
descriptions may remain bound indefinitely.

**Fuse load and features over current bytes.** The local graph rewrite above
changes only the execution graph. An author spelling the fused operation
directly would replace `ops.features(ops.load(tile))` with
`ops.load_features(tile)` in either body. That operation is already recordable
and has its own reusable stage. The source descriptor and fixed model stay
unchanged; the checked rewrite requires the load value to have one consumer.

Changing repetition from 2 to 128 or 256 tiles changes an attached count, not
a TU or physical block class. Tiling, storage stride and SIMD width are separate.
The optional 512-bit feature body produces **two** independent 256-position
feature records. It never treats their combined population as one model input.
Transition features currently have a 256-bit implementation only; their final
bit has no neighbour in the next tile.

## The actual hot sequence and its costs

[native_features.h](native_features.h) owns free-standing bodies. The same
`native_features` implementation is used by the inline executor, opaque scalar
benchmark wrappers, the CPS feature stage and the fused load/feature stage.
The two-feature body computes distance `abs(128-pop)` and the sum of byte-rank
widths `[0,3,5,6,7,6,5,3,0]`. There is no validation or error handling in these
bodies. Separate measured candidates add internal transitions and quadrant
dispersion; the simple pipeline does not pay for those unused fields.
Transitions count disagreements across the 255 adjacent bit pairs. Quadrant
dispersion is `sum(abs(4*pop(quarter)-pop(tile)))` over four 64-position quarters.
Their packed fields occupy bits8–15 and bits24–33 respectively; enumerative
cost remains in bits16–23. Both added features are complement-symmetric.

[stage.h](stage.h) fixes the small common signature:

```cpp
uint64_t stage(const Instruction* cursor, const uint8_t* tile,
               uint64_t accumulator, uint64_t fields,
               /* x86: __m256i; ARM: two uint8x16_t arguments */);
```

Every intermediate stage is in its own compilation unit or shared primitive
TU, marked `noinline`, and ends in a genuinely indirect
`[[clang::musttail]] return cursor->execute(cursor+1, ...);`.
The driver snapshots stride/count/address and the program entry, initializes
the accumulator, and restarts the program for each tile. `Done` adds that
tile's estimate and returns the scalar accumulator to the driver. The immutable
program vector and segment owner outlive every cursor; completion does not
dereference the one-past-end cursor.

| Live transport | x86 SysV AVX2 / Zen5 | AArch64 NEON |
| --- | --- | --- |
| cursor / tile / accumulator / fields | RDI / RSI / RDX / RCX | X0 / X1 / X2 / X3 |
| bits | YMM0 | Q0 and Q1 |
| feature output | RCX, distance bits0–7 and enum cost bits16–23 | W3, same packing |
| completion result | RAX | X0 |

The [retained release excerpts](evidence/release-20260909) keep complete load,
feature, fused-feature, model and completion functions. They show no
intermediate payload store/reload or stack frame in the baseline two-feature
load, feature, fused-feature, model or completion stages. On Zen5's
compiled feature path, `vpopcntb`, `vpshufb` and reductions use YMM1–4 while YMM0
remains live; RCX receives the packed fields before the indirect jump. On ARM,
`cnt`, `tbl` and reductions use V2–5 while Q0/Q1 remain live; the packed result
is formed in W3 before `br x4`. Model stages consume the scalar word directly.
There is no `std::expected` or mixed aggregate return in this sequence.

This common family deliberately exposes unused operands. Before load, incoming
bits/fields are unused, so the driver still seeds them with zero. After features,
the vector remains an ABI argument through model/completion even though this
particular graph never reads it again; tile and accumulator also occupy fixed
argument slots through stages that do not consume them. The richer transition
model exposes a concrete cost: its non-empty/full x86 path saves and restores
RBX (`pushq %rbx` / `popq %rbx`) while extracting the transitions byte with
`movzbl %ch, %ebx`. This adds an eight-byte callee-saved register round trip
per such tile in both the AVX2 and Zen5 captures; the final transfer is still
an indirect tail jump, and there is no vector payload spill. The ARM transition
model uses no stack frame. The baseline's spill-free stages therefore do not
establish a register budget for even this small extension. The assembly shows
the allocation cost; it does not isolate unused signature operands from packed
feature extraction or another compiler allocation choice as its cause. A
scalar-only continuation family would need an explicit bridge and new compatible
wrappers; this lowerer does not yet perform carrier-lifetime allocation.

The driver has ordinary callee-saved register/frame overhead around its loop.
An unfused tile has one indirect call from the driver and three indirect tail
jumps; load/feature fusion removes one jump. Inlining the whole recipe also
allows model-specific work elimination (the empty/full result can bypass the
enum-cost computation) and keeps constants outside the loop. Register residency
does not remove these dispatch or optimisation differences.

## Admission and empirical limits

[prepare.cpp](prepare.cpp) matches actual child identity, 256-position model
grain, layout/stride, every tile's 32 readable bytes, sum range and the compiled
ISA. It retains a `shared_ptr<const Segment>` plus immutable compiled program.
The integration contract forbids mutable aliases while those admitted bytes
and metadata are in use. It checks once at this boundary, not once per tile.
The public call is simply `prepared()` over erased attached metadata. Bounds,
layout or lifetime changes require another admission.

The baseline model is `bec256-distance-enum-q12-20260909`, whose integer
coefficients, rounding and clamp are fixed by the [predictor](../predictor/README.md).
Model identity is preserved in the graph and the lowered operation list.
Predicted bytes are empirical estimates, not capacity bounds or sound pruning
proofs. The two-feature model cannot distinguish some byte permutations with
very different tree costs; the transition model is an alternative being
measured. This composition result does not select either model's policy.

The [release checks](evidence/release-20260909/native-check.txt) on native ARM
and QEMU AVX2 exercise 198,150 scalar-oracle feature cases,
both 512-grain outputs, 162 graph/layout/execution combinations, retained owner
lifetime, and rejected graph/metadata contracts. The separate
[native ASan/UBSan check](evidence/sanitize-20260909/native-check.txt) passes
the same cases. Zen5 here is compiler evidence until the parent spike's hardware
run executes it. These checks count cases; they are not timings.

## Build, measure and inspect

The parent spike can `add_subdirectory(composition)` and link
`ikea_composition_core`. Targets are `ikea_composition_check` and
`ikea_composition_bench`. A standalone CMake build also works with pinned
Clang 21.1.8; each TU remains independently compiled and LTO/unity are disabled.

```sh
orb -m ubuntu python3 workbench/spikes/ikea-blocks/composition/run.py --cross
orb -m ubuntu python3 workbench/spikes/ikea-blocks/composition/run.py --sanitize
```

`--cross` adds AVX2 execution under QEMU and Zen5 compilation on the configured
ARM development host. `--benchmark` times only the native host; `--march` sets
its ISA. Outputs use captured source, the shared Run receipt and `SIXDB_RESULTS`
when present. Full object disassembly, including drivers and inline endpoints,
stays in the S3 bundle. Git keeps the short baseline chain and the transition
model's register-save example; sanitizer instrumentation is not exported as
assembly. Recover and reproduce the release excerpts without compiling:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/ikea-blocks/composition/evidence/release-20260909 \
  build/recovered/ikea-composition
orb -m ubuntu python3 workbench/spikes/ikea-blocks/assembly.py composition \
  build/recovered/ikea-composition build/recovered/ikea-composition-excerpts
```

The standalone benchmark takes an optional tile count (default 4096), pins
`SIXDB_CPU` or the first allowed CPU, calibrates each case to at least 20 ms and
runs five repetitions sequentially. It compares the two inline spellings,
CPS/fused CPS, two/three/four-feature extraction and complete predictors, plus
two-record 512-bit extraction where applicable. Inputs are a reproducible
synthetic mixture in contiguous and stride48 layouts; footprint and stride are
recorded, and no cache-residence or accuracy claim is inferred. Native ARM VM
timings are local calibration only; use the parent hardware runs for target
comparisons. A/B's generated computational loops match, so small timing
differences do not establish an authoring-style performance advantage.
