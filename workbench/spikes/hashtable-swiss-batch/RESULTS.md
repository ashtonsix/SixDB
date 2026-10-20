# Wider SIMD, unchanged logical groups

**The masking works and recovers useful dense-overflow gains without extra
full-key checks. Keep the current default.** Four-group batching reduces dense
map8 all-miss time by 23% hot and 14% cold on Zen 5 AVX-512, but adds about 5%
to mixed cold reads. The AVX2 gain is smaller. This is a useful read strategy
for dense, miss-heavy overflow, not a general replacement for the maintained
reader. No production option or occupancy change is introduced by this study.

## What stays the same

All variants retain eight-bit tags, 16-slot logical groups, the 87.5% ceiling,
capacity, key/value placement, seed, hash and triangular probe order
`g, g+1, g+3, g+6, …`. The first group containing an empty terminates probing;
**all matching lanes in that group remain eligible**, including lanes after
its first empty. Every later group is masked out before tuple inspection.
Tombstones do not terminate a search. Nonadjacent groups must be assembled in
logical order; four adjacent physical groups are not the next four probes.

- `group16`: exact maintained reader.
- `batch32`: compare the next two logical groups. Adjacent groups use one YMM
  load; nonadjacent groups and wrap use separate loads.
- `batch64`: compare the next four logical groups, assembled into one ZMM on
  AVX-512 or two YMMs on AVX2.
- `tail64`: retain the first scalar group, then batch four logical groups.
- `line32` / `line64`: batch only eligible probes within the first control
  cache line, then continue the scalar triangular sequence.

The final candidate changes ordinary/guarded overflow reads only. Its writes,
RMW and staged readers retain the original engine. Initial panels changed
lookup and mutation resolution together; their write cost is discussed below.

## Final combined candidate: map8

Independent whole-table reads, ns/lookup, lower is better. Each cell is
**hit / 50% miss / all miss**. Hot uses direct lookup; cold uses prefetch distance
32 for Ikea/Abseil. Boost uses its native lookup because this harness has no
native equivalent prefetch API. The complete direct/prefetched and routed-only
grids are in the [AVX-512](evidence/readonly/avx512/summary/cases.csv) and
[AVX2](evidence/readonly/avx2/summary/cases.csv) summaries.

“85%” is primary-capacity occupancy and yields about 60–62% Swiss occupancy.
“Dense” fixes Swiss occupancy at 86.9141%, just below the unchanged ceiling.
It is not an increased maximum occupancy.

| ISA | Working set | Occupancy | Group16 | Batch64 | Tail64 | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AVX512 | hot | 85% | 2.42/10.08/4.59 | 2.51/10.17/4.66 | 2.42/10.09/4.60 | 1.75/6.36/1.14 | 2.44/11.03/9.45 |
| AVX512 | hot | dense | 2.72/12.03/7.94 | 2.81/11.26/6.12 | 2.75/11.58/7.03 | 1.76/6.38/1.17 | 1.76/6.74/1.82 |
| AVX512 | cold | 85% | 25.47/22.46/8.05 | 25.27/23.65/7.86 | 25.35/22.56/8.06 | 26.85/25.35/14.48 | 22.55/25.81/20.71 |
| AVX512 | cold | dense | 26.07/24.91/12.97 | 26.03/26.09/11.14 | 26.02/25.15/12.21 | 27.02/25.17/14.48 | 20.85/19.47/11.92 |
| AVX2 | hot | 85% | 3.85/9.90/5.05 | 3.97/10.17/5.37 | 3.85/9.93/5.04 | 1.72/6.15/1.13 | 2.43/10.73/9.36 |
| AVX2 | hot | dense | 4.13/11.62/7.97 | 4.26/11.12/6.62 | 4.15/11.29/7.27 | 1.72/6.18/1.15 | 1.72/6.55/1.80 |
| AVX2 | cold | 85% | 39.11/32.59/9.48 | 39.30/33.67/9.47 | 39.48/32.35/9.49 | 26.46/24.67/13.51 | 22.16/25.71/20.31 |
| AVX2 | cold | dense | 40.19/34.89/14.51 | 39.99/35.97/13.03 | 39.70/34.92/13.87 | 26.94/25.16/13.74 | 20.71/19.49/11.86 |

On AVX-512, batch64 reduces dense hot misses from 7.94 to 6.12 ns and dense
cold misses from 12.97 to 11.14 ns. Hot hits cost 2.72 → 2.81 ns; mixed cold
reads cost 24.91 → 26.09 ns. At ordinary occupancy the cold all-miss improvement
is small, while mixed cold reads lose 5.3%. Tail64 largely preserves ordinary
reads but gives up much of the dense gain; it still does not establish a better
general default.

The peer controls matter: dense cold misses beat Boost and, on AVX-512,
Abseil; dense hot misses remain far behind both. Ikea's ordinary AVX-512 mixed
cold reads beat both peers, but its cold hit throughput and construction remain
behind Abseil. These are allocation-rounded native containers, not equal-byte
fixtures. Abseil doubles its capacity between these two populations, which
explains much of its dense-case improvement; this study does not attribute that
to probe instructions alone. Allocated bytes and cardinality accompany every
summary cell.

### Writes after separating the read engine

Ns/operation; each cell is **build / existing replacement / 50%-existing upsert /
erase+refill pair**. Churn includes all nine refill rounds and their maintenance
pulses; the first erase prepares the 50%-existing upsert and is excluded from
the pure-churn metric. Writes have one timed batch per seed; tiny hot construction
batches are noisy. Full repetitions and MADs remain available in the retained summaries.

| ISA | Working set | Occupancy | Group16 | Batch64 | Tail64 | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AVX512 | hot | 85% | 10.33/3.47/9.06/17.40 | 9.82/3.44/9.03/17.42 | 9.88/3.44/9.08/17.63 | 6.95/2.30/2.68/6.93 | 6.86/2.86/22.87/14.25 |
| AVX512 | hot | dense | 9.87/3.84/11.83/19.06 | 9.93/3.63/11.64/19.12 | 10.11/3.63/11.68/18.97 | 6.79/2.35/2.68/6.98 | 7.77/1.93/5.50/14.79 |
| AVX512 | cold | 85% | 55.35/25.58/56.57/144.16 | 55.20/25.82/56.27/143.44 | 55.04/25.68/56.40/143.28 | 30.83/35.92/33.53/99.84 | 24.88/27.51/45.13/152.50 |
| AVX512 | cold | dense | 55.80/26.63/69.47/147.63 | 55.05/26.77/70.03/148.72 | 55.57/26.64/69.31/148.49 | 30.24/36.05/33.79/100.48 | 37.00/25.02/37.43/137.66 |
| AVX2 | hot | 85% | 10.72/4.87/10.22/22.51 | 10.76/4.83/10.61/22.49 | 10.87/4.90/10.27/22.40 | 7.20/2.27/2.71/6.90 | 6.69/2.76/22.52/14.00 |
| AVX2 | hot | dense | 10.95/5.11/12.85/23.92 | 10.83/5.11/12.80/23.68 | 10.88/5.10/12.79/23.64 | 6.78/2.30/2.70/7.04 | 7.87/1.90/5.48/14.86 |
| AVX2 | cold | 85% | 69.83/39.20/73.01/210.62 | 69.89/39.61/72.91/211.01 | 68.43/39.02/75.24/210.60 | 30.51/35.18/32.55/99.03 | 24.80/27.16/45.00/151.25 |
| AVX2 | cold | dense | 71.09/40.30/87.97/215.22 | 69.23/41.05/88.68/213.13 | 69.93/40.72/87.78/212.35 | 30.37/35.99/33.82/100.62 | 37.65/24.97/37.46/138.35 |

The final read-only candidate preserves write throughput. It does not repair
pre-existing write gaps against peers: dense cold AVX-512 map8 upsert is about
70 ns against Abseil's 37 ns and Boost's 34 ns. These results concern ordinary
scalar operations, not same-seed growth/accumulation or Loom pipelines.

## Initial screen: 32 versus 64 bytes, sets and larger maps

The first panel changed both lookup and mutation resolution. It is evidence for
the read algorithms across a wider surface, not the final combined writer.
Dense all-miss ns/lookup:

| ISA | Working set | Value bytes | Group16 | Batch32 | Batch64 | Tail64 | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| AVX512 | hot | 0 | 7.47 | 6.32 | 5.62 | 6.62 | 1.12 | 1.62 |
| AVX512 | hot | 8 | 8.04 | 6.75 | 6.08 | 6.98 | 1.16 | 1.82 |
| AVX512 | hot | 64 | 7.91 | 6.89 | 6.35 | 7.24 | 1.24 | 1.71 |
| AVX512 | cold | 0 | 12.27 | 11.32 | 10.58 | 11.46 | 13.80 | 10.76 |
| AVX512 | cold | 8 | 13.05 | 12.19 | 11.19 | 12.41 | 14.53 | 12.06 |
| AVX2 | hot | 0 | 7.69 | 6.69 | 6.42 | 7.03 | 1.11 | 1.56 |
| AVX2 | hot | 8 | 7.96 | 6.91 | 6.60 | 7.27 | 1.15 | 1.80 |
| AVX2 | hot | 64 | 8.58 | 7.53 | 7.12 | 7.84 | 1.22 | 1.78 |
| AVX2 | cold | 0 | 13.20 | 12.52 | 11.99 | 12.68 | 12.53 | 10.59 |
| AVX2 | cold | 8 | 14.41 | 13.46 | 13.06 | 13.89 | 13.66 | 11.90 |

The 64-byte batch gives the largest dense miss gains; 32 bytes has the same
tradeoff at smaller magnitude. The first panel also shows the ordinary hot-hit
cost and mixed cold-read loss retained in the final run. After churn grows the
dense Swiss table, the initial dense hot map8 advantage disappears: group16 /
batch64 is 6.99 / 6.98 ns on AVX-512 and 6.83 / 7.22 ns on AVX2. The recipe is
about current density and query mix, not simply a large table.

### Why the first write results regressed

In the coupled AVX-512 candidate, ordinary cold erase/refill rose from 144.02 to
157.13 ns/pair and dense from 148.15 to 162.56. Inspecting the actual linked
benchmark showed `erase` inlined for group16 and outlined for every batched
variant, for both hot W40 and cold W50 maps. AVX2 already outlined baseline
erase and showed a much smaller difference. The original primary comparisons
covered complete read and upsert loops, not erase or RMW, so they did not prove
those callers were preserved.

Keeping the original writer/resolver and calling a separate batched helper only
from `read_overflow` repairs that seam. The final actual AVX-512 benchmark
inlines erase in all three owners; AVX2 outlines it in all three. The expanded
assembly check compares 28 complete primary read, upsert, erase and RMW caller
functions: 56 candidate/control comparisons per ISA, all identical. Native
churn then returns to 144.16 / 143.44 ns ordinary and 147.63 / 148.72 ns dense
for group16 / batch64. The masking is useful independently of changing writes.

## Cold traffic: tuple checks are fixed, lookahead still has costs

Instrumenting the actual reader gives the same 26,487 full-key checks, exact
slot sequence and answers across 96,000 lookups for every variant. There is no
extra architectural tuple candidate beyond the terminating group. This does
not prove hardware never speculates a tuple load.

A separate untimed pass over actual K30/b20/map8 tables counts distinct 64-byte
control regions requested within each query. For 262,144 all-miss queries:

| Swiss occupancy | Group16 | Batch32 | Batch64 | Tail64 |
| --- | --- | --- | --- | --- |
| about 60% | 56,151 | 69,448 | 138,739 | 60,843 |
| 86.9141% | 113,951 | 134,108 | 212,422 | 172,078 |

These are logical address counts, not PMU misses, transferred bytes or observed
bandwidth. Wider batches inspect metadata that scalar probing might never need.
They also add vector assembly, mask-prefix and lane-to-slot work. Removing the
extra tuple checks therefore removes one cost of wide *logical* groups, not all
costs of metadata lookahead.

The bounded `line32/line64` variants request exactly the baseline control regions
on default 64-aligned backing, and retain the exact tuple trace. They still add
CPU work, while batching fewer useful future groups. AVX-512 dense hot map8
misses improve 7.94 → 7.33 / 7.24 ns, but mixed cold reads lose
24.96 → 26.27 / 26.22 ns. AVX2 shows smaller dense gains and similar losses.
They are not a better default. See the [bounded cohort](evidence/bounded/provenance.json)
and [untimed accounting](evidence/bounded-static/traffic.csv).

## Method, checks and recovery

Three completed sequential panels used the same dedicated AMD EPYC 9R45 (Zen 5)
c8a.2xlarge host, one pinned physical core, no SMT and no concurrent observer.
Clang 21.1.8 used `-march=znver5` for AVX-512 and `-march=x86-64-v3
-mtune=znver5` for AVX2, with the same inlining threshold and function alignment
for controls and candidates. Actual Boost 1.92.0 and pinned Abseil
`2065f4ded0558c6f89fee67c8e5228feb4eb960e` used their native APIs/default hashes
and counted allocations. Capacity was rounded by each container's native policy.

Each panel used three seeds (713/991/1729), three read repetitions, one million
independent queries per timed read batch, ten erase rounds and rotated/reversed
treatment order. Cold reads follow a 256 MiB conditioning pass. Hot does not
mean L1-resident; conditioning does not prove every cold access reached DRAM.
Primary K30/E30, b10/b20 and V0/8/64 cover the native screen; the final combined
run covers map8 hot/cold only. No new all-W, serial-latency, Loom, ARM or Intel
performance claim is made.

- [Ordered batches](evidence/lookahead/provenance.json): 360 callers, 28,080 raw
  rows; full set/map8/map64 hot and set/map8 cold screen.
- [First-line batches](evidence/bounded/provenance.json): 300 callers, 23,100 rows;
  same surface, rejects cache-line-only lookahead as the general answer.
- [Final read-only candidate](evidence/readonly/provenance.json): 120 callers,
  9,240 rows; paired real peers, combined read/write verification on map8.
- [Local static evidence](evidence/static/provenance.json): sanitizer checks,
  exact tuple traces, address accounting and initial LLVM-MCA work. LLVM 21.1.8
  selects `Znver4Model` for `znver5`; isolated metadata-mask throughput is a
  scheduling proxy, not whole-reader or cache performance. The model predates
  the adjacent-pair-load specialization.

ASan/UBSan histories check sets/maps at W16/b0/V0, W20/b4/V8 and W64/b4/V64
against `std::map`, including erase, replacement, RMW, clone and staged reads.
Directed masks cover tombstones, wrap, upper lanes, vacancies, first-empty-group
termination and ignored physical groups. Every native audit checks the complete
query/phase grid, independent answers, cardinality, allocation/Swiss/debt state
in every phase, every churn round and assembly preservation. Report recipes for
all three native cohorts have been regenerated from their verified S3 archives.

The 100 captured hashtable/SeriesPack/TuplePack headers still match the maintained
sources. Production headers are untouched. Captured source and binaries in each
archive reproduce that cohort; the current runner defaults to the final read-only
mode and its stronger assembly checks. Historical coupled panels should use
their captured sources rather than the final runner's preservation requirements.
