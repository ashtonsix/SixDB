# Swiss overflow decision — October 20, 2026

**Graduate eight-bit tags alone. Keep 16-byte groups and the 87.5% ceiling.**
Ikea commit `50937e71` implements that choice across reads, mutations, growth,
clone, ordered accumulation and guarded publication. The tag is computed lazily
after routing to overflow. Zero and one encode empty and tombstone; hash bytes
zero and one map to tags eight and nine. Hashing, primary geometry and seed
semantics are unchanged. Multimap and K-way remain research.

Wider groups improve dense hot misses, but lose cold throughput at equal Swiss
capacity and do not consistently improve writes. They are rejected as general
overflow defaults. No adaptive representation was qualified by this study.

## Width decision

Whole-table independent reads, ns/query; each triple is **hit / 50% miss /
100% miss**. Lower is better. Map8 has eight-byte values. Ordinary means 85%
nominal primary occupancy. Dense starts at 86.9141% Swiss occupancy.

| ISA / map8 case | 16-byte groups | 32-byte groups | 64-byte groups |
| --- | --- | --- | --- |
| AVX-512, dense hot | 2.94 / 13.23 / 8.12 | 2.95 / 12.28 / 7.17 | 3.06 / 12.19 / 7.11 |
| AVX2, dense hot | 4.52 / 11.97 / 8.26 | 4.41 / 11.47 / 7.35 | 4.67 / 12.12 / 7.20 |
| AVX-512, ordinary cold | 27.84 / 24.82 / 8.57 | 28.45 / 25.04 / 9.21 | 28.07 / 26.02 / 10.86 |
| AVX2, ordinary cold | 41.99 / 35.37 / 10.17 | 43.15 / 35.90 / 10.75 | 42.29 / 36.27 / 12.87 |
| AVX-512, dense cold | 28.28 / 27.15 / 13.34 | 28.21 / 27.60 / 14.12 | 28.10 / 29.10 / 16.06 |
| AVX2, dense cold | 42.69 / 36.92 / 15.05 | 44.22 / 37.29 / 15.84 | 43.19 / 38.76 / 18.37 |

For ordinary cold map8 misses, widening costs **7.5% / 26.7%** on AVX-512 and
**5.7% / 26.5%** on AVX2. Dense hot misses save about 11–14% in map8; set misses
save as much as 20%. That trade does not justify a global width change. Plain
direct cold reads are less sensitive, but establish no consistent wider win;
the full grid retains them.

### Why cold throughput can fall

The leading explanation is additional false-tag tuple reads. The implementation
checks matching tags across the entire logical group before an empty ends a
miss. At the ordinary map8 population, Swiss occupancy is about 60% for every
width. With well-distributed independent tags, the expected accidental matches
in one group scale roughly as `width × occupancy / 256`: about 0.04, 0.08 and
0.15 for 16/32/64. These are modeled candidate counts, not measured counters;
the two reserved-code remappings make tag probabilities slightly nonuniform.

An accidental tag match turns into a dependent full-key read from the tuple
allocation. That is cheap with hot tuples and much more costly with cold ones.
Wider groups can save control-group visits in dense tables, while forcing checks
of candidates beyond where a smaller group could have established absence.
This trade fits the hot/cold pattern. It does not establish how much of the
observed loss comes from false matches, probe placement or instruction cost:
the experiment did not collect that causal breakdown. Nor does four times the
SIMD width imply four times the metadata cache-line traffic; aligned 16-byte
and 64-byte groups can both fit within a single line.

After churn, the dense-start map8 cold miss costs are 10.44 / 11.93 / 13.67 ns
for 16/32/64 on AVX-512 and 12.51 / 13.29 / 15.87 ns on AVX2. Much of the dense
hot-miss advantage disappears after maintenance/growth. Wider groups leave
fewer tombstones, but save no capacity in this trace: all widths end ordinary
cold map8 at 208 MiB, and dense-start map8 grows from 208 to 224 MiB.

## Whole-table peer comparisons

Ordinary 85% population, same useful results and value consumption. Triples
remain hit / 50% miss / 100% miss, ns/query. Hot uses direct independent reads;
cold uses prefetch32 for Ikea/Abseil and Boost's native lookup, which has no
public home-prefetch hook.

| ISA / case | Promoted Ikea | Boost 1.92 | Abseil |
| --- | --- | --- | --- |
| AVX-512, hot set | 2.07 / 10.12 / 4.55 | 1.49 / 6.68 / 1.17 | 2.11 / 10.98 / 10.27 |
| AVX-512, hot map8 | 2.61 / 10.90 / 5.18 | 1.91 / 6.70 / 1.20 | 2.73 / 12.54 / 10.98 |
| AVX-512, hot map64 | 3.64 / 11.12 / 4.97 | 3.19 / 7.19 / 1.31 | 3.45 / 12.67 / 10.30 |
| AVX-512, cold set | 10.51 / 14.29 / 8.27 | 22.95 / 24.18 / 15.15 | 17.26 / 23.71 / 21.69 |
| AVX-512, cold map8 | 27.84 / 24.82 / 8.57 | 28.51 / 27.56 / 15.83 | 24.84 / 28.13 / 22.50 |
| AVX2, hot set | 3.45 / 10.81 / 5.16 | 1.47 / 6.28 / 1.10 | 2.13 / 11.10 / 9.91 |
| AVX2, hot map8 | 4.06 / 10.30 / 5.42 | 1.84 / 6.84 / 1.18 | 2.47 / 11.59 / 10.21 |
| AVX2, hot map64 | 5.96 / 11.00 / 5.96 | 3.12 / 6.88 / 1.28 | 3.36 / 12.00 / 10.95 |
| AVX2, cold set | 12.44 / 15.78 / 9.41 | 22.90 / 23.26 / 13.61 | 16.60 / 24.41 / 20.21 |
| AVX2, cold map8 | 41.99 / 35.37 / 10.17 | 29.33 / 27.25 / 14.60 | 24.20 / 27.11 / 21.45 |

Boost's hot reads remain stronger. Ikea's cold set and miss-heavy results are
strong; Abseil's cold map hits remain stronger, especially on AVX2. Wider probes
do not fix those gaps. At this population, initial cold set/map8 storage is
72/208 MiB for Ikea, 272/512 MiB for Boost and 144/272 MiB for Abseil, excluding
fixture/oracle storage. All Ikea widths have equal initial physical capacity.

The dense population crosses Abseil's reservation boundary: it allocates
288/544 MiB and runs at a lower load. Dense cold map8 is correspondingly stronger
for Abseil: 22.31 / 20.49 / 12.94 ns on AVX-512 and 22.83 / 21.54 / 12.95 ns
on AVX2. Common cardinality does not imply equal native load or allocation.

## Writes

Ordinary map8, ns/operation. Each cell is **build / replace existing / 50% new
upsert / erase+refill pair**. Build includes owner allocation/reservation and
insertion of precomputed inputs. The last quantity amortizes all nine pure
erase/refill rounds, including maintenance pulses; it is not a median round.

| ISA / owner | Hot | Cold |
| --- | --- | --- |
| AVX-512, 16 | 11.01 / 3.77 / 10.21 / 19.20 | 59.38 / 27.53 / 59.83 / 155.27 |
| AVX-512, 32 | 10.35 / 3.86 / 10.19 / 19.67 | 58.95 / 27.34 / 62.14 / 155.41 |
| AVX-512, 64 | 10.39 / 3.87 / 10.26 / 19.30 | 59.23 / 27.99 / 60.87 / 154.96 |
| AVX-512, Boost | 7.44 / 2.39 / 2.98 / 7.21 | 33.77 / 39.73 / 36.67 / 109.65 |
| AVX-512, Abseil | 7.47 / 3.12 / 24.92 / 14.71 | 26.12 / 29.86 / 49.23 / 164.40 |
| AVX2, 16 | 11.41 / 4.96 / 10.80 / 22.82 | 75.43 / 41.59 / 78.59 / 224.96 |
| AVX2, 32 | 11.81 / 5.41 / 11.36 / 25.02 | 74.93 / 43.94 / 79.58 / 224.21 |
| AVX2, 64 | 11.13 / 5.17 / 11.05 / 24.00 | 74.47 / 43.87 / 79.75 / 225.20 |
| AVX2, Boost | 7.90 / 2.43 / 3.04 / 7.57 | 32.99 / 38.05 / 35.20 / 107.38 |
| AVX2, Abseil | 7.10 / 2.93 / 24.05 / 15.60 | 26.23 / 29.59 / 46.30 / 163.11 |

There is no consistent wider-group write gain. AVX2 hot churn worsens 9.6% with
32-byte groups and 5.2% with 64. Peer build/new-key and Boost churn gaps remain;
the ordinary-load Abseil upsert loss includes its own maintenance at that load.
Dense-start and set/map64 writes, mixed replacements, per-seed spread and
maintenance costs are retained in the complete CSVs. Small hot write batches
have one sample per seed; isolated 5–10% cells are less persuasive than the
repeated cold-read pattern.

## Tag promotion and preservation

Ordinary cold all-miss set/map8 costs fall from 8.93/9.21 to 8.27/8.57 ns on
AVX-512 and 10.54/11.13 to 9.41/10.17 ns on AVX2 after tag promotion. Ordinary
post-churn cold map8 misses fall from 12.45 to 11.24 ns and 14.01 to 12.70 ns.
Writes remain broadly near the seven-bit baseline; individual losses remain
visible in the retained grid. This is a targeted overflow improvement, not a
new claim that Ikea wins every whole-table workload.

Code comparisons on both ISAs show all six primary read loops unchanged by tag
promotion. Twelve primary writer loops lose two or three instructions because
the unused eagerly computed seven-bit tag is removed; remaining differences
include register allocation and smaller/shifted stack frames. For width changes,
all 18 primary read/write loops match group16 per variant per ISA: 72 identical
comparisons. Linked disassembly confirms actual wider comparisons and masks,
including two YMM comparisons for 64 bytes on AVX2. Timing can still vary with
code placement and allocation even when primary instructions match.

ASan/UBSan passed all 23 maintained hashtable checks/examples, six multimap
frontier checks and the ordered K-way history check. New directed tests exercise
all 256 raw tag bytes through overflow, deletion/reuse, clone, overflow reserve,
growth and ordered accumulation. Research width checks also force lanes 31/63,
full probe cycles, staged reads and RMW. Production and research compatibility
checks cover key widths beyond the native timing panel.

## Method and recovery

Worker `20261020T173042Z-7f191034`: AMD EPYC 9R45 Zen 5, c8a.2xlarge, eight
physical cores/no SMT, 16 GiB; pinned Clang 21.1.8. AVX-512 and AVX2 are separate
builds. All 576 callers ran sequentially on CPU 0, three seeds, rotated/reversed
treatment order, three read repetitions. Audits passed all expected phases,
cardinalities, results, capacities and code comparisons: **44,928 raw sample
rows and 4,672 summary cells**. All 100 captured production table/pack headers
still match the promoted checkout; archived experiment sources match the capture.

Treatments are reconstructed seven-bit/16, promoted eight-bit/16, eight-bit/32,
eight-bit/64, actual Boost 1.92.0 and Abseil
`2065f4ded0558c6f89fee67c8e5228feb4eb960e`. Controls use native APIs/default hashers
and stateless counting allocators delegating to `std::allocator`. Wider groups
retain the 87.5% ceiling and match total Swiss slots; minimum allocations differ
for the tiny spill populations at 50% primary load, explicitly checked by audit.

Native geometry is K30/E30, b10 for sets/map8/map64 and b20 for sets/map8. Hot
owners are warmed L2/LLC-sized footprints, not asserted L1-resident. Cold batches
scan a 256 MiB conditioning buffer; this does not prove every load reaches
DRAM. Key/value inputs and exact verification are outside timing. No Loom
throughput or consumer-pipeline interleaving is measured here. NEON was checked
under sanitizers, not timed natively in this comparison.

Complete summaries: [AVX-512 reads/writes](evidence/zen5/avx512/summary/cases.csv),
[AVX2 reads/writes](evidence/zen5/avx2/summary/cases.csv),
[AVX-512 amortized churn](evidence/zen5/avx512/summary/churn.csv),
[AVX2 amortized churn](evidence/zen5/avx2/summary/churn.csv). They retain all owners,
all hit/miss mixes, direct/prefetch variants, routed/primary subsets, populations,
capacities, MADs and negative cells. The [recovery instructions](README.md#evidence-and-recovery)
restore raw samples, binaries and source. The S3 report recipe was tested and
reproduced both audits and summaries. Rejected code is archived, not maintained
as another implementation.
