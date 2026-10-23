# Execution substrate, first probe: dispatch is not phase 1's cost

Front B ([partition](../PARTITION.md)), 2026-10-23. Does phase 1's per-block
cost come from how blocks are dispatched, or from the work inside each block?

## What was measured

[`plan.cpp`](plan.cpp) reads phase 1's encoded images unchanged. It turns the
descriptors into columns, groups block ids by (kind, width) once per image, and
runs each group in one loop with its leaf chosen outside the loop. Packed
blocks call the same catalogue kernels phase 1 uses. Run lengths are computed
from adjacent ends with no carried dependency, and width-8 run values are read
in place. Delta, patched and coded blocks fall back to phase 1's owner leaf.

[`measure.cpp`](measure.cpp) times four arms on the same rows: phase 1's owner
(`encoded_sum`, `encoded_count_ge`), phase 1's catalogue arm, the batched probe
and a native global-FOR array in its narrowest byte lane. The native loops
accumulate in u32 over 65,536-row chunks so they vectorise. Every call's answer
is checked against an exact oracle over the original values. COUNT uses the
median as threshold. Arms rotate each round and each call is a warm repeat.

Workers: Zen 5 (AMD EPYC 9R45, c8a.2xlarge; AVX2 and AVX-512 builds) and
Graviton 4 (Neoverse V2, c8g.2xlarge), Clang 21.1.8, source `8b85383c`, three
process repeats of 41 rounds. Jobs `20261023T213006Z-6767bfc7` and
`20261023T213036Z-29106869`. Repeat spread is within 1.2× in every cell, and
mostly within 1.03×. All medians are in
[evidence/batched-v1-summary.txt](evidence/batched-v1-summary.txt).

## Results

Nanoseconds per 256-row block. "One part" is 65,536 rows (L2-resident on
both hosts); "full" is 2,999,808 rows (the image sits in LLC on both).

| Column, operation | Host | Owner, one part | Batched, one part | Native, one part | Owner, full | Batched, full | Native, full |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OS SUM | AVX-512 | 17.7 | 15.2 | 2.5 | 31.0 | 27.2 | 2.6 |
| OS SUM | Graviton 4 | 26.0 | 20.1 | 10.2 | 43.5 | 36.2 | 12.7 |
| OS COUNT ≥ median | AVX-512 | 19.6 | 13.8 | 4.5 | 31.9 | 27.0 | 4.5 |
| ResolutionWidth SUM | AVX-512 | 19.9 | 18.0 | 2.8 | 33.8 | 37.3 | 4.6 |
| EventTime SUM | AVX-512 | 28.8 | 28.5 | 4.8 | 27.0 | 31.5 | 9.5 |
| URLHash SUM | AVX-512 | 72.1 | 104.2 | 9.1 | 73.8 | 62.8 | 41.2 |
| WatchID SUM | AVX-512 | 37.9 | 38.1 | 10.2 | 45.2 | 41.4 | 38.7 |
| UserID COUNT ≥ median | AVX-512 | 4.3 | 1.0 | 7.7 | 4.1 | 0.8 | 25.0 |
| UserID COUNT ≥ median | Graviton 4 | 8.5 | 1.4 | 28.2 | 9.3 | 1.6 | 89.8 |
| CounterID SUM | AVX-512 | 3.2 | 0.6 | 2.5 | 3.1 | 0.2 | 4.1 |

## Findings

1. **Dispatch is a small part of phase 1's cost.** Batching changes the
   owner's time by between −80% (closed-form cells) and +45%. On run- and
   packed-heavy columns it saves 0–30%. Phase 1's OS SUM spends about 15 ns per
   block in the leaf at L2 against the native array's 2.5 ns. My earlier claim
   that phase 1 mostly measured dispatch overhead was wrong; STAGES.md and the
   README are corrected.
2. **The unit of batching barely matters when access is predictable.** A follow-up run (`166b8727`,
   jobs `20261023T214551Z-63932352` and `20261023T214629Z-dc609e2b`) holds
   bytes and leaves fixed and varies only the unit: one switch per block in
   address order, one hoisted loop per address-order segment of equal kind
   and width, or the whole-image kind index. The three stay within about 10%
   of each other on run- and packed-heavy columns (OS SUM at one part, AVX-512:
   15.7 / 15.9 / 15.2 ns per block; native 2.6). Only closed-form cells gain
   from the kind index (UserID COUNT 2.4 → 0.9). URLHash's slowdown against
   phase 1 is not ordering: it is as slow per block in address order. Its
   first part is all width-64 run blocks, where this probe's u128 run leaf is
   slower than phase 1's on Zen 5 (faster locally). Medians:
   [evidence/units-v1-summary.txt](evidence/units-v1-summary.txt).
3. **Frame's leaves are residence-sensitive; the native array is not.** The
   first "one part" comparison mixed residence with data, because the first
   part's blocks differ from the column's. A same-data run (`03792b90`, jobs
   `20261023T215742Z-2882c078` and `20261023T215808Z-35c5647f`) times each
   65,536-row part warm on its own and adds them up. On AVX-512 that makes
   Frame 1.3–1.65× faster than one full-slice pass (OS SUM 30.9 → 21.2 ns per
   block, RegionID 49.3 → 30.3, UserID SUM 56.4 → 37.8), while the native
   array barely moves (OS 2.7 → 2.5). Graviton 4 shows the same pattern. A
   part's Frame image is about 22 KB on OS, so "part-resident" is close to L1
   for Frame. Prefetching each body eight blocks ahead changes nothing, so
   body acquisition at that distance is not the cause; the leaves themselves
   pay for L1 misses. Unattributed until front A qualifies counters. The
   native per-part control uses per-part FOR, so it is a stronger control
   than the global array. Medians:
   [evidence/residence-v1-summary.txt](evidence/residence-v1-summary.txt).
4. **Closed forms deliver what M1 predicts.** UserID COUNT is 31× faster than
   the native array on Zen 5 and 56× on Graviton 4 at the full slice, and
   CounterID and IsRefresh decide from descriptors. Batching makes these cells
   4–15× faster than phase 1's owner, because the bound checks run over a
   descriptor column.
5. **SUM over short runs is a representation limit, not only a leaf cost.**
   OS stores a median of 24 runs per block (mean 35) at 6–8 bits. Native u8
   SUM costs 2.5 ns per 256 rows on AVX-512, so a run leaf must spend under
   about 0.07 ns per run to match it. Even a fully vectorised leaf reaches
   parity at best. Runs pay for SUM only when blocks hold few runs, so runs
   versus packed is a per-operation choice (the weighted class in STAGES.md).
   Full-block SUM can also come from block summaries, which Ashton placed on
   the parent segment; body SUM matters for selected rows.

## Predicate(A) → SUM(B)

[`select.cpp`](select.cpp) computes `SUM(B) WHERE A >= t` on phase 1's images
of two columns, at four selectivities, in original order and with A and B's
blocks permuted together (same rows, same bytes). Frame staged emits A's row
masks with phase 1's emitter, derives B's coarse mask from A's per-block
counts and runs phase 1's masked SUM; Frame fused does both per block while
the mask is live. Native staged builds a bitmap and sums its set rows; native
fused is a branch-free pass over both arrays. Warm repeats, jobs
`20261023T220316Z-475d558f` (Zen 5) and `20261023T220351Z-8137fae3`
(Graviton 4), source `9ba76dd1`. Medians:
[evidence/select-v1-summary.txt](evidence/select-v1-summary.txt).

| Pair, AVX-512, original order | Density | Blocks touched | Frame fused µs | Best native µs | Native / Frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| UserID → URLHash | 0.1% | 0.2% | 76.0 | 432.0 | 5.7× |
| UserID → URLHash | 10% | 10% | 140.6 | 620.6 | 4.4× |
| UserID → URLHash | 50% | 50% | 577.1 | 1188.1 | 2.1× |
| EventTime → WatchID | 1% | 45% | 919.7 | 324.1 | 0.35× |
| EventTime → WatchID | 50% | 77% | 747.9 | 1035.9 | 1.4× |
| OS → UserID | 2% | 58% | 1678.7 | 300.5 | 0.18× |
| RegionID → ClientIP | 10% | 89% | 2304.0 | 231.6 | 0.10× |

Graviton 4 shows the same split: UserID → URLHash 4.2–7.7× faster than native
below 10% density, every predicate that spreads across blocks 0.3–0.5×.

1. **Pruning is the win, and only where A's matches cluster by block.** When
   the selected rows fill few blocks, Frame skips B's other blocks entirely
   and beats the best native plan 2–7.7×. When A's matches touch most blocks,
   Frame pays a full predicate pass over run-heavy A plus masked leaves over
   B, and loses 3–10× to a native pass that streams both arrays.
2. **Losing cells are leaf-bound again.** RegionID → ClientIP at 10% spends
   2.3 ms against 0.23 ms native fused, with 89% of blocks touched. The same
   leaf work as the full scans (run predicates, masked plane sums) dominates.
3. **Fused per block beats staged by 5–35%.** Keeping the mask live removes
   one pass over A's masks and the coarse derivation; it never lost.
4. **Whole-block permutation costs Frame 0–60% warm, native almost nothing.**
   This is not the phase 1 scattered cold loss (that needs a cold harness,
   front A's); warm, the permuted layout mainly breaks sequential order for
   Frame's variable-size bodies.

### Flushed inputs

The same pairs with every input the arm reads flushed before each call
(`fedf883b`, jobs `20261023T224308Z-b83c96ac` and `20261023T224341Z-754bbd8d`;
[evidence/select-v2-summary.txt](evidence/select-v2-summary.txt)). On Zen 5
the native fused pass slows from about 0.8 to 1.9 ms on OS → UserID, while
Frame barely changes, so Frame's losses shrink (10% density: 0.34 → 0.66 of
the best native time) without turning into wins. UserID → URLHash stays a
3–6.5× win. Permuted blocks now cost Frame up to 1.65× (10%: 266 → 437 µs),
the pattern phase 1 saw scattered and cold.

## Unpredictable block order and coarse masks

Ashton's challenge: grouping might matter once accesses defeat the hardware
prefetcher. [`masks.cpp`](masks.cpp) sums the blocks a supplied coarse mask
selects, warm and with inputs flushed before every call. Besides address
order it visits the same blocks in a random order, that random order
regrouped by kind, and the random order with a two-stage software prefetch
(descriptor 16 blocks ahead, body 8 ahead). The native control sums the same
256-row spans in the same orders. Jobs `20261023T224623Z-e32c5d98` and
`20261023T224659Z-ae5ed20f`, source `1cf8a90c`. Medians:
[evidence/masks-v1-summary.txt](evidence/masks-v1-summary.txt).

| All blocks, flushed, AVX-512 (ns per block) | Address order | Random | Random, grouped by kind | Random, staged | Native address / random / random staged |
| --- | ---: | ---: | ---: | ---: | --- |
| OS | 32.4 | 94.7 | 96.9 | 39.6 | 6.1 / 26.8 / 18.9 |
| EventTime | 29.9 | 137.7 | 137.8 | 87.0 | 22.2 / 48.2 / 34.7 |
| UserID | 49.4 | 161.4 | 157.3 | 109.5 | 56.1 / 114.0 / 69.7 |
| WatchID | 53.5 | 238.5 | 236.2 | 199.5 | 66.7 / 106.7 / 66.3 |

1. **Visit order matters a great deal once the prefetcher is defeated.** Cold
   and random, Frame is 2.9–4.6× slower than in address order; warm, 1.1–2×.
   The native array loses 1.6–4.4× too, but Frame's two-level, variable-size
   chain is hit harder.
2. **Grouping by kind does not recover it.** Regrouping the random order by
   kind changes nothing: the loss is locality, not dispatch. Batching only
   helps if it restores address order or makes addresses known early.
3. **Software staging recovers most of it on small bodies.** Prefetching
   descriptors then bodies brings OS from 94.7 to 39.6 ns per block (address
   order: 32.4) and EventTime from 137.7 to 87.0, but WatchID's 500-byte
   bodies only from 238.5 to 199.5: three prefetched lines cover a small
   body, not a wide one. Staging must cover the body's actual extent.
4. **Coarse masks cost little to consult.** Phase 1's owner (which skips empty
   16-block groups) and a plain set-bit walk stay within a few percent at
   every density; the mask walk is not where time goes.

5. **Prefetching each body's real extent closes most of the rest on Zen 5.**
   Covering the body to the next block's offset (`cea613bc`, jobs
   `20261023T225749Z-ed9a8552` and `20261023T225825Z-147c1397`;
   [evidence/masks-v2-summary.txt](evidence/masks-v2-summary.txt)) takes cold
   random-order WatchID from 191.0 to 96.0 ns per block (address order 49.8),
   UserID from 109.4 to 66.0, EventTime from 86.0 to 47.0 and URLHash from
   145.4 to 101.3, which matches its address-order time. Frame then beats the
   native array visited in the same random order without prefetch (UserID
   66.0 vs 112.1) and is within 1.0–1.5× of native random with prefetch.
   OS, with tiny bodies, gets slightly worse (39.6 → 47.9): the extra
   descriptor reads cost more than they save. Graviton 4 gains less (UserID
   161.0 → 126.5; WatchID unchanged).

6. **Lookahead distance is not the lever.** Sweeping the body distance from 2
   to 32 blocks (descriptors at twice that; `cab5870f`, jobs
   `20261023T230748Z-9fd88ef5` and `20261023T230824Z-8236bb53`;
   [evidence/masks-v3-summary.txt](evidence/masks-v3-summary.txt)) moves cold
   random-order cells by about 20% at most: the best distance is 4–8 on Zen 5
   and 2–4 on Graviton 4, flat beyond. Graviton 4's wide bodies stay far from
   address order at every distance (WatchID 313–360 vs 157 ns per block;
   URLHash 268–316 vs 139), which points at a cap on outstanding prefetched
   lines rather than lead time. That host-specific limit is a question for
   front A's measurements. Loom's hint interface maps demand classes to hint
   bodies but supplies no calibrated lookahead (its 32 default is a loop
   shape), so Frame cannot borrow a distance from it today.

So the substrate's job under unpredictable access is staging with prefetch
extents taken from descriptors, not regrouping by kind; a short fixed
lookahead is enough, and wide bodies on Graviton 4 need a different remedy
than more lead time.

## Independent point reads

[`points.cpp`](points.cpp) reads 65,536 random rows through phase 1's getter,
directly and with the next requests' descriptors and bodies prefetched, beside
the native narrow-FOR array with and without prefetch. Jobs
`20261023T220919Z-0291e583` (Zen 5) and `20261023T220947Z-9edf003c`
(Graviton 4), source `68dc725b`. Medians:
[evidence/points-v1-summary.txt](evidence/points-v1-summary.txt).

| Column (dominant kind) | Host | One part: native / Frame direct / staged | Full slice: native / Frame direct / staged |
| --- | --- | --- | --- |
| WatchID (packed) | AVX-512 | 0.59 / 4.7 / 5.4 ns | 1.18 / 9.4 / 8.4 ns |
| OS (runs) | AVX-512 | 0.48 / 26.4 / 25.8 ns | 0.82 / 36.3 / 31.8 ns |
| UserID (runs) | AVX-512 | 0.56 / 29.7 / 29.1 ns | 1.07 / 46.4 / 41.6 ns |
| UserID (runs) | Graviton 4 | 0.88 / 32.0 / 34.4 ns | 7.63 / 120.0 / 101.6 ns |
| URLHash | Graviton 4 | 0.98 / 37.7 / 37.9 ns | 7.53 / 92.7 / 70.0 ns |

1. **Point cost is the in-block leaf, not acquisition.** With everything in
   L1/L2, a run block's point read costs 26–38 ns against 0.5–1 ns native; a
   packed block's costs about 5 ns. Run search and the getter's per-kind setup
   dominate.
2. **Staging helps only where misses exist.** Prefetching the next requests'
   descriptors and bodies saves up to 1.3× on Graviton 4's full slice
   (URLHash 92.7 → 70.0 ns) and about 1.1× on Zen 5, and costs a little when
   resident. The native array gains similarly from prefetch.
3. The 3M-row slices fit both hosts' LLC (32–36 MB), so none of these cells is
   DRAM-resident; the DRAM tier needs larger slices on the workers.

4. **Phase 1's getter never used Intervals' native search.** It passes run
   ends through a `size()`/`at()` wrapper, so Intervals falls back to plain
   bisection. Searching the u8 ends through `direct_source` instead
   (`eef398ad`, jobs `20261023T225108Z-d4a30327` and
   `20261023T225137Z-142e22fc`) cuts resident points on Zen 5 from 37.8 to
   8.2 ns (EventTime) and 37.0 to 6.7 ns (URLHash), but only 27.1 → 20.1 ns on
   OS and 30.4 → 22.3 ns on UserID. On Graviton 4's full slice it halves
   UserID (99.8 → 43.0 ns). The remaining OS and UserID cost sits where the
   native search bisects a window wider than one vector or scans a window
   narrower than one vector one element at a time. That is the Intervals
   optimisation thread's question. Medians:
   [evidence/points-v2-summary.txt](evidence/points-v2-summary.txt).

The point leaf belongs to front C. Staging stays here, to be re-measured once
leaves are near their floor and on DRAM-resident slices.

## Next

Ikea LEAD's review proposes separating the units before committing to
descriptor columns or a kind index, with bytes and logical work fixed: the
ordinary per-block operation; the same leaf called directly over a
homogeneous run of blocks; preparation hoisted; then kind batching. Traces:
constants, short and long runs, FOR; homogeneous and mixed kind/width; all,
none and scattered coarse domains. Charge index construction and maintenance,
and keep original coordinates: reordering is lawful for exact integer SUM, not
for ordered outputs, first-match operations, prefix scans or ordered float
laws. Timing reuses phase 1's [scan schedule](../../frame-phase-1/scan/run.py)
(balanced positions and predecessors, every call checked) and its independent
raw-record audit.

- Leaves at their floor, measured against the native array per width:
  direct Local bit-plane SUM and COUNT for packed blocks (popcount per plane,
  no unpack), and a vectorised run leaf.
- Attribute the leaves' residence sensitivity once counters are qualified.
- Leaves for predicate emission over runs and for masked plane sums, which
  decide every losing selection cell.
- The same pairs cold, through front A's conditioning harness.
