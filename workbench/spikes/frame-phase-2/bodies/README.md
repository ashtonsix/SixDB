# Integer body leaves (front C)

Front C of [phase 2](../PARTITION.md), opened 2026-10-23. The question: how far
can each leaf over an integer body (constant, runs, packed FOR, encode-time
exceptions) be pushed, against the native narrow-FOR array, at each width, ISA
and residence tier, and which remaining costs are code and which are the
representation.

The leaf boundary is the substrate's: (block body, width, count, base,
operation arguments, optional 256-row selection) → partial result in the
residual domain. The substrate adds base terms and decides blocks from
descriptor bounds. Nothing here changes that boundary. Two additions are
optional and sit beside a body: a run-start bitmap (32 B per run block, as
evidence) and an output stream for compaction.

## Starting point

- The [substrate probe](../substrate/README.md) showed phase 1's per-block cost
  sat in the leaves. OS SUM spent about 15 ns per block at L2 against 2.5 ns for
  native u8, and run points 26–38 ns against 0.5–1 ns. Batching moved costs by
  about 10%.
- Phase 1 leaves decoded SeriesPack Local planes into 16-row packets
  ([kernels.cpp](../../frame-phase-1/encoding/kernels.cpp)) and walked runs one
  at a time ([runs.h](../../frame-phase-1/encoding/runs.h)). They are kept here as
  the `phase1` arm, on byte-identical local8 bodies. `check.cpp` asserts that my
  local8 encoder equals phase 1's `pack()`.
- [Packed filters](../../packed-filters/RESULTS.md) found direct Local plane
  comparison competitive at small widths on NEON, with GFNI decode winning on
  x86 for 16-value windows. Frame's unit is a 256-row block, which changes the
  balance: planes become 256-bit vectors.
- [NEON Local transpose](../../neon-local-transpose/README.md): stripes read
  1.8–4.7× faster than Local on Neoverse when decoding. I did not use Striped
  yet. No leaf here decodes in order to scan, so Striped's advantage (cheap byte
  extraction) mostly doesn't apply; it is still open for unpack.

## Representations

[body.h](body.h). K = 8B + R. As in SeriesPack, the B upper bytes are bytes
and the R low bits are bit planes.

| Layout | Arrangement | Bytes per block |
| --- | --- | --- |
| `local8` | SeriesPack Local: 8-row tiles of K bytes (row bytes, then plane bytes) | 32K |
| `plane64` | 64-row tiles: B byte slices of 64, then R plane words | 32K |
| `plane256` | one 256-row tile: B byte slices of 256, then R planes of 32 bytes | 32K |
| `lane` | byte-lane rounded (8/16/32/64 bits), the native narrow array per block | 256 · lane |
| runs | n u8 ends, then n values in a 1/2/4/8-byte lane | n(1 + lane) |
| patched | plane256 at K with exception slots 0, then n u8 positions and n u32 values | 32K + 1 + 5n (padded) |

## How the leaves work

[packed.h](packed.h), [runs.h](runs.h), [exceptions.h](exceptions.h),
[compact.h](compact.h), [sort.h](sort.h). Every leaf reads a block as four
64-row groups of (B byte slices, R plane words). plane256's planes are
contiguous across groups, so its leaves vectorise the planes across a whole
block. No packed leaf unpacks except `unpack`.

- **SUM**: byte sums (PSADBW or UADALP) per slice, plus Σ popcount(plane_b) ≪ b.
  For plane256 that is one VPOPCNTQ and one shift per 64 bytes of planes.
- **Compare, mask, COUNT**: byte compares on the slices from the top byte,
  then a branch-free bit-sliced compare over the planes, four groups per
  vector.
- **Masked SUM**: masked byte sums plus popcount(plane & selection).
- **MIN/MAX**: the top slice's extreme byte, then point reads of the few rows
  that share it, else bit-sliced narrowing per plane.
- **Point**: local8 gathers the R plane bits of a tile with one load and one
  multiply. The plane layouts need R byte loads.
- **Runs**: lengths from adjacent ends, 16 runs per vector, for SUM and COUNT.
  Masked SUM uses Abel summation over run starts with 32-entry byte lookups for
  selected-row counts (no per-run loop). Points count ends below the row in
  vector lanes. Mask emission has four variants (below).
- **Exceptions**: the packed leaf plus a correction over the n exceptions
  (slots hold 0). Nothing is decoded and then patched.
- **Compact**: selected rows appended to a plane256 stream at the same base and
  width. Bytes go through VBMI2 compress (else an 8-row shuffle table), planes
  through PEXT (else a bit loop).
- **Histogram (sort/partition)**: for K ≤ 6 bit-sliced, as a prefix tree of
  plane ANDs (2^(K+1) four-word ops, 2^K popcounts). Above that, decode and
  count. A sorted block of narrow keys is a runs body built from its histogram.

Correctness: [check.cpp](check.cpp) checks every leaf, layout and width 1..32
against an oracle over the original values. That covers 2,721 blocks across
distributions, edge constants, selections, every row as a point, plus runs at
11 run counts and patched blocks with 1–64 exceptions. Every timing run
re-checks each arm's answer first. [columns.cpp](columns.cpp) checks every
real-column answer against an exact oracle.

## Measurements

[measure.cpp](measure.cpp) runs synthetic sweeps: uniform residuals per width,
runs cut at random boundaries, exceptions per block. [columns.cpp](columns.cpp)
runs phase 1's ClickBench columns (3M rows) encoded block by block under a
policy and grouped by (kind, width). Each sweep loop is hoisted with the leaf
inlined. Medians of 9 rotated rounds, each at least 2 ms. Tiers are set by
rows: 64K (one part, L2), 4M (LLC) and 128M (DRAM, one core streaming). Times
are ns per 256-row block (per point for points). `native` is the global-FOR
narrow byte-lane array through the same lane leaves; `u64` is the logical
array. Workers: [worker.sh](worker.sh) on Zen 5 (c8a.2xlarge, AVX-512 and AVX2
builds) and Graviton 4 (c8g.2xlarge).

Evidence (medians): [zen5-v2-packed](evidence/zen5-v2-packed.txt),
[zen5-v2-runs](evidence/zen5-v2-runs.txt),
[zen5-v2-columns](evidence/zen5-v2-columns.txt). Job
`20261023T223949Z-9d9ee67e`.

### Packed bodies, Zen 5 AVX-512

| K | SUM L2 native / plane256 / phase 1 | COUNT ≥ L2 native / plane256 | SUM DRAM native / plane256 | Point L2 native / local8 / plane256 | Point DRAM native / local8 / plane256 |
| ---: | --- | --- | --- | --- | --- |
| 2 | 1.39 / 0.24 / 7.7 | 1.61 / 0.39 | 5.81 / 1.41 | 0.31 / 0.35 / 0.47 | 3.53 / 3.55 / 4.70 |
| 4 | 1.41 / 0.73 / 7.7 | 1.42 / 0.75 | 5.78 / 3.53 | 0.31 / 0.35 / 0.78 | 3.55 / 4.20 / 9.90 |
| 7 | 1.42 / 1.29 / 8.7 | 1.47 / 1.36 | — | 0.31 / 0.39 / 1.37 | — |
| 8 | 1.42 / 1.38 / 7.3 | 1.49 / 2.31 | 5.72 / 5.77 | 0.31 / 0.41 / 0.32 | 3.52 / 4.96 / 3.52 |
| 12 | 3.70 / 2.05 / 18.4 | 2.79 / 2.97 | 12.37 / 11.16 | 0.38 / 0.66 / 1.18 | 3.94 / 6.92 / 15.79 |
| 16 | 3.70 / 2.41 / 15.0 | 2.79 / 4.61 | 12.71 / 11.49 | 0.38 / 0.45 / 0.49 | 4.04 / 5.53 / 6.40 |
| 20 | 12.42 / 2.91 / 31.3 | 7.26 / 4.81 | 27.96 / 14.42 | 0.38 / 0.70 / 1.35 | 4.38 / 7.20 / 16.56 |
| 32 | 12.52 / 6.18 / 25.1 | 7.27 / 8.76 | 27.35 / 30.51 | 0.39 / 0.41 / 1.05 | 4.49 / 4.22 / 13.07 |

1. **Bit planes make SUM cost scale with K, not with the byte lane.** At L2,
   plane256 SUM beats native narrow at every width except K = 8 (parity):
   1.1–5.8× at K ≤ 7 and 1.5–4.3× from K = 9 to 32. That is 4–30× faster than
   phase 1's leaf on the same bits. The native control pays its rounded lane
   (u16 at K = 9, u32 at K = 17); planes pay K/8 vectors.
2. **Compares follow K at small widths, the lane above 8.** plane256 COUNT
   beats native at K ≤ 7 (0.4–1.4 vs 1.5 ns). From 9 to 32 it is within
   1.7× and wins at K = 20. Each residual plane adds one branch-free op per block vector.
   Byte slices compare at native speed, so the cost above K = 8 is R plane ops
   plus combining four group masks.
3. **plane256 is the scan layout; local8 is the point layout.** A local8 point
   is 0.35–0.7 ns at L2 and 4–7 ns at DRAM (native 0.3 and 3.5–4.5), because a
   tile keeps a row's bits in one line. A plane256 point needs R plane bytes
   from up to R lines: 10–16 ns at DRAM with five or more planes. This
   is a representation limit, so the scan/point mix should choose the layout
   (local8 or plane64 for point-heavy blocks).
4. **DRAM converts bytes into time only past the lane boundary.** One core
   streaming, plane256 SUM is 2× native at K = 20 (14.4 vs 28.0) and 4× at K = 2,
   but at parity at K = 8, 12, 16 and 32, where bytes are equal or the decode
   work saturates first. The saturated tier, where bytes matter most, is not
   measured yet (front A's harness).
5. **Open anomaly**: plane256 masked SUM falls off beyond L2 at K ≥ 8 (DRAM
   K = 16: 29 ns vs native 13.6 and local8 13.1), while plain SUM does not.
   Unattributed.

### Runs, Zen 5 AVX-512, L2 (ns per block)

| Runs per block | SUM native u8 / runs | COUNT native / runs | Mask native / best run variant | Masked SUM native / runs (scalar) | Point native / runs |
| ---: | --- | --- | --- | --- | --- |
| 4 | 1.38 / 1.29 | 1.47 / 1.27 | 1.45 / 7.3 | 1.92 / 3.9 | 0.31 / 1.81 |
| 24 | 1.41 / 2.05 | 1.61 / 1.98 | 1.39 / 14.6 | 2.57 / 18.4 | 0.31 / 2.27 |
| 64 | 1.36 / 3.14 | 1.45 / 3.02 | 1.48 / 23.1 | 1.93 / 41.4 | 0.32 / 3.41 |

6. **SUM and COUNT over runs reach about 0.03 ns per run.** At OS's median of 24
   runs, run SUM costs 2.05 ns against native u8 1.41 (phase 1: about 15). That
   is the ceiling the probe predicted: per-run work cannot beat 256 bytes of
   PSADBW much. COUNT is at parity, and both win below about 8 runs.
7. **Run points meet the 2–5 ns bar.** Counting ends below the row in vector
   lanes costs 1.8–3.4 ns from 4 to 64 runs at L2, and no binary search is
   involved.
8. **Mask emission and selected SUM were the run leaves far from the floor.**
   Every ends-only mask variant (per-run toggles, transition walk, fill, byte
   scatter) costs about 0.6 ns per run. The prefix-count masked SUM and the
   run-start bitmap mask (PDEP) are in the v3 run.

### Real columns, Zen 5 AVX-512, full 3M-row slice (LLC)

Policy `bytes` picks the smaller of runs and packed per block; `packed` never
uses runs. ns per block, native / Frame:

| Column | Bytes Frame / native | SUM | COUNT ≥ median | Mask ≥ median | Point |
| --- | --- | --- | --- | --- | --- |
| OS (bytes: 36 runs mean) | 1.0 / 3.0 MB | 2.14 / 2.51 | 2.27 / 2.78 | 3.34 / 58.4 | 3.88 / 13.3 |
| OS (packed) | 3.0 / 3.0 MB | 2.23 / 2.26 | 2.41 / 3.33 | 2.78 / 3.60 | 3.90 / 8.7 |
| RegionID (bytes) | 2.4 / 12.0 MB | 13.94 / 3.62 | 11.76 / 4.03 | 15.9 / 53.8 | 3.92 / 16.7 |
| RegionID (packed) | 5.6 / 12.0 MB | 13.50 / 4.26 | 11.19 / 6.23 | 9.28 / 5.77 | 3.85 / 11.7 |
| EventTime (packed) | 6.5 / 12.0 MB | 13.57 / 4.95 | 10.55 / 4.78 | 9.80 / 5.26 | 3.87 / 5.29 |
| UserID (bytes, 64-bit runs) | 6.1 / 24.0 MB | 33.97 / 7.57 | 27.55 / 0.46 | 28.1 / 0.86 | 2.49 / 14.9 |
| ClientIP (bytes) | 3.6 / 12.0 MB | 13.66 / 5.10 | 11.52 / 5.45 | 16.2 / 67.6 | 3.88 / 14.7 |
| CounterID (bytes) | 0.19 / 6.0 MB | 5.60 / 0.16 | 5.08 / 0.27 | 4.60 / 0.71 | 3.86 / 0.99 |

9. **Leaves near their floor turn phase 1's losses into wins.** SUM and COUNT
   now beat the native narrow array on RegionID, EventTime, UserID and ClientIP
   (SUM 2.6–4.5×, COUNT 2–60×) and match it on OS. They do it in 1.8–5× fewer
   bytes (32× on CounterID). Phase 1's OS SUM cost 15–27 ns per block. The u64
   (Polars-width) array is 4.6–13× slower than Frame on these columns.
10. **Runs versus packed is a per-operation choice.** The same OS rows cost 2.5
    ns per SUM block as runs and 2.3 as packed, but 58 vs 3.6 for mask
    emission. Run points cost 13–17 ns in this harness against 5–12 packed
    (part of it is the per-point kind dispatch). This is STAGES.md's weighted
    class: Engine's objective weights decide, or a block keeps both forms when
    masks or points dominate.

## Still open

- v3 (Zen 5) and Graviton 4 runs with the prefix masked SUM, run-start bitmap
  masks and points, exceptions, compaction, histograms, MIN/MAX and first match.
- Striped layouts for unpack on NEON; SVE2 BDEP/BEXT (Graviton 4 has
  `svebitperm`) for run masks and compaction on ARM.
- K > 32 packing (only 64-bit hashes need it here).
- Peers from front A: TurboPFor direct access and FastLanes decode.
- The saturated DRAM tier.
