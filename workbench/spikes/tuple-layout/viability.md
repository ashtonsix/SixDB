> Status after the implementation decision: Ikea TuplePack is implemented
> with manually specified layouts. Engine analysis stays in this spike pending
> broader architecture work. The analyser directions below remain research
> conclusions, not authorization to promote an Engine module. See the
> [module contract](../../../ikea/docs/tuplepack/reference.md).

# TuplePack: enough to start implementing

The spike supports a viable first TuplePack implementation and a small Engine
layout analyser. The important change in direction is to **prepare useful whole
operations**, simplify their bit mappings before lowering, and select execution
from the resulting applicability and footprint. Fast kernels and composition no
longer require competing caller contracts in the demonstrated family.

This is an implementation starting point, not a graduated Ikea API or a claim
that every format/map is equally fast. Everything here remains Workbench.
The [original hypothesis](README.md), [first runtime screen](runtime-first.md)
and [literature](../layout-analyser/tuplepack-search.md) retain their narrower evidence.

## Current evidence

The final captures use one source snapshot for each host, with AVX2/AVX-512
run sequentially on Zen 5 and NEON on V2. Each of 1,028 cases has three pinned,
sequential repetitions. [Zen evidence](evidence/final-zen5/provenance.json) and
[V2 evidence](evidence/final-v2/provenance.json) retain source/binary identities,
all repetitions and check output in Git; exact case descriptions are
[recoverable from the same captures](evidence/README.md). The final
[ASan/UBSan capture](evidence/final-sanitized/provenance.json) covers the same
kernel/check sources, including the prefilter and direct single-write additions.

Checks include 6,144 runtime schemas/maps, 1,324 dense native writes, 5,120
compound mutations, 384 child-substitution mutations, 512 independent lowering
cases, 516 ordered scan/tail cases, all 16 four-row active masks under two maps,
27,648 single-code width/shift/offset cases, and 100,352 small layout/map/recipe
cases. The analyser's 12 checks independently validate enumeration, features,
ranking and migration arithmetic. These counts describe experimental coverage,
not public format promises.

For the partial-union compound body, contiguous storage, 1,024 rows and one
binding (median CPU ns per mutation):

| Profile | Ordered | Reordered | Normalized / constant control, median across 48 paired cases | Worst ratio |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 9.58 | 18.99 | 0.928× | 1.119× |
| Zen 5 AVX-512 | 3.81 | 5.59 | 0.863× | 1.170× |
| V2 NEON | 12.68 | 23.75 | 0.716× | 0.989× |

The 48 pairs per profile cover both layouts, unions and placements, body/effects
shells, and the declared row/binding configurations. These are the paired-byte
compound family, not every legal code schema. The effects-shell measurements
and individual repetitions remain in the reports; none of these 144 paired
comparisons exceeds 1.4× its constant-control counterpart.

At 1,048,576 rows, the four-row scan's 16-byte plane takes 0.63/0.44/0.96 ns per
row on Zen AVX2/AVX-512/V2 respectively; placing that same projection in 64-byte
rows takes 1.25/1.29/2.01 ns. This is evidence for exposing placement and batching
as separate choices, not an inferred cache-miss count or an all-workload ranking.

The direct single-code writer's final V2 median is about 1.41 ns across candidate
layouts, versus roughly 2.17 ns before removing its byte traversal. The final
unpacked control is about 1.30 ns; its earlier run was 1.37 ns. This comparison
keeps the differing snapshots explicit. The intermediate single-write capture is
[recoverable](evidence/README.md); the earlier mixed-run samples remain in Git.
Larger sparse-write and mixed-plan costs
remain represented in the evidence rather than being hidden by that improvement.

## What the compound experiment established

The operation consumes two native 64-byte code packets, updates 128 codes, and
returns the change in the sum of 32 caller-owned little-endian uint16 values.
Both full and partial unions preserve the correct bits. The same logical
operation runs against ordered/reordered physical layouts, contiguous/split
32-byte regions and one/32 copies of its binding. The bodies share the native
packet interface; no variant gets a more convenient materialized input.

Five strategies remain recoverable in [fusion.cpp](runtime/fusion.cpp): separate
compiled transforms; generic inline composition; AOT constant controls;
composition of runtime bit routes; and normalized byte routing. The last two
prepare transformations outside the hot loop. The AOT case measures known
fixtures, not runtime compilation latency.

The normalization is structural. A restricted algebra records where each
output bit comes from, composes selections/shifts/masks and disjoint OR, then
recognizes byte permutations and rotations. It checks all bits; it does not
recognize a fixture name. Arbitrary bit-route checks and independent random
byte rotations exercise the lowering beyond the timing cases. Wider-value
assembly and summary semantics remain in caller code.

Separating the comparisons matters. Separate/generic compares inner call
boundaries; generic/constants compares constant controls; generic/algebraic
compares route composition with runtime controls. Normalized routing also
changes writer lowering to a direct permutation/shift/mask body. Its gain is
not attributable solely to eliminating decode expansion.

The [captured NEON entry](evidence/fusion-neon-entries.json) is 792 text bytes,
versus 5,112 for the corresponding generic inline entry. Its only stack accesses
save/restore four callee-saved D registers: no payload Q-register spills. The
[captured AVX-512 entry](evidence/fusion-avx512-entries.json) is 498 bytes and has
no stack accesses. These are static code-generation observations, not dynamic
traffic counts. They show a useful code-size/runtime operating point without
requiring a JIT or an explosion of per-layout specializations.

## Applicability belongs to binding

A legal representation may leave the convenient normal form. The adversarial
case swaps two equal-width code descriptors within one child. It needs two
writer contributions per packet per destination byte and assembles semantic
bytes from multiple physical bytes. A decoder fallback alone would be unsafe.

[Ordinary binding](runtime/fusion.cpp) now chooses the normalized operation only
when the byte normal form and both one-round writers apply; otherwise it selects
a complete multi-round general operation. The same owner checks exercise that
fallback. Fast-path applicability is distinct from schema validity. The general fallback
has correctness evidence here, not tuned performance evidence; a real consumer
outside the fast family should drive the next lowering or specialization.

[Child substitution](runtime/composition_check.cpp) resolves independently
supplied child descriptions into the parent operation. One child changes while
the other retains its description, and old/new/general parent bindings coexist
for separately encoded data. The parent assembly and caller operation stay the
same. This is **description substitution followed by flattening**. It does not
claim independently invoked child kernels, arbitrary child extents, or physical
source retirement. Such callbacks are not required on every hot invocation.

## Point access, scan geometry and mutations

The small point experiment uses an eight-byte scalar carrier and seven ordered
read/write maps over four labelled codes of widths 1,7,3,5. Its 28 physical
candidates include all eight two-byte arrangements plus 20 deterministically
sampled three-byte arrangements. Each uses the same count-specialized kernels,
trace, stride and sink. This is a credible comparison within that declared
candidate and recipe set, not an optimal layout search over 128 codes.

The two recipes use a coalesced word load or selected-byte loads; writers issue
only selected-byte stores and preserve all spare bits. A single-code writer
now bypasses physical-byte traversal entirely, with the same direct body for
both recipe labels. It is checked across every width 1..8, legal shift and three
byte positions. The [unpacked control](runtime/point.cpp) uses four byte-sized
codes with the same ordered maps, width checks and spare-bit preservation. It
is not a comparison against every possible typed-struct implementation.

The [scan probe](runtime/scan.cpp) compares one 16-code tuple per native call
with four adjacent tuples per 64-byte packet, using a runtime ordered map and
extraction controls. The same projection occupies a 16-byte plane or the first
16 bytes of 64-byte rows. It checks individual packet coordinates, exact tails,
and prefilter masks whose inactive tuple pointers are null. Inactive rows
produce zero code bytes; the caller retains the active mask separately and must
apply it to downstream predicate results. A zero value does not encode absence.

This establishes one useful batching geometry and its placement tradeoff, not
a general point/scan crossover, every gather shape, or a sparse-prefilter timing
model. The plane/row timing data is a full sequential scan. Point timings use a
repeated random row-ID trace. They are deliberately different workloads.

Mutation obligations are concrete in [the owner model](runtime/composition_check.cpp):
all input packets are admitted before a no-fail before-write hook, issued spans
are recorded before stores, summaries reflect actual merged values, and
publication follows completed effects. Cancellation, stale generations,
insufficient capacity, invalid inputs and overlapping leases reject without
changing this call's data/effects. A suspended request owns its binding, inputs
and leases; completed work is not replayed on a later cancellation.

The fixed compound family writes all 64 bytes, including preserved bits. Its
two qualified spans are fixed by that body. The scalar owner witness derives
coverage from the chosen writer: the same logical map can require a different
number of byte effects after layout substitution, and capacity admission follows
that change. Neither test implements Loom, Orbital or Engine. No immutable
beforeimage is required; in-place mutation remains compatible with Orbital COW.
The timed effects shell includes value admission, pre-write span records and
row-summary updates, with leases/capacity already bound. It excludes acquisition,
publication, page faults and scheduling.

## What the analyser must learn from complete workloads

The [executable analyser](../layout-analyser/tuplepack-reference/README.md) enumerates 2,816 small legal layouts,
ingests measured operation costs, charges preparation against a stated reuse
horizon, and compares structural heuristics with its declared subset optimum.
Its migration arithmetic treats resident preparation as sunk. It is useful
without a learned instruction-cost model or a general search framework.

However, adding isolated operation costs is **not sufficient for final
selection**. The [mixed-plan experiment](runtime/point.cpp) keeps seven bindings
and their call targets live, runs observed 5%/50%/90% write traces, and compares
56 plans: 28 layouts × one uniform word/bytes family. Isolated prediction and
measured selection use exactly that universe and the actual recorded weights.
No arbitrary per-operation hybrid is inferred from these measurements.

The first [V2 mixed run](evidence/mixed-v2/provenance.json) chose plans that were
about 65% behind the measured best in the balanced/write-heavy traces, while
the read-heavy choice was within 0.4%. The [Zen run](evidence/mixed-zen5/provenance.json)
had much smaller selection regret, but still large absolute underprediction of
mixed invocation costs. Branch/control history and indirect targets are plausible
contributors, not PMU-established causes. The final V2 snapshot after the single-write improvement still shows about
44.5% balanced-workload and 14.1% write-heavy selection regret, with the
read-heavy choice within 0.1%. Thus improving one family does not restore the
additive selection assumption. The counterexample remains useful even when a
later kernel change improves the timings.

A workable first analyser therefore proposes a small diverse set of legal
layout/operation candidates, then probes complete bound candidates under a
representative workload before choosing. Isolated costs are evidence for
proposals and diagnosis. Equal co-access sets, structural features or near-equal
isolated costs do not justify pruning alternatives. Close measured candidates
should remain alternatives rather than be declared permanent winners.

Preparation, code/control working set, bytes moved and expected future reuse
belong in that choice. Current compound preparation deliberately builds generic
plans and both lowering alternatives together; it does not measure the
incremental price of normalization. The 32-binding case uses different control
addresses with identical maps, not diverse generated code. These limits guide
implementation and further sensing; they do not require a new research framework
before writing the module.

## Provisional implementation shape

Keep the physical description independent of presets, native controls and
Engine's semantic schema. A description identifies code widths, byte offsets,
bit positions, extent and a versioned format; derived function pointers and
shuffle controls are rebuilt when binding. Engine owns field decomposition,
layout choice, per-segment representation identity and migration. Different
segments can retain different representations indefinitely.

A small implementation can expose three distinguishable surfaces:

- Ordinary prepared readers/writers: ordered maps, scalar/native shape, checked
  admission and a bound invocation. Read duplication and zero holes are useful;
  writers reject duplicate destinations and out-of-width selected values.
- Composition authoring: inline-friendly ISA bodies plus descriptions sufficient
  to compose byte mappings, resolve child coordinates, select a specialized body
  or retain a correct fallback. The restricted bit algebra is one mechanism,
  not an Engine expression IR or a mandatory representation of every transform.
- Owner integration: exact read/write requirements, admission/effect capacity,
  borrowed lifetimes, resumable state and completion/publication obligations.
  Keep these around kernels instead of embedding them in their arithmetic.

A native operation remains logically the same under manual fusion, inlining or
a compiled pipeline. The existing [ABI probe](initial.md#carrier-feasibility)
shows why AVX2's default SysV two-vector struct return cannot simply be assumed
register-only; the measured native path uses Clang regcall. Metadata stays out
of native payload aggregates. CPS construction itself is not benchmarked here;
Ikea's established straight-through composition machinery is the starting point,
not another VM to invent for TuplePack.

Construction initializes a caller-provided extent under explicit spare-bit
semantics; replacement preserves unselected bits. Multiple packets or child units
need one operation-level admission before any mutation. A row larger than one
unit remains a composition of units and placements. Segment row limits belong
to the Engine-facing adapter; standalone tuple arrays need not inherit them.

Engine's analyser can start with explicit candidate descriptions, workload
maps, executable bindings and versioned probe observations. Candidate proposals
can exploit complementary widths, edges, locality and co-access, while comparing
real recipes. Preserve the ability to improve a resident plan or replace a child
without changing the container-level semantics. Analysis cadence, future-reuse
prediction and migration priority remain owner policies.

## Deliberate limits and further experiments

Implementation need not wait for JITs, SVE/SVE2 kernels, exhaustive large-schema
search, distribution-dependent patching or a globally optimal migration policy.
Those remain research options. Neither the small enumerator nor the hand-picked
compound examples should become the definition of TuplePack.

Work that belongs with implementation includes curating backend-specific prepared
controls (the first generic plans intentionally retain multiple ISA formats),
covering more native output shapes, documenting checked/trusted entry points,
and measuring the finished ordinary adapters. Source/ABI conveniences for
caller-owned struct assembly also need a deliberate small public surface.

Further research should follow concrete consumers: an independently authored
child with a different native body/footprint; diverse live maps; remaining sparse
native writes; compound predicates using active masks; and realistic mixed
transaction/scan traces with packed allocation sizes, dependent lookup latency
and contention between writers. Owner exclusion/serialization must cover the
issued physical bytes, including neighbors preserved by a read-modify-write. None of the present timing
controls establishes universal superiority over ordinary structs or layouts.

## Reproduce and inspect

[Run instructions](runtime/README.md) describe source capture and pinned,
sequential CPU measurements. `--filter '^(fusion|scan|small|mixture)/'` selects this
round. `--check-extra` on the benchmark runs its scalar/scan checks without timing;
`tuple_runtime_check` owns general maps, compound mutation and child substitution.

```sh
python3 workbench/spikes/tuple-layout/runtime/report.py \
  workbench/spikes/tuple-layout/evidence/final-zen5 \
  workbench/spikes/tuple-layout/evidence/final-v2 \
  --cost-output build/layout-analyser/tuplepack-reference/measured
python3 workbench/spikes/tuple-layout/runtime/mixed_report.py \
  workbench/spikes/tuple-layout/evidence/final-zen5 \
  workbench/spikes/tuple-layout/evidence/final-v2
```

The full measured sources, binaries and build receipts accompany each retained
run. Later validation and specializations have their own captures; historical
measurements are not relabeled as observations of a newer source tree. Raw
repetitions remain in compact evidence; the reports normalize each case by its
own item count. See the analyser guide for importing those costs and reproducing
horizon-sensitive rankings. Synthetic analyser fixtures remain clearly separate
from measured kernel evidence.
