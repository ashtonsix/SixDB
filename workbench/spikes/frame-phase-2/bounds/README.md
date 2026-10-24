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

## A faster search: sample proposes, one window-local pass verifies

Ashton (2026-10-24, after the first round): bring the bounded search toward
the cost of an unbounded one, using fewer samples, partial information from
samples or priors; points should barely notice capped u8 patch positions.
Jobs `20261024T161234Z-696f6ac2` (Zen 5) and `20261024T161234Z-952ad389`
(Graviton 4), [evidence/time-v9-summary.txt](evidence/time-v9-summary.txt);
bytes in [evidence/census-v39-summary.txt](evidence/census-v39-summary.txt).

`fast::guess_local` ([fast.h](fast.h)):

1. **Minimum and maximum** (FOR's own pass).
2. **The sample decides.** 16 or 32 strided values, sorted in one vector
   (compile-time bitonic network). The best window that excludes m sampled
   values is the narrowest span of S - m consecutive sorted values, so every m
   is a shift, a subtraction and a lane minimum: the *exclusion ladder*. No
   pair search, no deduplication (runs are handled because a window's values
   include all equal values). If no exclusion pays and the prior is full width
   too, the block is done.
3. **One window-local pass verifies.** Every priced window lies within
   [lo - 2^(K+1), lo + 2^(K+2)), so the pass stages values relative to a base
   below that range with saturation at both ends: compares stay exact while
   lanes need only K + 3 bits. A 57-bit UserID block whose window is 1 bit
   wide is priced in u8 lanes. The pass prices the guess (with its inliers'
   extent), K-2, K-1 and K+1 at either end, and the prior window when it fits
   the local range.

| ns per block | Zen 5: today | sample 16 | sample 32 | Graviton 4: today | sample 16 |
| --- | --- | --- | --- | --- | --- |
| URLHash | 637 | 86 | 116 | 3556 | 187 |
| WatchID | 627 | 77 | 104 | 3453 | 161 |
| UserID | 537 | 173 | 209 | 2959 | 574 |
| ClientIP | 326 | 128 | 157 | 1807 | 383 |
| EventTime | 183 | 87 | 112 | 977 | 186 |
| RegionID | 168 | 188 | 223 | 865 | 528 |
| ResolutionWidth | 122 | 129 | 190 | 654 | 323 |
| OS | 100 | 176 | 220 | 475 | 470 |
| ConnectTiming | 115 | 170 | 207 | 587 | 488 |
| IsRefresh | 28 | 82 | 110 | 108 | 165 |

"Today" is lo = minimum with every width; on narrow residual lanes that
search costs 50–160 ns on Zen 5 and 125–400 on Graviton 4
(`narrow_min_exhaustive`), which stays the cheapest arm on narrow,
low-entropy columns. The guess with 32 samples loses at most 0.5% of bytes
against the oracle on every real column; with 16, at most 1.7% (OS).

8. **Wide columns now pay for choosing lo several times over.** On Zen 5 the
   window-local path is 2.5–8× faster than today's search on URLHash,
   WatchID, UserID, ClientIP and EventTime, and 4.7–21× faster on Graviton 4.
   Narrow columns are 1–1.8× slower than today's search on Zen 5 (IsRefresh,
   nearly constant, 2.9×). Their
   floor is now the sample (gather, sort, ladder: about 40–60 ns on Zen 5)
   plus the minimum and maximum.
9. **The prior is a candidate, not a gate.** Trusting the previous block's
   window and falling back on a learned test (an OR of thresholds over
   exception counts, width change, tail masses and sample distinct counts,
   [evidence/gate_search.py](evidence/gate_search.py)) still lost 1.5–2.4%
   at 20–27% fallbacks. An oracle-perfect gate would need to fall back on
   6–22% of blocks. Checking the prior window against the sample (32
   compares) covered 58% of the loss at 20% fallbacks. A stable prior is
   often stably wrong, because the best lo moves with the data. Pricing the
   prior beside the sample's guess in the same pass is cheaper and loses
   nothing.
10. **Patches cost points almost nothing when capped and compact.** Exceptions
    sit in compact columns: u8 positions padded to one 64-byte register per
    block, and decoded u64 values. Up to 64 per block, a point does one
    compare and a branch-free select (`compose.cpp`,
    [evidence/compose-v8-summary.txt](evidence/compose-v8-summary.txt)).
    With a cap of 64, OS points are 12.2 ns against today's 12.0, with 4.1%
    fewer bytes. RegionID is 9.6 against 9.4 (1.2% fewer), and ResolutionWidth
    10.5 against 6.8 (3.3% fewer); its exception rows are common inliers' zeros.
    A cap of 64 removes ClientIP's and UserID's saving: it came from blocks
    with more than 64 exceptions (one dominant value), which are an RLE or
    dictionary case. The SeriesPack thread measured the same field-first
    effect independently
    ([patch/README.md](../seriespack/patch/README.md)).

Negative results this round: pricing all seven windows in one fused pass
spills 4W 64-byte accumulators (2× slower on both hosts), so windows are
priced one per pass. A coarse u16 sample (dropping low bits of wide
residuals) loses narrow windows in wide domains (UserID +11%). Zooming in
with saturation invents spans at the saturated edges. Reading the sorted
sample at the prior's predicted quantiles is cheap but loses RegionID
(1.4–2.8%), whose best exclusions vary block to block.

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

- The narrow-column floor: a cheaper gather of the sample (it is 32 scalar
  loads), the ladder in narrow lanes when vbits allows, and choosing between
  narrow lo = minimum and the sample path per column.
- A bad-guess test that predicts missed savings rather than exceptions. Tail
  masses and sample checks were not enough on their own.
- The cap as Engine's choice beside the penalty: blocks above 64 exceptions
  belong to RLE or dictionaries.
