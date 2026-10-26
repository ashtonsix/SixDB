# Masked-loop assembly and timing (neutral-contribution diagnostic)

The question is whether a masked-out row contributing the identity (0 for
SUM, UINT64_MAX for MIN, 0 for MAX) inside a dense loop beats visiting the
selected rows.

[`mask/kernels.cpp`](../mask/kernels.cpp) holds the Frame fallback
formulations as out-of-line functions over one decoded 256-row block. It
depends only on `<cstdint>`, so the same source cross-compiles. The rejected
Frame attempts are preserved verbatim on branch `spike/frame-seam-mask`:
the masked SUM is 8d1955d8 (reverted in 22a2b47f) and the masked summary is
d84f4ac2 (reverted in 6985390d). [`mask/run.sh`](../mask/run.sh) regenerates
everything here with the spike flags (`-O3`, function/block alignment, no
fast-math). The files are:

- `kernels-neon.s`, from `-march=armv8-a+simd`;
- `kernels-avx2.s`, from `--target=x86_64-linux-gnu -march=x86-64-v3`
  (assembly only, not timed);
- `remarks-*.txt`, the loop-vectoriser remarks.

## Kernels

| Function | What it is | Vectorised (NEON / AVX2) |
| --- | --- | --- |
| `sum_bitwalk` | kept Frame fallback: `rows_of(b).for_each`, u128 add per selected row | no (uncountable loop) |
| `sum_rejected_and` | rejected attempt, verbatim: `keep = 0 - bit`, AND, u64 low/high halves, runtime `k` trip | yes, width 2 / 4 |
| `sum_fixed_and` | same with a fixed 64-row trip and the word hoisted | yes, 2 / 4 |
| `sum_expand` | expand the mask to lanes first, then a dense AND-and-sum | yes, 2 / 4 |
| `sum_adaptive` | `fixed_and` above 40% selected, else the bit walk (one popcount per block) | as its parts |
| `sum_dense` | unmasked whole-block sum (kept `block_sum`) | yes, 2 / 4 |
| `summ_bitwalk` | kept Frame fallback for SUM/COUNT/MIN/MAX | no |
| `summ_rejected` | rejected attempt, verbatim: halves × 0/1 bit, MIN/MAX as `on && v < mn ? v : mn` | **no**: the guarded select is not recognised as a reduction |
| `summ_identity` | neutral identities: `v & keep` for sum/max, `v \| ~keep` for min, unconditional `umin`/`umax` | yes, 2 / 4 |
| `summ_adaptive` | `identity` above 50% selected, else the bit walk | as its parts |

## Findings

**The rejected SUM did vectorise.** NEON at width 2 and AVX2 at width 4 both
keep the exact sum out of any per-row u128 carry chain: halves accumulate
in u64 with one fold per block. Its cost is flat, about 0.35 ns/row on NEON
whatever the density. The NEON inner loop spends a `ushl` (variable shift)
plus an `and` per two lanes expanding the bit. With only two u64 lanes per
op, that makes it about 2.1× the unmasked loop (0.17 ns/row).

**The bit walk costs about 0.88 ns per selected row.** The two cross near
40% selected. The Frame experiment measured period-3 partial blocks (33%),
where the bit walk does less work (0.20 vs 0.35 ns/row), so it lost 1.2×.
That loss is a density effect, not missing SIMD.

**The rejected summary did not vectorise.** The guarded MIN/MAX
`on && v < mn ? v : mn` stops the vectoriser on both ISAs. That loss tests
a scalar formulation. Written with identity elements, the summary vectorises
at 0.64 ns/row flat (unmasked: 0.37) and crosses the bit walk near 50%.

**Adaptive selection gets both.** Choosing per block from its popcount
tracks the better form at every density, at the cost of one popcount.

**AVX2 expands the mask more cheaply.** Per four lanes it uses
`vpbroadcastq`, `vpsllvq`, `vpcmpgtq` and `vpand`, so its crossover should
be lower than NEON's. AVX-512 would use a mask register directly. Neither
x86 profile is timed here; Codex's native capture is the place to find
x86's crossovers.

Local VM (aarch64 NEON) times, ns per row over 65,536 random u64 rows, median
of 11 samples:

| Kernel | all | random 3/4 | half clustered | period 3 | sparse 1/32 | none |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| sum_bitwalk | 0.884 | 0.719 | 0.431 | 0.199 | 0.036 | 0.014 |
| sum_rejected_and | 0.355 | 0.349 | 0.346 | 0.350 | 0.359 | 0.353 |
| sum_fixed_and | 0.349 | 0.349 | 0.346 | 0.359 | 0.347 | 0.351 |
| sum_expand | 0.473 | 0.463 | 0.461 | 0.458 | 0.473 | 0.465 |
| sum_adaptive | 0.361 | 0.351 | 0.359 | 0.209 | 0.043 | 0.024 |
| sum_dense (unmasked) | 0.164 | 0.165 | 0.163 | 0.161 | 0.162 | 0.167 |
| summ_bitwalk | 1.213 | 0.953 | 0.606 | 0.380 | 0.051 | 0.014 |
| summ_rejected | 1.051 | 1.063 | 1.052 | 1.048 | 1.070 | 1.066 |
| summ_identity | 0.644 | 0.652 | 0.645 | 0.637 | 0.635 | 0.638 |
| summ_adaptive | 0.651 | 0.655 | 0.626 | 0.384 | 0.059 | 0.024 |
| summ_dense (unmasked) | 0.374 | 0.363 | 0.363 | 0.369 | 0.361 | 0.367 |

## Consequence for Frame

The principle holds, but the selection belongs to the leaf kernel or to a
density-adaptive fallback, not to an always-on dense loop. Only density in
the middle (above about 40%) favours neutral SUM on NEON, and a two-lane u64
register makes the mask expansion relatively expensive.

For Frame's decode fallback:

- `summ_identity` is the right summary form: unlike the rejected select form,
  it vectorises.
- `sum_adaptive` and `summ_adaptive` are the candidates for partially
  selected blocks.
- None of these changes leaf kernels, so the shared SeriesPack archives stay
  identical.

## AVX2 exact-halves leaf repair

`exact_halves_regions` in `ikea/include/ikea/seriespack/author/expression.h`
reaches u128 masked sums from width 57 and weighted sums from width 49. Its
non-AVX-512, non-AArch64 arm stored each 16-row region and branched per
selected bit into a u128 add. The new `__AVX2__` arm follows the AVX-512 arm
and leaves NEON and AVX-512 unchanged:

- It widens the region to 4 × 4 u64 lanes and turns each lane's selection
  bit into an all-ones or zero lane.
- It adds the low and high 32-bit halves into u64 lanes, multiplying by
  `mul_epu32` when weighted.
- It folds to u128 every 2^20 rows. Per lane that is at most 2^18 rows ×
  2^40, so the lanes cannot overflow.

The disassembly comes from the [leaf driver](../leaf/run.sh) built for
x86-64-v3:

- `exact-halves-avx2-K64-masked-{old,current}.s` (width 64, masked,
  unweighted). The old body is a `testb` per row bit, each guarding a scalar
  `add`/`adc` into the u128 in memory. The current body broadcasts the
  region bits, then per four lanes does `vpsllvq` and `vpcmpgtq` (the
  selection bit into the sign, then a lane mask), `vpand`, `vpblendd` (low
  halves), `vpsrlq` (high halves) and `vpaddq`, with no per-row branch. The
  lo/hi accumulators are still spilled across the region callback in this
  build; native timing will show whether that matters.
- `exact-halves-avx2-K49-weighted-{old,current}.s` (width 49, weighted): 4
  `vpmuludq` against 0, and 13 scalar `mul`/`imul` (edges) against 31.

The [leaf driver](../leaf/exact_halves.cpp) is header-only through the typed
reader. It links only SeriesPack's `admission.cpp` and core's `overlap.cpp`,
which hold no read kernels. `run.sh PROFILE` builds it twice from one source:
`current`, and `old` with `leaf/old-include/` (the 38695dc4 `expression.h`)
first on the include path. It then requires identical results.

The cases are widths 49, 56, 57 and 64. Values are random with runs of the
maximum, so both u128 halves are exercised. Each case runs unweighted,
weighted at periods 16 and 64, and masked under all, none, period 3,
three-in-four, sparse, single high bit and hashed masks. Every result is
checked against an independent u128 oracle over partial ranges.

Local results:

- **NEON (native here):** both variants are identical, and their timings
  agree within ±9%; that is the control, as NEON is unchanged.
- **AVX2 under qemu 10.1 (`QEMU="qemu-x86_64 -cpu max -L
  /usr/x86_64-linux-gnu" run.sh avx2`):** every oracle check passes for both
  variants, and the results are identical. qemu timings are not
  measurements.
