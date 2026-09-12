# GPR packets: motivation and limits

An eight-byte packet has a useful role for small bounded requests, scalar
consumers and shared mutation admission. The evidence supports a small scalar
composition path. It does **not** support filling out the API matrix merely for
symmetry, choosing eight-byte packets by default, or requiring GPR-only arithmetic
inside their kernels. AVX-512 can win even when the final result is a GPR.

The [probe](README.md) leaves Ikea unchanged. These are observations from a
provisional lowering, not production guarantees or a completed API proposal.

## What the measurements establish

Each profile completes 5,313 cases with three pinned sequential repetitions,
Clang 21.1.8, Release, no LTO, and independent bit-level checks of every actual
competitor. Results are median CPU nanoseconds per original row, including
inactive rows, over 1,024 resident rows. They do not establish cold-memory,
contention, plan-working-set or database performance.

Two requested rows each projecting four codes are the motivating shape. Across
46 random-access cases per consumer, the inline GPR/SIMD time ratios are:

| Consumer | Zen AVX2 | Zen AVX-512 | Neoverse V2 |
| --- | ---: | ---: | ---: |
| Materialized-word scalar arithmetic | 0.72 | 1.10 | 0.43 |
| Natural byte sum | 0.87 | 1.14 | 0.53 |
| Checked read/update with effects | 0.84 | 0.90 | 0.70 |

Below one favors GPR. This is an unweighted parameter grid, not an application
frequency distribution. Masks, map ordering and stride matter. AVX-512 has 15 of
46 word-read comparisons where GPR is over 1.4× slower, despite the GPR result
required by that consumer. GPR is an execution choice, not a superiority claim.

### A real favorable case

For two adjacent four-byte tuples, with four ordered seven-bit codes per row and
a common zero shift, the complete physical window fits one word. These are random
requests for pairs; both rows are active. Each cell is GPR / stronger inline point
control / current SIMD with the same two requested rows:

| Consumer | Zen AVX2 | Zen AVX-512 | Neoverse V2 |
| --- | ---: | ---: | ---: |
| Materialized word | 0.243 / 0.462 / 1.596 | 0.254 / 0.520 / 0.843 | 0.438 / 0.651 / 2.751 |
| Checked update | 1.156 / 2.989 / 4.166 | 1.214 / 3.016 / 3.315 | 2.018 / 4.458 / 7.747 |

The improvement survives checked width/range/capacity admission and byte effects.
It is not just a cheap inner decode. The compiled GPR update takes
1.739 / 1.731 / 2.750 ns respectively: some benefit survives a real call boundary.
This is a structural consecutive-byte/common-shift lowering, not recognition of
these particular code ranks or benchmark values.

### Much of the read benefit also belongs to points

The stronger `gpr_points` comparator uses the same provisional scalar lowering
one row at a time, then combines its results. Across the two-row random cases,
the packet/stronger-point median ratios for materialized-word reads are
0.98 / 0.96 / 1.02 on AVX2 / AVX-512 / V2. For sums they are 1.02 / 1.00 / 1.00.
Batching itself usually adds little to these reads. A scalar implementation should
share its useful lowering with points rather than strand it behind a batch API.

Shared admission matters more as the requested row count rises. For eight-row
updates, the packet/stronger-point median ratios are 0.61 / 0.56 / 0.46.
Repeated point calls do less whole-packet preflight but repeat their per-call
checks and effect handling. The packet retains whole-call admission.

Compiled GPR reads do not get the same result as inline ones. For two-row
materialized-word reads, their median ratios against current SIMD are
0.79 / 1.30 / 0.74. A register-valued return does not remove call overhead or make
all control traversal inexpensive. CPS was not measured here.

### Counterexamples that must guide implementation

Reversed mixed-shift codes from two 16-byte tuples with stride 64 favor AVX-512:
GPR word reads cost **1.941 ns**, versus **0.995 ns** for SIMD, including its
compaction into a GPR. Their checked updates cost **6.476 versus 4.917 ns**.
For four spread codes in tightly placed 16-byte tuples, GPR update costs
**10.370 versus 3.431 ns**. General scalar bit assembly is not a replacement for
byte permutations and SIMD shifts. That update comparison also includes a
footprint tradeoff: GPR writes four separated selected bytes per row; SIMD can
reissue the complete 16-byte window, preserving its unselected bytes. Across the
two tight rows, the journal consequently records seven selected-byte spans versus
one 32-byte span. Both obey their issued-byte contracts, but their coverage and
owner exclusion requirements differ. The timing does not isolate instruction
choice from effect fragmentation. ARM also has losing scattered updates.

For scans, the correct alternative is often to fill the SIMD packet. Reading
one code from tightly packed one-byte tuples and summing it gives:

| Profile | Eight-row GPR | Eight-row SIMD | 64-row SIMD |
| --- | ---: | ---: | ---: |
| Zen AVX2 | 0.146 | 0.376 | 0.032 |
| Zen AVX-512 | 0.152 | 0.235 | 0.029 |
| Neoverse V2 | 0.200 | 0.842 | 0.110 |

The corresponding checked updates are 0.358 / 0.847 / 0.097,
0.381 / 0.739 / 0.092 and 0.533 / 1.746 / 0.223 ns. Fuller SIMD is faster here;
its larger request is not a substitute for an isolated small operation.

## Consequences for scope

Keep packet shape, native carrier and internal instruction selection distinct.
A future `reader<8, Rows>` / `writer<8, Rows>` can provide one GPR at composition
boundaries without promising that every lowering uses only scalar instructions.
The useful scope is a small family sharing point bodies, a bounded word-transfer
fast path and the existing admission/effect/observation shell. The evidence does
not justify another general routing optimizer or a proliferation of scalar policies.

The public symmetry alone is insufficient motivation. The concrete reasons are
small-request materialization, coalescing an owned word where possible, and
amortizing mutation admission while retaining whole-packet failure semantics.
Layout- and ISA-dependent alternatives must remain available. Production CPS,
substitution and lifetime checks still belong to any subsequent implementation;
this probe does not claim to have supplied them.

## Evidence and reproduction

Selected complete families retain both the favorable four-byte units and mixed
16-byte units for the two-row question, plus one-byte/eight-row cases and their
fuller SIMD controls. All methods, masks, consumers and repetitions survive the
selection. Full-grid counts, medians and best/worst case names are in each
`coverage.json`, with the original sample hash:

- [Zen AVX2 cases](evidence/zen-avx2/cases.csv), [coverage](evidence/zen-avx2/coverage.json),
  [recovery](evidence/zen-avx2/artifact.json).
- [Zen AVX-512 cases](evidence/zen-avx512/cases.csv), [coverage](evidence/zen-avx512/coverage.json),
  [recovery](evidence/zen-avx512/artifact.json).
- [Neoverse V2 cases](evidence/v2/cases.csv), [coverage](evidence/v2/coverage.json),
  [recovery](evidence/v2/artifact.json).

`analyze.py JOB_DIRECTORY... --output REPORT.json` regenerates full-grid comparisons
from collected samples. For selected case exports, use `evidence.py summarize`
with filter `^gpr/(2/unit(4|16)/|8/unit1/)` and retain `items_per_iteration`,
`rows_per_call`, `drawn_codes` and `stride`. The export provenance records its inputs.

The AVX2 profile completed all 5,313 cases and the script entered AVX-512
configuration before a Spot interruption. Only its completed AVX2 profile is
used. AVX-512 was rerun on on-demand capacity; V2 completed on Spot. The production
Ikea and probe C++/build-source bytes are identical across these retained captures.

The initial monolithic V2 benchmark TU was killed at 1,519,164 KiB peak RSS after
1m23.40s of selected-target build work; its [failure archive](evidence/compile-failure-v2.json)
remains recoverable. Sharding by row shape and pruning unreachable shape/count
combinations reduced the successful run's peak to 720,756 KiB (1m43.40s elapsed).
That elapsed time includes a different cache state and is not a paired build-time
comparison. Resource and text-size witnesses are in each `compilation.json`.
The teaching prototype remains uncurated: its size is not the projected size of
a maintained Ikea implementation.
