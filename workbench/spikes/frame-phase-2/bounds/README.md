# The bounds stage: choosing lo and width

Ashton's critique of the runs spike's bounds stage (2026-10-24): it fixes the
base and searches every width, so one low outlier (a 9 among values near 1000)
ruins a block, and it pays a counting pass per width. He asked for a
guess-then-verify stage that also chooses the base: propose a few windows,
price them by optimistic encoding, recognise a bad guess, fall back.

The stage maps (values, cost model, prior) to an inlier window
[lo, lo + 2^K) and exceptions. The caller prices a choice
(`cost_model` in [bounds.h](bounds.h)): SeriesPack's tile bytes for the
series at width K, exception bytes (position, value relative to lo with a
sign bit once any lies below lo) and an optional per-exception penalty that
Engine sets for read time. Physical cost need not be monotone in K
([pfor-bounds](../../../notebook/pfor-bounds.md)); every method searches
the table, not bit counts.

## Starting point

- [Residual-bound notes](../../../notebook/pfor-bounds.md): chunk extrema are
  cheap but merging them leaves clusters of exceptions; a valid guess can
  still waste bits; refining width at a fixed base is exact and monotone;
  previous-block priors were untried.
- Calico Keyset POST (`calico/keyset/include/keyset/detail/post.h`): lo is the
  block minimum, widths prior-1..prior+1 with a full search every 16th box.
  It never recognises a bad guess; the refresh is its only fallback.
- The runs spike: the no-RLE path ([pipelines.cpp](../runs/pipelines.cpp))
  uses lo = block minimum and one counting pass per width (up to 63); run
  sizes ([read.cpp](../runs/read.cpp)) use lo = 0.

The inliers of any window are a contiguous stretch of the sorted values, so a
choice with i low and j high exceptions is fixed by the (i+1)-th smallest and
(j+1)-th largest values. The oracle ([`exact`](bounds.h)) sorts and, for
each width, finds the window holding most values (two pointers).

## The method ([fast.h](fast.h))

Residuals x - min are staged at the narrowest lane that holds them (u8 to
u64), so every pass compares narrow lanes.

1. **Guesses.** The oracle on a 32-value strided sample over its distinct
   values (excluding up to 6 distinct values a side, bodies scaled by the
   stride; the timed build allowed 8), the sample's mode at the narrowest width, the previous block's
   window, and the full width.
2. **Verify.** One counting pass per window prices each guess exactly.
3. **Refine.** The cheapest is moved to its inliers' minimum and narrowed to
   their extent, then priced with widths K-2, K-1 and K+1 anchored at either
   end; one round.

Order statistics (exact over at most k exceptions a side) are an optional
extra guess. The emission pass that pulls exceptions out recounts the final
window.

## Findings

Inputs: 11 ClickBench columns (3M rows, phase 1's worker inputs), 256-row
blocks, constant blocks excluded, and 7 generated cases. Bytes are
deterministic ([census.cpp](census.cpp),
[evidence/census-v21-summary.txt](evidence/census-v21-summary.txt)); timings
are Zen 5 (c8a.xlarge, AVX-512) and Graviton 4 (c8g.xlarge), warm, median
of three process repeats, jobs `20261024T143159Z-a55b6554` and
`20261024T143159Z-b1d9afe4`
([evidence/time-v3-summary.txt](evidence/time-v3-summary.txt),
[evidence/compose-v3-summary.txt](evidence/compose-v3-summary.txt)).

| Column | exact bits/value | today (lo = min, every K) | Keyset prior ±1 | guess–verify–refine | + order statistics |
| --- | --- | --- | --- | --- | --- |
| ClientIP | 30.32 | +4.53% | +5.40% | +0.01% | +0.01% |
| ConnectTiming | 4.04 | 0.00 | +1.50 | +0.08 | +0.06 |
| CounterID | 3.92 | +11.51 | +16.04 | 0.00 | 0.00 |
| EventTime | 16.89 | +0.14 | +0.17 | +0.09 | +0.02 |
| OS | 6.45 | +5.91 | +7.50 | +0.48 | +0.26 |
| RegionID | 9.92 | +2.92 | +11.82 | +0.51 | +0.65 |
| ResolutionWidth | 10.32 | +4.60 | +5.05 | +0.22 | +0.10 |
| URLHash | 63.81 | +0.27 | +0.30 | +0.01 | +0.01 |
| UserID | 47.67 | +7.74 | +10.66 | +0.04 | +0.03 |
| IsRefresh, WatchID | | 0 | 0 | 0 | 0 |
| gen_low (Ashton's 9 among ~1000) | 5.16 | +80.6 | +80.9 | +0.26 | +0.25 |
| gen_two (rare outliers both sides) | 8.67 | +123.9 | +132.7 | +1.67 | +0.15 |
| gen_ts (late timestamps) | 12.34 | +31.1 | +31.1 | +0.14 | +0.03 |

1. **Choosing lo is worth 3–12% of packed bytes on real columns** (OS, ClientIP,
   ResolutionWidth, UserID, CounterID) and 30–124% on low or two-sided
   outliers. Today's stage cannot represent those choices.
2. **Guess–verify–refine stays within 0.5% of the oracle on every real column**
   and within 1.7% on every generated case.
3. **The sample is the essential guess.** Order statistics alone lose 3–83%
   on heavy one-sided tails and run-heavy blocks: the best choice there
   excludes every occurrence of a value, so multiplicity matters (as the
   notes predicted). Sampling over distinct values and adding the mode fixed
   UserID (2.9% → 0.04%), where the best window is one value at K = 1 with
   everything else an exception. The prior helps on stationary columns, and
   one refine round removes most of what is left.
4. **Speed on Zen 5 (ns per block, bounds only):** 150–460 against today's
   27–630. It is faster on wide columns (URLHash 418 vs 630, WatchID 391 vs
   613, UserID 458 vs 529) and slower on narrow ones (OS 206 vs 94,
   ResolutionWidth 283 vs 121). On Graviton 4 it is 1.0–3.4× faster than
   today except on near-constant columns: ClientIP 658 vs 1838, URLHash 1058
   vs 3589, OS 428 vs 476. The oracle costs 0.8–37 µs per block.
5. **Narrow lanes are a separate win for today's stage.** The same lo = min
   search on staged residuals takes 41–728 ns on Zen 5 (OS 55 vs 94) and
   122–2099 on Graviton 4 (ClientIP 651 vs 1838). On narrow columns it is
   the cheapest arm, so it is the right fast path where lo = min is enough.
6. **Composed with SeriesPack, bytes trade against read time**
   ([compose.cpp](compose.cpp); every SUM, COUNT ≥ median and point checked).
   With no penalty, the stage cuts packed bytes 4–7% on OS, ClientIP,
   ResolutionWidth and UserID, but takes 2–4× the exceptions, and patching
   slows points (ClientIP 5.6 → 14.9 ns on Zen 5) and SUM. A penalty of 2
   bytes per exception still saves 1–5% against today on OS, ClientIP,
   ResolutionWidth and UserID, with OS SUM 22 → 18 ns and points 28 → 22 ns,
   but costs RegionID 1.3%. At 8 bytes reads are mostly faster than today
   and bytes grow (OS +3.9%, ConnectTiming +50%). The penalty is therefore a
   weighted choice for Engine, and the patch cost is the SeriesPack thread's
   to cut.
7. **Run sizes gain nothing from lo** ([sizes.cpp](sizes.cpp),
   [evidence/sizes-v1.jsonl](evidence/sizes-v1.jsonl)). Sizes minus one
   almost always contain 0, so lo = 0 with every width is within 0.2% of the
   oracle, and the guess loses up to 0.25% on group Bulk tiles. Run sizes
   should keep lo fixed at 0, which is an admitted-candidate choice in the
   cost model, not a different stage.

### Negative results

- **Prior-first gating fails.** Using the prior alone and running the sample only
  when bytes jump more than 0–10% above the previous block (with a
  refresh every 8–64 blocks) left 3–7% regret on OS, ClientIP, RegionID and
  UserID while still sampling 25–55% of blocks. A stable prior can be stably
  bad, so a cost jump does not detect missed compression.
- **Prior plus local refinement alone** loses 4–11% on real columns. It cannot
  move lo far enough.
- **A two-sided-exceptions test then fallback** (to the oracle or to order
  statistics) fires on 13–28% of real blocks and recovers at most 0.4 points.
  The guess is already close enough that a fallback buys little here. A
  test that predicts large missed savings cheaply is still open.
- **Keyset's prior ±1 at lo = min** is worse than today's full search on every
  column with structure (RegionID +11.8%).

### Code generation (both hosts)

Clang vectorised the counting loop across registers with gathers on AVX-512
(250 ns a pass for u64) until the outer loop was written over clang vector
extensions. Byte-lane counters reduced lane by lane cost 20 ns a pass until
widened by `__builtin_convertvector`. A compile-time bitonic sort of the
32-value sample (`vector_sort`) takes about 10–20 ns, against 150–300 ns
for `std::sort`.

## Representation and seams

A packed block carries lo (u64), K, its slot in its width's SeriesPack array
and exceptions (u8 position, signed x - lo). Exception slots store field 0.
Operations compensate for lo:

- SUM = fields + lo × 256 + Σ (x - lo) over exceptions
- COUNT(x ≥ t) = every non-exception row if t ≤ lo, else fields ≥ t - lo;
  then each exception by its own value. The block minimum is no longer lo,
  so the `t ≤ base → whole block` shortcut must use the exceptions.
- point = field + lo unless patched

`compose.cpp` does this at the decode level as a control. Fused
compensation inside SeriesPack operations belongs to the SeriesPack
operations thread.

## Next

- Faster sample and windows: one fused counting pass for all windows, a
  narrow-column path that tries narrow lo = min first.
- A cheap test for large missed savings (the open bad-guess question).
- Fused patch reads in SeriesPack, which decide where the penalty should sit.
