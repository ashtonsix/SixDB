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
2. **The unit of batching barely matters.** A follow-up run (`166b8727`,
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
