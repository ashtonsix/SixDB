# Compiled-once range operations

Frame phase 3 asked SeriesPack ([FINDINGS.md §1 and §3](../../../frame-phase-3/FINDINGS.md))
for whole range operations through its runtime-width reader, so Frame needs
no width-specialised kernels. Each of Frame's 16-width units took 4–7 GB and
2–5 minutes to compile. It also asked for a position-weighted SUM for the
delta stage. A masked-sum law for that SUM was 1.3–2.6× slower than decoding.

## What SeriesPack now offers

- **`bind_range_reader<U>(dense_input)`** returns the runtime-width reader
  with a layout's compiled `range_operations`. These are compiled once per
  layout in `src/seriespack/operations_*.cpp`, four widths per unit.
  - Range operations: `sum`, `sum_masked` (16-row masks, optionally repeating
    with a period), `count` and `filter_words` per comparison, and
    `weighted_sum`. Checked forms exist for `sum`, `count` and
    `filter_words`; `sum_masked` and `weighted_sum` are trusted only.
  - `point_kernel()` and `resource()`, for a caller's fused point path.
  - The plain `bind_reader<U>(dense_input)`, and every other erased reader,
    answer the same methods by decoding.
  - **Link cost.** A program that links `bind_range_reader` retains every
    layout's tables. A get-only program built with `--gc-sections` is 4.75 MB
    of text through `bind_range_reader` and 192 KB through `bind_reader`, the
    same as before this batch (Codex's witness, rebuilt locally).
- **`set(const dense_output&, row, value, journal&)`**, a compiled point
  replacement. Its records name the `dense_output`. `footprint(target, row)`
  returns the byte spans it stores and reads, for a write envelope.
- **`author::weighted_sum_regions`** and `reader::weighted_sum_unchecked`.
  These give the modulo-2^64 sum of `weights[row % 16] × value` in one
  decode: lanes are multiplied in registers inside `scan_regions`.

Building the whole SeriesPack library with these units takes 59 s at 2.1 GB peak on
Zen 5 and 100 s at 3.0 GB on Graviton 4 (`-j6`, jobs below). Each unit
compiles at about 1.2 GB locally.

## Cost at Frame's grain

[`bench.cpp`](bench.cpp) times one call per 256-row block over a 64K-row part
(warm), typed reader against runtime reader over the same bytes; every answer
is checked. ns per call, typed / runtime. Jobs `20261024T210622Z-4c694678`
(Zen 5) and `20261024T210657Z-64997da1` (Graviton 4), on the final sources (filter words consumed and checked in full);
[evidence/erased-zen5.jsonl](evidence/erased-zen5.jsonl),
[evidence/erased-graviton4.jsonl](evidence/erased-graviton4.jsonl).

| Zen 5 | sum | masked sum | count ≥ | filter ≥ | point |
| --- | --- | --- | --- | --- | --- |
| compact K = 3 | 4.7 / 4.8 | 4.5 / 9.1 | 4.3 / 5.6 | 7.0 / 7.9 | 0.7 / 1.6 |
| compact K = 12 | 4.0 / 8.5 | 15.2 / 18.8 | 9.6 / 13.0 | 12.1 / 14.0 | 0.8 / 1.6 |
| compact K = 33 | 12.6 / 12.6 | 21.6 / 24.8 | 18.0 / 21.4 | 18.6 / 21.6 | 0.6 / 1.6 |
| bulk K = 12 | 5.6 / 5.6 | 14.1 / 13.6 | 5.8 / 7.0 | 7.7 / 8.4 | 0.5 / 1.6 |

| Graviton 4 | sum | masked sum | count ≥ | filter ≥ | point |
| --- | --- | --- | --- | --- | --- |
| compact K = 3 | 6.2 / 8.4 | 38.5 / 31.9 | 10.4 / 11.5 | 11.7 / 11.1 | 0.9 / 1.5 |
| compact K = 12 | 20.8 / 17.9 | 60.4 / 60.7 | 34.0 / 35.5 | 33.9 / 34.6 | 1.1 / 1.6 |
| compact K = 33 | 38.2 / 42.1 | 118 / 121 | 163 / 164 | 162 / 162 | 1.0 / 1.4 |
| bulk K = 14 | 18.1 / 16.3 | 40.0 / 42.3 | 41.3 / 42.4 | 43.1 / 44.4 | 2.5 / 2.9 |

- A runtime call costs 0–4 ns more per block than the typed reader, which
  inlines into the caller's loop. Masked SUM over narrow compact widths on
  Zen 5 is the largest relative gap (4.5 → 9.1). On Graviton 4 the two are
  within noise.
- A point through the exposed kernel costs 0.2–1.1 ns more than the typed
  reader's inlined point: one indirect call.

## Position-weighted SUM

The delta stage's SUM law, Σx = n·c + Σ δⱼ(n − j), needs Σ j·δⱼ by in-region
position. `weighted_sum` gets it in one decode. The masked-sum law needs one
prefiltered SUM per bit of the position, four for 16-row regions. ns per
256-row block (weighted / masked-sum law): weights[row % 16] = row % 16.

| | Zen 5 | Graviton 4 |
| --- | --- | --- |
| compact K = 3 | 12.5 / 86.5 | 30.7 / 158 |
| compact K = 12 | 15.5 / 90.1 | 64.0 / 245 |
| compact K = 33 | 19.2 / 107 | 301 / 489 |
| bulk K = 3 | 8.9 / 87.5 | 15.8 / 168 |

That is 5–10× faster on Zen 5, and 3.8–11× on Graviton 4 at K ≤ 20. At
K > 32, Graviton 4 multiplies 64-bit lanes as two 32-bit halves and the gain
drops to 1.6–1.8×. On NEON, K ≤ 16 multiplies u8 or u16 lanes into u16 or u32
products before widening, which cut Graviton 4's cost by 1.4–4× at K ≤ 14
against the first version (u32 lanes throughout).

This covers the measured 16-position law only. The result is modulo 2^64,
so an exact whole-delta SUM still needs a bounded-width or wide lift (u128).
G64 regions need another route.

Both machines' numbers are from the final sources.

## Exact sums (u128)

Frame phase 3 asked for exact sums, so it can stop chunking SeriesPack's
modulo-2^64 sums and stop decoding above 55 bits. `sum_exact`,
`sum_masked_exact` and `weighted_sum` (weights indexed by row % 16 or
row % 64) now return u128.
- Where a width cannot wrap a 64-bit sum within a chunk of at least 256 rows,
  they run the modulo kernels on such chunks.
- Wider widths, and a weighted masked sum, add the 32-bit halves of each
  value, times its weight, in separate u64 lanes.

The earlier modulo `weighted_sum` (period 16) is replaced. Weighted sums
are ns per 256-row call. Exact SUM is ns per 256 rows amortized over one
65,536-row call (`call_rows` in later evidence); both exact SUM arms share
that boundary. Jobs `20261025T035710Z-757ad2ac` (Zen 5) and
`20261025T035743Z-5e5b3efb` (Graviton 4);
[evidence/exact-zen5.jsonl](evidence/exact-zen5.jsonl),
[evidence/exact-graviton4.jsonl](evidence/exact-graviton4.jsonl).

| | Zen 5 weighted (runtime / masked law) | Zen 5 exact SUM (SeriesPack / Frame's chunks or decode) | Graviton 4 weighted | Graviton 4 exact SUM |
| --- | --- | --- | --- | --- |
| compact K = 3 | 15.6 / 87.0 | 0.4 / 0.4 | 33.0 / 163 | 1.8 / 1.8 |
| compact K = 12 | 23.8 / 94.7 | 2.3 / 2.3 | 66.2 / 253 | 14.8 / 14.8 |
| compact K = 33 | 24.0 / 112 | 11.9 / 11.4 | 305 / 498 | 37.6 / 37.6 |
| compact K = 55 | 38.4 / 152 | 23.2 / 23.5 | 163 / 570 | 73.0 / 73.2 |
| compact K = 60 | 42.9 / 193 | 30.2 / 105 | 152 / 729 | 116 / 162 |
| compact K = 64 | 21.1 / 171 | 11.4 / 49.2 | 184 / 478 | 91.3 / 117 |

- The exact weighted sum is 4–8× faster than the masked-sum law on Zen 5.
  On Graviton 4 it is 2.6–5× faster, and 1.6× at K = 33.
- The exact SUM matches Frame's chunked route up to 55 bits. Above that it is
  3.5–4.3× faster than decoding on Zen 5 (1.3–1.4× on Graviton 4).
- Two fixes came out of timing:
  - Dividing by a runtime weight period in every part had held the runtime
    weighted sum at 43 ns on Zen 5; the period (16 or 64) is now a mask.
  - Narrow widths' operation units outgrew a 16 GB worker at `-j6`. The
    operations now compile in 16 units of four widths, 1.3 GB at most each;
    the whole library builds in 58 s at 1.6 GB on Zen 5 and 90 s at 2.8 GB
    on Graviton 4.

Full-scope checks (every width instantiated) pass on these sources: jobs
`20261025T035559Z-42af01a9` (Zen 5 AVX2 and AVX-512) and
`20261025T035630Z-2d0f6664` (Graviton 4).

## Checks

Maintained tests (`ikea_seriespack_check`) cover:
- every width and both geometries at run time (`check_runtime_widths`: values
  through the runtime initializer, read back through compiled and decoded
  operations, with a u8 reader where it fits);
- sampled widths over phase 1's independent wire encoder:
  compiled and decoded paths alike, every comparison, masked sums with and
  without a period, exact filter words, checked forms, and the point kernel;
- runtime `set` against a bound writer over identical bytes: equal bytes,
  records naming the `dense_output` and covering exactly its footprint, and
  unchanged bytes and effects on range, value and capacity refusals;
- `weighted_sum_regions` on placed, headed, guarded and substituted sources.

With every width instantiated (`SIXDB_TEST_SCOPE=full`), jobs
`20261024T210507Z-cd9d2ab0` (Zen 5 AVX2 and AVX-512) and
`20261024T210545Z-376ba5e1` (Graviton 4) pass the SeriesPack, admission,
descriptor, ownership and filters checks with the spike checks.
