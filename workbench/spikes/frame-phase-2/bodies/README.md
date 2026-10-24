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

### Read and write envelopes

Ikea LEAD's [contract review](../instruments/LEAF-CONTRACTS.md) asks for each
leaf's actual physical envelope instead of the blanket 256 bytes of image
padding the harnesses allocate. The halo is how far a leaf's loads go past the
logical body it was given.

| Leaf | Reads past the body | Notes |
| --- | --- | --- |
| plane256, plane64, lanes: every operation | none | Vector loads stay inside the slices and planes; odd R loads 32 bytes, not 64 |
| local8, AVX-512 and NEON transposition | up to 63 bytes past each 64-row group (ceil(8K/64) × 64 − 8K), so into the next body for the last group | The generic path reads exactly |
| local8 point | 8 − R bytes past the row's tile | One u64 load of the tile's plane bytes |
| runs, any vector leaf | ends: within the body (they run into the values). Values: up to 15 lanes (15 × lane bytes) past the last value | 16 runs per vector; lanes past n are masked |
| patched | positions: up to 15 bytes into the values. Values: up to 60 bytes past the last | 16 exceptions per vector |
| compaction (output) | writes exact selected byte counts to slices; ORs whole plane words; clears a block's R planes on first touch | Partial output blocks leave unused slice bytes uninitialised; a full-block reader needs them initialised or a tail reader |
| run unpack, fill variants (output) | up to 32 rows past row 256 of the scratch | Splat stores overwritten by the next run |

So one image suffix of max(63, 15 × lane, 60) bytes covers every leaf
reading the last body. Interior bodies need the next body readable to the same
depth, or a boundary-tail path. Phase 1's catalogue arm keeps SeriesPack's
exact-tail law.

## Measurements

[measure.cpp](measure.cpp) runs synthetic sweeps: uniform residuals per width,
runs cut at random boundaries, and exceptions per block. [columns.cpp](columns.cpp)
runs phase 1's ClickBench columns (3M rows) encoded block by block under a
policy and grouped by (kind, width). [peers.cpp](peers.cpp) runs front A's
pinned TurboPFor and FastLanes at the same widths. Each sweep loop is hoisted
with the leaf inlined. Times are medians of 9 rotated rounds, each at least
2 ms. Tiers are set by rows: 64K (one part, L2), 4M (LLC) and 128M (DRAM, one
core streaming); the saturated tier is front A's. Times are ns per 256-row
block (per point for points). `native` is the global-FOR narrow byte-lane array
through the same lane leaves, a control of my own (front A's corrected bridge
replaces it). `u64` is the logical array. Workers: [worker.sh](worker.sh) on
Zen 5 (c8a.2xlarge, AVX-512 and AVX2 builds) and Graviton 4 (c8g.2xlarge).

Evidence (medians): Zen 5 job `20261024T003628Z-9fa10b9b` (v6),
[packed](evidence/zen5-v6-packed.txt), [runs](evidence/zen5-v6-runs.txt),
[patched](evidence/zen5-v6-patched.txt), [peers](evidence/zen5-v6-peers.txt),
[wide](evidence/zen5-v6-wide.txt), [weighted](evidence/zen5-v6-weighted.txt),
[columns](evidence/zen5-v6-columns.txt). Graviton 4 job
`20261024T003705Z-050e5a24` (v6), [packed](evidence/g4-v6-packed.txt),
[runs](evidence/g4-v6-runs.txt), [patched](evidence/g4-v6-patched.txt),
[peers](evidence/g4-v6-peers.txt), [wide](evidence/g4-v6-wide.txt),
[weighted](evidence/g4-v6-weighted.txt), [columns](evidence/g4-v6-columns.txt).
Targeted reruns of compaction and MIN/MAX: jobs `20261024T011854Z-217c194b`
(Zen 5) and `20261024T011923Z-1dce6c9f` (Graviton 4). Tables below that came
from v4 (`20261023T233103Z-ab8ecf15`) reproduce in v6 within 5%. The Zen 5 v2
files show the state before each fix.

### Packed bodies, Zen 5 AVX-512

| K | SUM L2 native / plane256 / phase 1 | COUNT ≥ L2 native / plane256 | SUM DRAM native / plane256 | Point L2 native / local8 / plane256 | Point DRAM native / local8 / plane256 |
| ---: | --- | --- | --- | --- | --- |
| 2 | 1.39 / 0.25 / 9.0 | 1.44 / 0.39 | 5.84 / 1.47 | 0.31 / 0.35 / 0.47 | 3.73 / 3.99 / 5.29 |
| 4 | 1.39 / 0.73 / 7.6 | 1.41 / 0.73 | 5.81 / 3.47 | 0.31 / 0.35 / 0.78 | 3.68 / 4.33 / 10.05 |
| 7 | 1.41 / 1.32 / 8.5 | 1.40 / 1.33 | — | 0.31 / 0.39 / 1.34 | — |
| 8 | 1.40 / 1.39 / 7.1 | 1.43 / 2.23 | 5.80 / 5.78 | 0.31 / 0.40 / 0.31 | 3.71 / 5.77 / 3.68 |
| 12 | 2.75 / 2.02 / 18.4 | 2.74 / 2.95 | 11.42 / 10.33 | 0.37 / 0.66 / 1.17 | 3.99 / 6.83 / 14.97 |
| 16 | 2.75 / 2.39 / 14.9 | 2.74 / 4.59 | 11.58 / 11.54 | 0.37 / 0.44 / 0.48 | 4.07 / 5.52 / 6.40 |
| 20 | 5.63 / 3.09 / 31.2 | 7.26 / 4.81 | 24.81 / 14.52 | 0.38 / 0.70 / 1.35 | 4.38 / 7.14 / 16.36 |
| 32 | 5.63 / 6.11 / 24.8 | 7.26 / 8.70 | 24.92 / 29.23 | 0.38 / 0.41 / 1.05 | 4.36 / 4.08 / 12.87 |

1. **Bit planes make SUM cost scale with K, not with the byte lane.** At L2,
   plane256 SUM beats native narrow at every width except K = 8 and 32
   (parity): 1.1–5.6× at K ≤ 7 and 1.15–1.8× from K = 9 to 20. That is 4–36×
   faster than phase 1's leaf on the same bits. The native control pays its
   rounded lane (u16 at K = 9, u32 at K = 17); the planes pay K/8 vectors.
2. **Compares follow K at small widths and the lane above 8.** plane256 COUNT
   beats native at K ≤ 7 (0.4–1.3 vs 1.4 ns). From 9 to 32 it is within 1.7×,
   and it wins at K = 20. Each residual plane adds one branch-free op per block
   vector. Byte slices compare at native speed, so the cost above K = 8 is R
   plane ops plus combining four group masks.
3. **plane256 is the scan layout; local8 is the point layout.** A local8 point
   costs 0.35–0.7 ns at L2 and 4–7 ns at DRAM (native 0.3 and 3.7–4.4), because
   a tile keeps a row's bits in one line. A plane256 point needs R plane bytes
   from up to R lines: 10–16 ns at DRAM with four or more planes. This is a
   representation limit, so the scan/point mix should choose the layout.
4. **One core streaming from DRAM, bytes pay only past the lane boundary.**
   plane256 SUM is 1.7× native at K = 20 and 4× at K = 2. It is at parity at
   K = 8, 12 and 16 and 0.85× at 32, where the bytes are equal or the decode
   work saturates first. The saturated tier, where bytes should pay most, is front
   A's next measurement.
5. **A masked-load trap, fixed.** Clang folded "load, then mask in a register"
   into AVX-512 fault-suppressing masked loads. DRAM-streamed masked SUM ran
   2.2× slower (v2: K = 16 29 ns vs local8 13.1; native u8 14 vs 5.8 unmasked).
   An empty asm keeps the load whole: K = 16 is now 12.7 ns against native
   14.7, and it wins at DRAM from K = 2 to 20.

### Runs, Zen 5 AVX-512, L2, 8-bit values (ns per block)

| Runs per block | SUM native u8 / runs | COUNT native / runs | Mask native / ends / start bitmap | Masked SUM native / prefix / per run | Point native / ends / start bitmap |
| ---: | --- | --- | --- | --- | --- |
| 4 | 1.39 / 1.30 | 1.41 / 1.26 | 1.41 / 7.3 / 6.67 | 1.67 / 4.39 / 3.90 | 0.31 / 2.01 / 1.18 |
| 24 | 1.34 / 2.02 | 1.35 / 1.98 | 1.46 / 14.4 / 6.67 | 1.51 / 5.59 / 18.3 | 0.31 / 2.25 / 1.19 |
| 64 | 1.39 / 3.08 | 1.45 / 2.93 | 1.40 / 25.7 / 6.74 | 1.64 / 8.19 / 41.3 | 0.31 / 3.23 / 1.22 |

The "ends" mask column is the best of the four ends-only variants.

6. **SUM and COUNT over runs cost about 0.03 ns per run.** At OS's median of
   24 runs, run SUM costs 2.0 ns against native u8 1.34 (phase 1: about 15).
   That is the ceiling the probe predicted: per-run work cannot beat 256 bytes
   of PSADBW much. Both win below about 8 runs.
7. **Run points meet the 2–5 ns bar without a search.** Counting the ends below
   the row in vector lanes costs 2.0–3.2 ns from 4 to 64 runs. With the
   run-start bitmap a point is a popcount rank: 1.2 ns at any run count. The
   intervals-search thread measured the same fixed-control count at 0.7–1.3 ns
   against 10–18 ns for a search that mispredicts its exit.
8. **Masked SUM by Abel summation removes the per-run loop.** SUM over selected
   rows equals Σ (v_r − v_{r−1}) × (selected rows at or after the start of run
   r). The counts come from 32-entry byte lookups of a prefix-popcount table
   (16 runs per step). 5.6 ns at 24 runs instead of 18.3, and at DRAM 5.7 ns
   against native's 14 (the run body is far smaller).
9. **Mask emission needs the run-start bitmap.** Every ends-only emitter
   (per-run toggles, a transition walk, splat fills, byte scatter) costs about
   0.6 ns per run, because turning sorted positions into a bitmap is a
   scatter. With the bitmap, the run-level transitions are deposited at the run
   starts by PDEP and prefix-XORed (PCLMUL): 6.7 ns at any run count. It is
   still 4.6× native u8 at L2. At DRAM it is within 2× of native for 8-bit
   values and faster for 16-bit (7.4–9.8 vs 12.8).
   On Graviton 4, SVE2 BDEP gives the same shape: 20–26 ns per block on real
   columns against 70–85 without it.

### Real columns, Zen 5 AVX-512, full 3M-row slice (LLC)

Policies: `bytes` picks the smaller of runs and packed per block, `+starts`
adds the run-start bitmap to run blocks (32 B each, charged), and `packed`
never uses runs. ns per block, native / Frame:

| Column, policy | Bytes Frame / native | SUM | COUNT ≥ median | Mask ≥ median | Masked SUM (A ≥ median) | Point |
| --- | --- | --- | --- | --- | --- | --- |
| OS, bytes+starts | 1.39 / 3.0 MB | 2.27 / 2.45 | 2.36 / 2.74 | 2.73 / 8.95 | 2.95 / 7.08 | 3.81 / 6.08 |
| OS, packed | 2.96 / 3.0 MB | 2.26 / 2.24 | 2.33 / 3.29 | 2.70 / 3.65 | 3.08 / 3.95 | 3.83 / 8.69 |
| RegionID, bytes+starts | 2.73 / 12.0 MB | 9.85 / 3.61 | 10.98 / 3.98 | 10.14 / 9.23 | 13.58 / 8.96 | 3.88 / 10.0 |
| RegionID, packed | 5.55 / 12.0 MB | 10.73 / 4.02 | 12.28 / 5.98 | 11.02 / 6.40 | 12.63 / 6.06 | 3.85 / 11.9 |
| EventTime, packed | 6.54 / 12.0 MB | 9.94 / 4.62 | 10.26 / 4.70 | 9.29 / 4.98 | 9.13 / 5.15 | 3.88 / 5.62 |
| ClientIP, bytes+starts | 3.96 / 12.0 MB | 8.61 / 5.06 | 8.51 / 5.45 | 9.67 / 10.03 | 11.80 / 11.94 | 3.86 / 6.91 |
| UserID, bytes+starts | 6.46 / 24.0 MB | 28.53 / 7.55 | 20.02 / 0.55 | 26.84 / 0.88 | 83.59 / 67.87 | 2.56 / 7.07 |
| ResolutionWidth, packed | 3.93 / 6.0 MB | 4.74 / 2.92 | 4.38 / 3.79 | 4.84 / 4.10 | 6.80 / 4.38 | 3.82 / 7.34 |
| CounterID, bytes | 0.19 / 6.0 MB | 4.62 / 0.16 | 4.19 / 0.27 | 4.37 / 0.74 | — | 3.82 / 1.41 |

10. **With leaves near their floor, phase 1's losses become wins on most
    cells.** SUM beats the native narrow array 1.6–3.8× on RegionID,
    EventTime, ClientIP, UserID and ResolutionWidth and matches it on OS.
    COUNT is 1.2–36× native except on OS (0.86×). Masks are 1.1–30×, except OS
    (0.3×) and ClientIP (0.96×). Masked SUM is 1.2–2.1× except OS and ClientIP.
    The columns take 1.5–4.4× fewer bytes (32× on CounterID). Phase 1's OS SUM cost 15–27 ns per block. The u64
    (Polars-width) array is 4–13× slower than Frame on every one of these
    columns.
11. **Runs versus packed is a per-operation choice.** On OS, runs cost 2.45 ns
    per SUM block against 2.24 packed, but 8.95 against 3.65 for mask emission
    and 7.1 against 4.0 for masked SUM. RegionID's masks favour packed (6.4 vs
    9.2) while its COUNT favours runs (4.0 vs 6.0). This is STAGES.md's weighted
    class: Engine's objective weights decide, or a block carries both forms
    when masks dominate.
12. **Points stay 1.6–4× native in this harness.** Resident packed points cost
    5–12 ns and run points 6–10 ns with the bitmap, against native's 3.8. The
    synthetic leaves cost 0.3–3 ns, so the rest is this harness's per-point
    kind dispatch and group lookup, which the substrate thread owns.

### Encode-time exceptions (patched FOR), Zen 5 AVX-512

K = 8 with 4 exceptions per block in [2^8, 2^32); without them the block
needs 32 bits (`native` is then a u32 lane).

| ns per block | L2 native / plane256 at 32 / patched | DRAM native / plane256 at 32 / patched |
| --- | --- | --- |
| SUM | 5.33 / 6.16 / 3.00 | 24.72 / 27.85 / 7.06 |
| COUNT ≥ | 7.21 / 8.72 / 3.82 | 24.94 / 41.63 / 8.25 |
| Mask ≥ | 5.10 / 9.73 / 5.69 | 23.87 / 29.94 / 8.91 |
| Point | 0.39 / 1.06 / 1.50 | 4.32 / 12.64 / 19.30 |

13. **Fused patching costs a correction per exception, never a decode.**
    SUM, COUNT and masks are the packed leaf at K plus one vector pass over the
    exception values. They run 2.7–3.5× native at DRAM with 27% of the bytes.
    Masks pay a scalar per-exception fix-up: at 16 exceptions they lose 3× at
    L2. Points lose at DRAM (19 vs 4.3 ns): the position list and the planes
    are separate lines, a representation cost unless a point-heavy block uses
    local8 and keeps its exceptions in the same tile.

### Compaction, histograms, MIN/MAX, first match (Zen 5 AVX-512)

14. **Compaction to an encoded result beats native narrow compaction where
    the width is whole bytes, and loses where planes must be bit-appended.**
    Masked stores of each compressed slice cost 6.0 ns per block at K = 8
    (native u8 VPCOMPRESSB 4.2) and win at K = 16 (8.1 vs 9.9) and K = 32
    (12.4 vs 23.8), at L2 on Zen 5. With residual planes, each group's PEXT
    result is ORed into the output plane: 14.7 / 19.5 / 18.5 / 20.8 ns at
    K = 3 / 5 / 12 / 20 (native 4.0–19.6). Concatenating a block's four
    extracts in registers and ORing once was slower (K = 5: 39 ns) because of
    the u128 variable shifts and data-dependent deposit loops. Against u64
    (Polars-like) compaction, Frame is 1.1–5.3× faster at every width. At DRAM
    it is 1.1–2.3× behind native narrow. On Graviton 4, SVE2 BEXT makes plane
    compaction beat native u16/u32 lanes 1.3–2.3× from K = 12 to 20.
15. **Narrow-key histograms are bit-sliced 2.3–3.9× faster than the best native.**
    A 2^K-bin histogram of a block (the radix partition and counting-sort
    core) is a prefix tree of plane ANDs: 1.4 / 2.8 / 5.7 / 10.9 ns at
    K = 1 / 3 / 4 / 5, against 3.1–44 ns for native per-key vector counts
    and 53–81 for a scalar histogram. Run bodies histogram per run (10–16 ns
    at 24 runs). From K = 6 the plane path must decode and loses. A sorted
    block of narrow keys is a runs body built from the histogram, with no row
    movement.
16. **MIN/MAX from a body is a fallback.** Bit-sliced narrowing is 0.2–2 ns at
    K ≤ 5. At K = 8 the top byte is the value: 1.5 ns at L2 and 6.5 at DRAM
    (native 1.6 and 6.6; it was 18.9 at DRAM while it still read candidates).
    From K = 12 the few rows sharing the top byte are read as points: 6–9 ns
    at L2, 20–26 at DRAM (native 3.5–7.3 and 11–23). A branch-free narrowing
    instead was worse at DRAM (29.7–79.6 ns), so it is not used. Exact block
    statistics answer MIN/MAX without the body (MECHANISMS.md), so this leaf
    only serves selected rows.
17. **First match** (`first_ge`) is a compare plus a branch-free position over
    the four groups. It beats native at DRAM on Zen 5 (3.3 / 6.4 / 9.6 / 20.1
    ns at K = 4 / 8 / 12 / 20 against 4.2 / 7.9 / 14.7 / 26.9). Before, it cost
    13–44, because a mispredicted loop exit throttled memory parallelism. At
    L2 it costs 1.6 ns more than the branchy loop (2.5 vs 0.9 native at K = 4).

### Peers (front A's pinned TurboPFor and FastLanes)

| | Zen 5 AVX-512 L2 | Graviton 4 L2 |
| --- | --- | --- |
| SUM K = 4: plane256 / FastLanes narrow decode+sum / TurboPFor vertical decode+sum | 0.73 / 2.16 / 17.7 | 4.53 / 8.24 / 35.4 |
| SUM K = 12 | 2.09 / 4.81 / 18.1 | 8.62 / 22.3 / 38.9 |
| Unpack to u32, K = 12: plane256 / TurboPFor vertical / FastLanes | 8.29 / 5.10 / 4.03 | 64.8 / 17.4 / 28.4 |
| Point K = 12 at DRAM: local8 / TurboPFor bitgetx32 / native | 6.97 / 11.41 / 3.95 | 13.91 / 9.92 / 7.19 |

18. **Operating on planes beats decode-then-consume with the best decoders.**
    plane256 SUM is 1.6–8× FastLanes decode-then-sum on Zen 5 and 1.7–3.8× on
    Graviton 4 at K ≤ 16. That compares an operation law to the decode
    ceiling: FastLanes and TurboPFor decode, then the lane leaf consumes from
    L1.
19. **Decoding is where planes lose, most on NEON.** Unpack to u32 is
    at parity to 2.3× slower than TurboPFor and FastLanes on Zen 5, where
    VPMOVM2B expands a plane in one op, and 2.8–4× slower on Graviton 4, where each plane costs a
    table lookup and a test per 16 rows. A body that is mostly decoded (a
    point-heavy or materialising consumer) wants a vertical or striped layout
    on ARM. That is a representation limit, not code.
20. **local8 direct access beats TurboPFor's on Zen 5 at DRAM** (4–7 ns vs
    11.4–11.8 at K ≥ 12; native 4–4.4) and loses to it on Graviton 4 at K = 12–20
    (14–17 vs 10 ns), where the multiply-gather of plane bits and the separate
    body-byte loads cost more. Unattributed.

### Graviton 4 (NEON, SVE2 bit permutes)

21. **NEON leaves needed their own compare and decode.** NEON has no movemask,
    so byte compares stay as byte masks across slices and become row bits
    once per group; COUNT sums byte masks and converts only the equal-prefix
    rows. COUNT ≥ is now 4.7 / 5.2 / 16.0 / 30.5 ns at K = 4 / 8 / 12 / 32
    (native 7.1 / 7.0 / 8.0 / 14.8), and masks 3.4 / 5.5 / 15.9 / 34.8 (native
    12.3 / 12.3 / 15.7 / 25.6). Below K = 9 Frame now wins. From 9 to 32 it
    is at parity to 2× behind, because each slice adds two byte compares that a u16 or
    u32 lane does in one. SUM uses byte popcounts weighted per plane: 3.5 /
    4.3 / 7.5 / 10.9 ns at K = 4 / 8 / 12 / 20, against native 4.2 / 4.2 / 7.3
    / 21.9.
22. **Decode stays 2–5× behind on NEON, a layout limit.** Expanding a plane to
    bytes is a table lookup, a test and a shift-insert per 16 rows (the SRI
    chain saves one op of four), and B = 1 bodies combine in u16 lanes before
    one widening. K = 12 unpack fell from 65 to 52 ns (native 12.3, TurboPFor
    vertical 17.4). Decode-heavy blocks on ARM want SeriesPack's striped or a
    vertical layout.
23. **SVE2 BDEP makes run masks native-speed on Graviton 4.** The run-start
    mask leaf is compiled for SVE2 and AES as a whole, so BDEP and the PMULL
    prefix inline: 12.5–12.8 ns per block at 4–64 runs, native u8 12.3, both
    at L2 and DRAM (ends-only: 12.6 / 31.0 / 59.7). On real columns RegionID
    masks run 1.8× native and ClientIP 1.5×. The vector popcount rank makes
    run points 2.6 ns at L2 at any run count; from DRAM the extra evidence
    line costs 32–36 ns against 10–41 from the ends alone.
24. **NEON prefix masked SUM** runs 15.4 ns at 24 runs, against native 11.3 and
    the per-run loop's 28.8.

### Widths above 32 bits (Zen 5 AVX-512)

25. **Planes to K = 64 keep paying until about K = 48.** With u128 sums, SUM
    is 6.0 / 9.0 / 10.8 / 14.1 ns at K = 33 / 40 / 48 / 63 against the u64
    array's 14.5 at L2. At DRAM it is 25.9 / 30.6 / 35.6 / 49.3 against 44.5.
    COUNT wins to K = 40, and masked SUM and MAX to K = 56. Points (1.3–2.9 ns at L2,
    14–34 at DRAM, native 0.3 and 4.0) and unpack lose. Only blocks whose span
    stays below 48 bits pay; 64-bit hashes are left as u64 lanes.

### Weighted code SUM (the dictionary seam)

26. **SUM of D[code] avoids per-row gathers wherever the codes are narrow or
    run.** [weighted.h](weighted.h) has four paths:
    - Runs: one weight per run. 9–12 ns per block at L2 and 15–18 at DRAM,
      against 14.5 and 44.5 for the materialised u64 values.
    - K ≤ 6: the bit-sliced histogram dotted with D, 6.7 ns at K = 4.
    - K ≤ 8 with D prepared once per part as byte tables (VBMI permutes): 13–17
      ns at L2 and 13–14 at DRAM, 3.2× the materialised values there.
    - Uniform codes above 8 bits: decode and gather (VPGATHERDQ) costs 55–77 ns
      and loses to materialised values (4× at L2).
    Graviton 4: runs 12–18 against 26–62, and the histogram at parity at
    K = 4. NEON's four-register table lookups make byte tables slower than
    gathers there (83–212 ns). Masked forms exist for runs, tables and
    gathers.

### The exact boundary route

27. **A body copy costs 2–10 ns per scanned block and up to 40 per point**
    (Zen 5: runs SUM 2.0 → 11.7 ns, local8 SUM at K = 12 3.6 → 14.9, local8
    points 0.4 → 3.1 at L2 and 5.1 → 24.6 at DRAM). It is meant for the last
    body of a slice or one next to a protected neighbour, not every read.
    A point near a boundary should use exact-length loads of its tile instead.

## Still open

- Compaction with residual planes (bit-granular appends) and MIN/MAX at
  K ≥ 12 beyond L2 remain behind native.
- NEON compares from K = 9 (two byte compares per slice) and NEON decode
  (a layout question: striped or vertical bodies for decode-heavy blocks).
- Weighted SUM over uniform codes wider than 8 bits (gather-bound).
- Exact-length point loads for boundary bodies.
- The saturated DRAM tier and front A's corrected native controls.
