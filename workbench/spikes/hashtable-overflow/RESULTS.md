# Overflow experiment: Zen 5

**Keep the original Swiss ceiling. Eight-bit tags are a useful small refinement;
filtered continuation has not established a consistent whole-table advantage.** Increasing
the ceiling saves a modest fraction of whole-table memory but substantially
hurts misses and new-key writes. It does not repair the gaps against Boost or
Abseil. These are research recommendations; no production header changed.

## Scope and controls

AMD EPYC 9R45 (Zen 5), one pinned CPU, Clang 21.1.8, `-O3 -g0 -DNDEBUG`,
`-march=znver5 -mtune=znver5`, inline threshold 1000, function alignment 64.
Actual Boost 1.92.0 `unordered_flat_{set,map}` and Abseil
`2065f4ded0558c6f89fee67c8e5228feb4eb960e` `flat_hash_{set,map}`, native APIs
and default hashers. Counting allocators delegate to `std::allocator` and do
accounting only on allocation/deallocation. Peers reserve the common initial
cardinality; capacity rounding is an observed result. Allocated peer bytes omit
their small owner object; Ikea bytes include its owner.

All Ikea variants have the same seed, single-product permutation, primary
geometry and measured encoding selection. K=30 (E=30), b=10 for warmed tables
and b=20 for large tables; sets, eight-byte maps, and hot 64-byte maps. Every
value limb is consumed. Three key/hash seeds, three read repetitions per seed,
one build and replacement treatment per seed, and ten churn rounds. Treatment
order rotates/reverses across seeds. Every caller verifies complete membership,
payloads and read-result digests; all treatments have identical requests/results.

Hot tables occupy L2/LLC-sized footprints, not necessarily L1. Large tables
use a 256 MiB conditioning walk before each measurement; this does not establish
that every access reaches DRAM. Cold reads below use software lookahead 32 for
Ikea and Abseil, including prime/tail costs. Boost has no public home prefetch
hook, so its native lookup including internal candidate prefetches is the
comparator. This is independent throughput with inline result consumption,
not serial latency, a Loom microtask ring, or an interleaved consumer pipeline.

The density panel stops the common initial population at specified Swiss spill
counts. It is deliberately near 90% **primary** occupancy, not a new recommended
primary load. Threshold changes that do not change physical capacity should not
be read as different density experiments.

## Whole-table reads

Representative initial spill=925 per 1024 primary buckets; large cases scale
both by 1024. Baseline/tag8 Swiss load is 45.17%; filter15 is 48.18%; the dense
filter15/31 arm reaches 96.35% in half the Swiss groups. Primary occupancy is
about 90%; individual seed cardinalities are retained.

Times are **ns/lookup**, lower is better. Each triple is **hit / 50% miss /
all miss**. Values are medians over three seeds × three repetitions.

| Owner | Hot set | Hot map8 | Hot map64 |
| --- | --- | --- | --- |
| Baseline | 2.27 / 11.05 / 5.52 | 2.76 / 11.05 / 5.48 | 3.90 / 11.45 / 5.94 |
| Tag8, 87.5% ceiling | 2.24 / 10.68 / 5.22 | 2.71 / 10.80 / 5.30 | 3.95 / 11.43 / 5.91 |
| Filter15, 87.5% ceiling | 2.29 / 10.81 / 5.56 | 2.84 / 11.05 / 5.74 | 3.97 / 11.49 / 6.10 |
| Filter15, 96.875% ceiling | 2.47 / 12.16 / 7.84 | 2.95 / 12.13 / 7.82 | 4.09 / 12.69 / 8.21 |
| Boost | 1.52 / 6.43 / 1.15 | 1.79 / 6.47 / 1.19 | 3.09 / 6.83 / 1.28 |
| Abseil | 1.32 / 6.52 / 1.67 | 1.80 / 6.92 / 1.87 | 2.84 / 7.16 / 1.73 |

| Owner | Cold set | Cold map8 | Set / map8 allocated MiB |
| --- | --- | --- | --- |
| Baseline | 13.96 / 16.45 / 10.28 | 29.24 / 25.42 / 10.64 | 80 / 224 |
| Tag8, 87.5% ceiling | 14.11 / 15.75 / 9.52 | 29.20 / 25.27 / 9.92 | 80 / 224 |
| Filter15, 87.5% ceiling | 14.08 / 15.86 / 9.67 | 29.42 / 25.68 / 10.06 | 79.1 / 222.1 |
| Filter15, 96.875% ceiling | 11.32 / 17.98 / 13.06 | 27.52 / 27.75 / 13.62 | 71.6 / 207.1 |
| Boost | 23.31 / 23.89 / 14.36 | 29.19 / 26.76 / 15.09 | 272 / 512 |
| Abseil | 13.50 / 16.28 / 11.73 | 22.62 / 20.89 / 12.82 | 288 / 544 |

At the original ceiling, tag8 makes these cold all-miss cases 7.4%/6.8% faster
than baseline; after churn the improvements are 9.4%/8.4%. Hits are roughly
unchanged. Filter15 does not beat tag8 here despite its continuation filter.
Its denser version improves cold hits through a smaller footprint but loses on
mixed and absent queries. Tag8 is still **29% slower than Abseil on cold map8
hits** in this caller; the favorable absent-key result must not hide that gap.
Boost is distinctly stronger hot, using 1.8–3.6× baseline whole-table memory
across these geometries.

The hot mixed-query cost exceeds both pure-hit and pure-miss costs: random
outcomes expose branch costs. These timings include that caller behavior;
they are not a branch-free primitive cost or a prediction for every consumer.

## Writes and maintenance

Same initial population. Build includes allocation/reserve and insertion of all
precomputed inputs. Replacement uses the existing-key optimized Ikea strategy
versus native peer insert/assign; upsert50 follows erasure of the new-key half.
Cold conditioning precedes each batch, not each operation. Churn is **amortized
ns per erase+refill pair** over the nine pure refill rounds, including their
rehashes, then median across seeds. The preceding mixed round is reported
separately. This is not a median round that hides occasional rebuilding.

| Owner | Hot map8 build / replace / upsert50 | Hot map8 churn pair | Cold map8 build / replace / upsert50 | Cold map8 churn pair |
| --- | --- | --- | --- | --- |
| Baseline | 10.55 / 3.84 / 10.21 | 19.28 | 59.55 / 31.04 / 63.04 | 161.32 |
| Tag8 | 10.36 / 3.83 / 10.27 | 20.18 | 59.69 / 31.22 / 62.27 | 159.72 |
| Filter15, 87.5% | 10.59 / 3.75 / 9.96 | 19.44 | 58.94 / 32.78 / 63.83 | 168.94 |
| Filter15, 96.875% | 10.69 / 4.02 / 16.16 | 19.99 | 59.36 / 30.29 / 87.55 | 169.77 |
| Boost | 7.05 / 2.77 / 3.02 | 7.47 | 31.49 / 39.43 / 40.05 | 109.70 |
| Abseil | 7.95 / 1.99 / 5.74 | 15.14 | 40.04 / 26.99 / 39.94 | 147.12 |

Tag8 keeps ordinary writes near baseline; the hot churn sample above is 4.7%
slower, while the other map8 density population is 3.6% faster. There is no
consistent write gain to claim. Filter15's cold map8 churn is about 5% slower
than baseline. The dense filtered arm's upsert50 is 58% slower hot and 39%
slower cold. In this high-primary-load panel, both Ikea variants remain well
behind Boost for build, new-key insertion and churn, though Ikea's cold
replacement is faster. Retained tables include sets and map64 writes too.

## Swiss occupancy

Eight-bit tags reduce accidental full-key comparisons. They do not eliminate
the extra groups needed to find space or prove absence at high load.

With target925, tag8/30 and tag8/31 use 90.33% Swiss occupancy: cold map8
all-miss goes from 9.92 to about 14.9 ns. At target980, tag8/31 reaches 95.70%:
that cost becomes 19.61 ns versus tag8's 10.07 ns. The dense filter15 arm at
96.35% mitigates part of this penalty but remains worse than ordinary tag8.

The supplementary target890 cohort tests the **93.75% ceiling**, reaching
92.71% actual Swiss load in filter15/30 versus 86.91% in tag8/28. It reveals a
useful narrower tradeoff. Compare within this cohort, not across hosts:

| Map8 owner | Hot hit / mixed / miss | Cold hit / mixed / miss | Cold MiB | Hot / cold upsert50 | Cold churn pair |
| --- | --- | --- | --- | --- | --- |
| Tag8/28 | 2.86 / 12.50 / 7.94 | 28.74 / 27.40 / 13.64 | 208 | 12.36 / 75.52 | 164.24 |
| Filter15/28 | 2.86 / 11.25 / 5.61 | 30.16 / 26.39 / 10.17 | 222.1 | 10.44 / 64.69 | 172.72 |
| Filter15/30 | 3.08 / 11.99 / 7.09 | 28.76 / 27.53 / 12.07 | 207.1 | 15.85 / 88.73 | 172.92 |

Filter15/30 improves cold misses 11.5% and hot misses 10.7% versus tag8/28 at
nearly the same whole-table memory. It loses 28%/17% on hot/cold upsert50 and
5% on cold churn. Its original-ceiling arm is faster on misses but uses twice
as many Swiss groups here; that is a capacity effect as well as a continuation
comparison. **Filtered continuation deserves to remain a read-heavy research
option; it is not a superior general-purpose replacement.** The higher ceiling
trades 6.8% whole-table memory for slower misses/new-key writes relative to
filter15's own original-ceiling arm.

After ten churn rounds, the live Swiss population grows even at fixed total
cardinality: deleting a primary key does not pull a Swiss key back into its
hole. All initially dense treatments grow to the same group count as their
ordinary-density counterpart. Their initial 6–10% whole-table memory saving is
lost. The filtered erasure budget can clean at the same size, but cannot prevent
growth when live spill itself exceeds the limit.

## Implementation and verification

The first eight-bit implementation computed its tag in the common hash result,
adding work to primary writers. Moving it inside Swiss removed that regression.
The first continuation implementation assumed its erasure budget would prevent
a full stale-bit cycle; a legal directed trace disproved that assumption. The
corrected find, resolve and staged paths stop after every group has been visited.
The trace hung before the fix and passes afterward.

Local ASan/UBSan checks cover W=16/20/64, sets/map8/map64, random mutation,
saturating primary overflow counts, clone, RMW, staged reads and that full-cycle
case. Native callers independently check membership/payloads after mutations,
all removed keys, final full state and all timed result digests. Complete
primary read/upsert loops match across variants for K14/K30 × V0/8/64 and both
write strategies (36 comparisons). This does not qualify the filtered layout's
guarded damage publication or descriptor merge/growth.

## Evidence and decision

Main cohort: job `20261020T160412Z-5145231e`, 270 complete callers, 21,510 samples,
2,240 summary cells. It includes target925 and target980 at all three Swiss
ceilings, all peers and every write round. See
[retained measurements](evidence/density-main/summary/cases.csv) and its
[recovery reference](evidence/density-main/artifact.json).

Supplement: `20261020T163217Z-5abfe23e`, 135 callers, 10,755 samples and 1,120
summary cells at target890. It ran on a second EPYC 9R45 instance. All 40
benchmark binaries are byte-identical to the main cohort, and 103 measured
driver/overlay/codegen and production pack/table source files match. It also
re-emits all 36 primary loop comparisons with explicit `-DNDEBUG`; all match.
See [supplemental measurements](evidence/density-9375/summary/cases.csv) and
[codegen commands](evidence/density-9375/codegen/commands.json). The main
cohort's earlier codegen emission omitted explicit `-DNDEBUG`; its timed
benchmark binaries already had that definition. No timings from differing
programs are pooled. Together the cohorts cover all 405 intended callers.

The earlier broad panel (`20261020T154945Z-4eb3d939`, 330 callers) supports
tag8's small effect across primary loads 50/80/85/95 and K14/K30: median whole
read ratio 0.997, build 0.993, replacement 1.001, upsert50 0.997, erase 0.999,
refill 0.998 against baseline. Its unbounded filtered implementation is
superseded and excluded from this recommendation. Its
[recovery reference](evidence/broad-tag8-artifact.json) preserves the complete
panel; the main cohort uses the corrected filter.

**Decision:** keep tag8 as the candidate worth carrying forward at the current
ceiling. Do not raise the general default Swiss occupancy or promote filtered
continuation on these results; retain its 93.75% read-heavy tradeoff as research.
Wider SIMD probing remains an unmeasured separate question. The whole
table's hot mixed reads and high-load writes need broader changes than reducing
false fingerprint matches; this experiment does not establish a fix for them.
