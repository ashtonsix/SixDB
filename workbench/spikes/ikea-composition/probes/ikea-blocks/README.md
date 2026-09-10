# Ikea bitset primitives

Experimental evidence owned by the [composition investigation](../../README.md).
Use its [current design](../../design.md) for authoring direction.

Restarted 2026-09-09 after the initial implementation was rejected. Its code
and retained evidence have been removed. Correctness of a materialized API
did not answer the [composition investigation](../../README.md),
and unmeasured scalar codecs were the wrong performance baseline.

The exercise must deliver performance and composition together. Headless
plain bitsets and **Bec256** (Bisection–Enumerative Code) cover
the same 256 positions. The latter uses a bisection tree of populations and
enumerative byte codes; external cardinality is part of interpreting its bytes.
Neither stores a size or population header. Compressed decode–operate–encode
algebra is explicitly out of scope.

The current work compares actual ABI handoffs, trusted SIMD codecs against
Calico's fast arms, and a separate analyser-facing compressed-size estimate.
Compute is free-standing; repetition and physical layout are separate from
native execution width. Validation belongs to admission/wrappers and its
lifetime must be explicit. A codec inner body does not perform recoverable
error handling.

The naming workshop considered BEC (Bisection–Enumerative Code), BPEC
(Binary Partition Enumerative Code), PTEC (Population-Tree Enumerative Code)
and TEC (Tree Enumerative Code). BEC keeps both defining mechanisms while
remaining short; `Bec256` distinguishes this format. The name was accepted on
2026-09-09. It is not a new claim of algorithmic invention.

The [heterogeneous follow-up](../ikea-heterogeneous/README.md) now reuses these
native bodies with integer-packed metadata and dependent body addresses. Its
`run.py --check-only` checks both providers and the combined caller; the reuse
and admission contract stays in that study.

## The boundary is part of the primitive

The [ABI experiment](abi/README.md) inspects independent producer, caller and
continuation TUs with Clang 21.1.8 and libstdc++ 15. Ordinary x86 System V and
AAPCS64 calls return the tested wide `std::expected` and scalar/vector structs
through memory. For the successful AVX-512 expected control this introduces
a 64-byte payload store and reload plus status traffic. The scalar expected
control returns in registers. x86 `__regcall` returns the trivial mixed
scalar/vector control in registers, but the tested wide expected types still
use memory. Tiny known-success inline controls erase expected completely;
dynamic validation does not disappear, and this is no evidence that a full
codec will always inline.

Passing scalar state and **native vector arguments separately** to an immediate
continuation works with the ordinary ABI: YMM/ZMM on x86 and multiple Q
arguments on AArch64. Oversized compiler vectors on ARM are not equivalent to
multiple native arguments. Git retains illustrative complete functions and
compact LLVM IR signatures; the S3 bundle contains the full caller-side traffic,
disassembly and IR. The findings come from those instructions, not type size.

The codec bodies in [native_neon.h](native_neon.h) and
[native_avx512.h](native_avx512.h) therefore use native operands and plain scalar
state. [native.cpp](native.cpp) contains explicitly materializing endpoints
for comparisons and callers whose destination belongs in memory. These are
different boundaries; neither introduces an expected result or validates in
the hot path. [reference.cpp](reference.cpp) owns the cold bounded validator
and independent bit-at-a-time oracle.

## BEC256 byte contract

This format is compatible with Calico's partial-cell enum body, implemented
afresh. We additionally define zero-byte empty/full bodies, where Calico's
outer kind normally handles those cases. BEC is expanded here as
**Bisection–Enumerative Code**; the acronym also has unrelated uses, including
binary erasure channel. The accepted name makes no originality claim.

- Position `i` is bit `i % 8` in byte `i / 8`. All fields are emitted least
  significant bit first, breadth-first through five equal bisections.
- A parent population `P` covering `2H` positions encodes its left population
  `L` as `L - max(0, P-H)`, in `bit_width(min(P, 2H-P))` bits. The right
  population follows by subtraction. The root population is external.
- The resulting 32 byte populations select enumerative ranks of byte values
  in ascending numeric order. Population 0..8 has rank width
  `[0,3,5,6,7,6,5,3,0]`. The final partial byte has zero padding bits.
- The maximum is 150 tree bits plus 224 rank bits: **374 bits / 47 bytes**.
  There is no embedded population, byte length, or raw 32-byte escape.

Trusted encode requires 32 readable input bytes, the correct population and
64 writable output bytes. Its scalar return is the logical byte count; stores
may extend beyond it. Trusted decode requires **64 readable bytes** even for
a short or empty body, a valid body/population association, and 32 writable
output bytes. The two-stream body takes two independent such inputs. Input
and output ranges must be disjoint. Allocation slack is a caller/admission
contract, not a header in the format.

The cold validator accepts an exact-length span without slack, validates
split/rank bounds, canonical padding and exact framing, and reports a scalar
error plus bit count. It cannot authenticate a root population against the
intended original data. A containing interface must retain the admitted bytes,
root association and readable extent for the entire trusted use; this spike
does not supply a production storage owner for compressed bodies.

## Physical definition, native width and repetition

[blocks.h](blocks.h) contains data definitions only: `PlainBits<Positions>`,
`Tiles<Child, Count>` and a `Bec256` format identity. A 512-position native AND
or OR accepts two native carriers regardless of the physical type from which
they were loaded. No Pair TU or codec per storage size is required.

[tiling.h](tiling.h) demonstrates a contiguous placement adapter over the same
ordered positions, using native grains and a short word remainder. Repetitions
up to four expand statically; larger extents use one loop. Checks cover 64,
256, 512, 4096 and 65536 positions, flat and tiled storage, union/intersection
and exact in-place output. Arbitrary partial overlap is unsupported. This is
an execution/traversal example, not a decision that every physical composition
must be contiguous or word-aligned.

The AVX-512 decoder also operates on two **independent** compressed streams
and produces one native 512-position carrier. It does not require adjacency,
invent a Pair storage type, or combine their independent root populations.
Compressed decode–operate–encode union/intersection has been removed from
scope; the implemented set algebra is plain bitwise algebra only.

## A useful composition to expose costs

The [composition probe](composition/README.md) makes an analyser sum predicted
BEC sizes over attached 256-position tiles. It implements the two authoring
spellings from the Ikea investigation, records repetition and reduction once,
changes contiguous access to a stride-48 placement, and applies a local
load/feature fusion. Its native feature bodies are shared by inline execution,
opaque scalar endpoints and separately compiled continuation stages.

The first binder incorrectly recognized entire recipes. The revised lowerer
walks typed dependencies and emits reusable stages into an owned runtime
program; a newly authored transition estimator and reordered node storage
exercise that distinction. It explicitly rejects graph shapes whose live
values it cannot carry, including one value consumed twice. That is a limit
of this carrier allocation: branching data dependencies can still execute
straight through without branching control flow or buffering. The selected
inline executor only accepts its known compiled recipe. See the independent
[composition review](../../predictor-review.md).

This reveals limits rather than settling an interface: A/B differ here in
packaging and child discovery; both use the same operation vocabulary. An
opaque source-child marker is not deep physical-child substitution. Auxiliary
sources, prefilters, mutation, suspension and general live-value allocation
remain untested. The common continuation signature also keeps a vector live
through scalar stages that no longer need it. Dispatch and that register
pressure must be measured, even when the payload never spills.
The [value-reuse sketches](../../value-reuse-sketches.md) identify
the next unresolved question: comparing two estimates of the same tile with
either explicit bounded carrier roles or an inline region behind a reusable
continuation stage. Neither option has been implemented by this probe.

## Predicting size is a separate operator

The [predictor study](predictor/README.md) fits fixed Q12 piecewise linear
models against the shared Real Roaring and MS MARCO inputs. It preserves
whole-family train/validation/test separation, sampling weights, exact targets,
frozen coefficients and a separate structural holdout. Its scalar byte result
is an estimate, not an allocation bound or an encoder early-exit decision.

The cheap control uses population distance and enumerative cost. Adding
complement-symmetric transitions improves structural coverage; quarter
population dispersion improves it further. A linear mixture of the existing
features alone would not increase a linear model's expressive power.

The intended run feature was clarified to `pop(bits & ~(bits << 1))` after
the initial adjacent-11 comparison. A separate corrected comparison fits that
one-run count with the same selected regimes and development weights, leaving
the frozen models unchanged. Its accuracy is effectively tied with internal
transitions (real test MAE 0.180 versus 0.179 bytes; structure 1.216 versus
1.211). The earlier adjacent-11 result is not attributed to the intended
one-run feature. The current native timings below cover transitions;
one-run extraction cost has not been measured separately.

| Candidate | Partial real test MAE / p95 / max (bytes) | Fresh structure holdout MAE / p95 / max |
| --- | --- | --- |
| Distance + enum cost | 0.185 / 1 / 5 | 1.971 / 7 / 14 |
| Add transitions | 0.179 / 1 / 5 | 1.211 / 4 / 7 |
| Add quarter dispersion | 0.145 / 1 / 3 | 0.779 / 2 / 6 |

Sixteen full and sixteen empty bytes can require 1 or 18 encoded bytes after
permutation, while the cheap control always predicts 4. Natural-corpus MAE
alone would have concealed this failure. All three operators remain explicit
experimental alternatives; no automatic model-selection or compression policy
has been adopted.

## Validation and reproducibility

`check.cpp` compares fresh native codecs with the independent oracle and pinned
Calico fast implementations. It covers 70,417 codec cases: all singleton and
single-hole positions, exhaustive embedded
16-bit patterns crossing the middle boundary, randomized sets at every
population and runs at every population. It checks canonical bytes, returned
extent, round trips, two independently paired streams, malformed framing,
padding and guard-page readable bounds. Composition adds 198,150 feature cases
and 162 graph/layout/execution combinations. Counts are correctness evidence,
not performance measurements. The retained Neoverse run precedes the added
512 singleton/hole cases and reports 69,905; its NEON implementation is unchanged.

```sh
orb -m ubuntu python3 workbench/spikes/ikea-composition/probes/ikea-blocks/run.py --sanitize --check-only
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/ikea-composition/probes/ikea-blocks/cloud.sh --machine zen5 --capacity on-demand \
  --detach -- --target zen5
```

Other hardware targets are `granite-rapids` and `neoverse-v2`. The runner uses
captured sources, the pinned Linux compiler, independent incremental TUs and
shared prepared inputs. Benchmarks require one pinned CPU; cases and five
repetitions execute sequentially. Codec comparisons call opaque endpoints with
the same padded input/output contracts; the x86 suite includes Calico's faster
P2 AVX-512 encoder and its two-stream AVX-512 decoder, not just AVX2 controls.

The retained exports were reduced after closeout: useful timing repetitions
remain, full disassembly stays in the original S3 bundles, and the predictor
correction references unchanged initial outputs. This changes the selection,
not the measured runs or their source identities. `assembly.py` recreates the
smaller excerpts from recovered runs; `report.py` regenerates timing comparisons
offline. Commands that compile, train or benchmark again are separate experiments.

Natural timing inputs sample up to 64 windows per named corpus and 16 tiles
per sampled window. Both all tiles and partial-only views are retained;
partial cases exclude empty/full cells that normally use Calico's outer kinds.
Some partial views are tiny and this sample is not a measured SixDB workload.
Synthetic fixed-population random sets and runs are separate. These are hot
resident microbenchmarks in nanoseconds per 256-position cell; they do not
measure a full analyser, cache-cold traversal, compression policy or throughput
under concurrent load. Two-stream rows divide call cost by two.

## Hardware findings, 2026-09-09

The selected resident matrix **meets or beats the matched Calico baseline in
every tested partial/synthetic case on all three targets**. This statement is
about these calls and inputs, not all workloads. Each ratio below is the
median of the 30 per-case ratios (28 named partial-corpus views plus two
synthetic shapes); the maximum exposes the worst case rather than hiding it
inside a combined throughput score. Each individual time first takes the
median of five sequential repetitions. Lower is better.

| Operation / matched prior | Zen 5 median / max | Granite Rapids median / max | Neoverse V2 median / max |
| --- | --- | --- | --- |
| Encode / Calico P2 AVX-512 on x86, NEON on ARM | 0.596 / 0.790 | 0.686 / 0.711 | 0.769 / 0.795 |
| Single decode / Calico AVX2 on x86, NEON on ARM | 0.548 / 0.551 | 0.616 / 0.620 | 0.984 / 0.986 |
| Two-stream decode / Calico AVX-512, per cell | 0.929 / 0.935 | 0.693 / 0.695 | — |

The uniform-population synthetic case gives the following absolute costs.
These include opaque call and materializing endpoint costs, with identical
loop/checksum machinery for each matched pair. Our single decoder also returns
the consumed bit extent; Calico's comparison endpoint returns only the payload
through its destination. The x86 P2 encoder computes its own root population;
our headless encoder receives the externally associated population.

| Nanoseconds per cell, Bec / Calico | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | --- | --- | --- |
| Encode | 13.49 / 23.39 | 24.48 / 35.57 | 25.11 / 32.65 |
| Single decode | 43.70 / 79.97 | 48.91 / 79.47 | 83.60 / 84.92 |
| Two-stream decode | 34.16 / 36.78 | 29.80 / 43.00 | — |

The initial restarted two-stream decoder lost on Zen 5. Restricting tree reads
to their proven 150-bit extent allows a single 64-byte permutation domain for
the two trees; prefix computation now visits only 2/4/8/16 active nodes per
domain at successive levels. NEON tree gathers similarly need only the first
32 input bytes, while enumerative decoding retains the full readable extent.
The x86 encoder also directly emits singleton/single-hole codes. These are
algorithmic specializations under the trusted contract, not validation branches.
The final kernel was measured against the fastest pinned prior arms after
these changes; passing round trips alone was not used to establish speed.

Composition has a measurable cost even with native payloads in registers.
The following are median **inline loop** costs over the composition benchmark's
4096-tile synthetic mixture, contiguous stride 32. They include extraction,
model evaluation where named, and scalar reduction. They are not timings on
the model-training sample and are not standalone scalar function-call latency.

| Nanoseconds per 256-position tile | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | --- | --- | --- |
| Two-feature estimate, inline authored analysis | 2.910 | 3.879 | 3.599 |
| Same estimate, separate CPS stages | 3.480 | 4.454 | 4.338 |
| Same estimate, CPS load/features fused | 3.337 | 4.448 | 4.223 |
| Transition estimate, inline loop | 3.776 | 5.431 | 5.005 |
| Quarter-dispersion estimate, inline loop | 4.880 | 8.108 | 6.792 |
| Two features only, 256-position grain | 0.810 | 1.807 | 2.152 |
| Same two features, 512-position grain, per tile | 1.147 | 2.586 | 2.152 |

A and B's selected inline spellings are effectively tied here. Separate CPS
adds about 15–21% to this short analysis. The local load/feature fusion saves
one indirect jump, but achieves little on Granite Rapids in this probe.
Simply widening feature extraction to 512 positions is **slower** on both x86
targets; native width must remain a measured execution choice rather than a
physical type requirement. Both contiguous and stride-48 repetitions remain
in the compact CSVs. Stride 48 here is a placement test, not a selected metadata
layout or evidence about SoA versus interleaved metadata costs.

The two-feature estimate is the inexpensive control, not a robust choice for
every structured bitset. The quarter model buys substantially better structure
coverage at roughly 1.7–2.1 times the inline predictor cost in this mixture.
Transitions sit between them. We have not measured the downstream analyser's
decision value or chosen a compression policy, and these estimates must never
stand in for safe output capacity.

Selected reproducible evidence:

- [Zen 5](evidence/20260909-zen5): c8a.medium, `-march=znver5`, 70,417 codec checks.
- [Granite Rapids](evidence/20260909-granite-rapids): c8i.large with one visible
  CPU, `-march=graniterapids`, 70,417 codec checks.
- [Neoverse V2](evidence/20260909-neoverse-v2): c8g.medium,
  `-mcpu=neoverse-v2`, 69,905 codec checks; same final NEON body.
- [Native ARM ASan/UBSan](evidence/20260909-native-sanitize): 70,417 codec checks
  plus the final composition checks. This is correctness evidence only.

Every hardware run also passes the 198,150 feature cases and 162 composition
checks. Artifact references recover source archives, actual binaries,
disassembly and commands. Later documentation and the singleton helper's
move from `tables.h` to `pack.h` do not change measured compute; a pinned
`-O3 -march=znver5` build before/after that move produced byte-identical native
objects. No coefficients or native feature layouts changed after timing.
The separate ABI and model directories retain their own experiments.

```sh
orb -m ubuntu python3 workbench/spikes/ikea-composition/probes/ikea-blocks/report.py \
  workbench/spikes/ikea-composition/probes/ikea-blocks/evidence/20260909-zen5 \
  workbench/spikes/ikea-composition/probes/ikea-blocks/evidence/20260909-granite-rapids \
  workbench/spikes/ikea-composition/probes/ikea-blocks/evidence/20260909-neoverse-v2 \
  --csv build/ikea-bec-comparisons.csv
```

`report.py` verifies retained compact hashes before reading them and preserves
every per-case comparison, including the all-tile views. Calico source identity
and dependency hashes are pinned in [prior-input.json](prior-input.json).
