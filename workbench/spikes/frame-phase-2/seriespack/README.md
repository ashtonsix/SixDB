# SeriesPack remeasured

Phase 2's integer-body front compared only SeriesPack Local, and only through
phase 1's own leaves, then built a new `plane256` layout. This probe measures
SeriesPack through its maintained writer and typed reader instead, under all
three presets, so the performance thread starts from SeriesPack's real gaps.

[`measure.cpp`](measure.cpp) packs uniform random K-bit values with
SeriesPack's writer (`compact` = Local; `bulk_x86` and `bulk_arm` = striped
32-byte residuals at their widths, Local elsewhere) and times the typed
reader's `sum`, `sum` under a random 50% mask, `filter<greater_equal>` in
64-row windows (popcounted), `read` to u32 and random `get`. Beside it:
front A's qualified native SUM control and typed native loops over the
byte-lane-rounded array, and front C's `plane256` image (SUM and COUNT) as a
reference for what a scan leaf reaches on the same values. Every answer is
checked; each `read` is checked in full before timing. Warm is one part
(65,536 rows) resident; flushed is 8M rows with the array's lines flushed
before each run (a conditioning step, not proof of DRAM service).

Source `14d384d6`; jobs `20261024T025627Z-100e3dc5` (Zen 5, AVX2 and
AVX-512) and `20261024T025659Z-ad8eaf5f` (Graviton 4). Medians:
[evidence/remeasure-v2-summary.txt](evidence/remeasure-v2-summary.txt),
[AVX2](evidence/remeasure-v2-avx2-summary.txt).

## Findings

Ratios are SeriesPack time over native time; below 1 is a SeriesPack win.

| Warm, ratio to native | SUM Zen 5 | SUM Graviton 4 | COUNT ≥ Zen 5 | COUNT ≥ Graviton 4 | Point Zen 5 | Point Graviton 4 |
| --- | --- | --- | --- | --- | --- | --- |
| Bulk, K = 1, 2, 4 | 0.58–0.60 | 0.63–0.96 | 4.5–4.6 | 2.3–2.4 | 1.3–1.4 | 2.7–2.9 |
| Bulk, K = 3, 5, 7 | 5.0–6.9 | 1.9–2.5 | 5.9–7.1 | 3.3–3.4 | 7.4–14 | 10–16 |
| Bulk, K = 10, 12, 14, 15, 20 | 2.4–10 | 1.8–2.3 | 5.1–7.8 | 3.1–3.8 | 2.0–17 | 2.9–13 |
| Compact, non-byte K | 1.5–5.7 | 1.2–4.9 | 3.8–6.8 | 1.6–5.7 | 1.2–3.6 | 2.3–3.5 |
| Compact, K = 8, 16, 24, 32 | 1.4–1.9 | 0.75–1.2 | 1.9–5.1 | 2.0–2.2 | 1.3–1.8 | 1.2–1.8 |
| plane256 reference | 0.39–1.64 | 0.20–0.92 | 0.31–2.3 | 0.20–1.5 | — | — |

1. **SeriesPack's layout is not what loses scans.** Bulk SUM at K = 1, 2 and
   4 beats native 1.7× on Zen 5 (1.5 ns per 256 rows against 2.6), within 1.1–1.5× of `plane256`. Flushed on Zen 5 it reaches 0.25–0.5× native.
   The 32-byte stripes already give the bit-plane scan property `plane256`
   was built for.
2. **The gaps are kernel coverage.** Bulk SUM at the odd striped widths (3, 5,
   7) and the ARM-only stripes (14, 15) runs 5–10× native on Zen 5. Its masked
   sum costs the same as its unmasked sum there, so the unmasked call has no
   fast path at those widths. Compact SUM at non-byte widths pays 1.5–5.7× warm.
3. **Filter is the largest gap.** Counting `x ≥ t` through 64-row `filter`
   windows costs 3.8–7.8× native on Zen 5 at non-byte widths and 1.6–5.7× on Graviton 4, while `plane256`'s count leaf beats native at K ≤ 8. The
   per-call window (a 64-bit result per call) and its setup dominate, not the
   comparison.
4. **Striped points are slow at odd widths.** Bulk points at K = 3, 5, 7, 14 and 15 cost 7–17× native warm on Zen 5 (1.9–4.6 ns against 0.25), where compact costs 1.2–3.6×. The
   preset guidance already sends points to compact; nothing here contradicts
   it.
5. **Decode is close on x86 and behind on ARM.** `read` to u32 runs at
   0.85–1.8× native on Zen 5. On Graviton 4, compact pays 1.2–3.5× at non-byte widths, bulk 0.9–1.8×, and bulk at K = 1–3 flushed 3–4×.
6. **Two anomalies at K = 24.** Compact flushed COUNT is 4.0× and SUM 2.6× native on Zen 5, against 1.0–1.1× at 16 and 32.

## Limits

- Uniform random values only; no runs, constants or real columns. Those are
  Frame's composition over SeriesPack, not SeriesPack's own work.
- The native masked-sum control does not vectorise on either ISA (it
  stays at 47–81 ns per 256 rows on Zen 5, 23–30 on NEON). Masked sums are
  reported against each arm's own unmasked sum.
- `plane256` uses a per-block base (FOR), SeriesPack none; with uniform
  values the stored widths match.
- Warm cells fit L1/L2; flushed cells are conditioning, not DRAM service.

## What the performance thread should take on

In order of the gap's size and how often Frame will hit it: a SUM and filter
fast path for every striped width (a COUNT or mask over many windows, not
one 64-row call each); compact SUM and filter at non-byte widths; the K = 24
anomalies; striped points; NEON compact decode. `plane256`'s leaves
(`../bodies/`) are prior art for the first two; they belong inside
SeriesPack's kernels, not beside them.

The iteration on these gaps, its findings and what remains are in
[performance.md](performance.md).
