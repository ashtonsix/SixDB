# Mutable accumulation: results before promotion

This records the final research qualification before promotion of mutable
accumulation into [Ikea](../../../ikea/docs/hashtable/usage.md#mutable-accumulation).
The refactor replaced width-specific merge loops with one image engine and one
primary/Swiss resolver; the last improvement covered missing cache lines in large
96-byte buckets. The numbers below belong to those captured research versions.
The maintained core's newer qualification is in the
[accumulation scorecard](../../benchmarks/hashtable/ACCUMULATION.md).
K-way and owning multimap remain research; this report does not establish a
current K-way performance claim.

## Captured performance

Merge time excludes sizing, allocation, copying the first source into the
destination and overflow reservation.
Each fixture supplies exact union/spill counts outside timing, to both controls.
All comparisons use actual Abseil, not a modeled baseline. Counts below describe
these panels, not workload-weighted scores.

The refactor's paired after/before merge-time ratios (lower is faster):

| CPU | Panel | Cells | Median ratio | Wins over Abseil |
| --- | --- | ---: | ---: | ---: |
| Zen 5 | [K14/K30 sets/map8/map64](evidence/refactor-qualified-zen5.csv) | 54 | 0.9883 | 53/54 |
| Granite Rapids | [K14/K30 sets/map8/map64](evidence/refactor-qualified-granite.csv) | 54 | 0.9955 | 54/54 |
| Zen 5 | [K31/K47/K55 map8](evidence/refactor-qualified-wide-zen5.csv) | 24 | 1.0025 | 18/24 |
| Granite Rapids | [K31/K47/K55 map8](evidence/refactor-qualified-wide-granite.csv) | 24 | 1.0030 | 20/24 |
| Neoverse V2 | [K14/K30 sets/map8/map64](evidence/refactor-neoverse.csv) | 54 | 1.0007 | 30/54 |

The final prefetch change leaves the aligned B64 set/map8/map64 request plans
unchanged. Representative K30, eight-input map8 results from those panels,
ns per incoming entry; each cell is **0 / 50 / 100% overlap**:

| CPU | Source b | Candidate | Abseil |
| --- | ---: | --- | --- |
| Zen 5 | 10 | 5.42 / 10.42 / 5.08 | 6.48 / 10.79 / 6.42 |
| Zen 5 | 15 | 7.84 / 10.44 / 4.93 | 15.41 / 22.37 / 8.09 |
| Granite Rapids | 10 | 6.49 / 11.95 / 7.34 | 8.43 / 13.60 / 10.09 |
| Granite Rapids | 15 | 9.35 / 13.07 / 7.97 | 18.64 / 24.91 / 18.35 |

Correct destination sizing was necessary but insufficient: the first correctly
sized large disjoint Zen map8 kernel took 21.38 ns against Abseil's 14.22.
The shared engine decodes a source bucket once, retains logical destination
images for a small split fanout, and packs each dirty image once. Same-seed
bucket/quotient arithmetic skips full-key recovery and hashing on the primary
path. Swiss routes recover keys lazily. Overlap checks and reductions still run.
The refactor kept encoding, capacity rules and K-to-K-prime profiles unchanged.

## Final prefetch improvement

A 96-byte bucket can start halfway through a cache line. The old prefix plan
could omit the flags line or the last map8 value line. One geometry helper now
covers the complete key/map8 extent, including every admitted bucket alignment.
It is selected at compile time when destination primary backing is at least
4 MiB; smaller owners retain the original request plan. There is no runtime
threshold branch or entry dispatch. The cutoff separates the tested hot and
large geometries; it is not an exhaustively tuned crossover.

[Zen 5](evidence/prefetch-policy-zen5.csv) and
[Granite Rapids](evidence/prefetch-policy-granite.csv) each cover 48 wide-key
set/map8 cells, with paired shared-engine-before and actual Abseil controls.
Panel median time ratios are 0.9915 and 0.9991; Abseil wins are 39/48 and 42/48.
For large K47/b17, selected examples are:

| CPU | Case | Before | Selected | Abseil |
| --- | --- | ---: | ---: | ---: |
| Zen 5 | Set, 8 inputs, disjoint | 9.03 | 8.35 | 15.33 |
| Zen 5 | Map8, 8 inputs, mixed | 13.70 | 13.25 | 30.29 |
| Granite Rapids | Set, 2 inputs, disjoint | 7.62 | 6.93 | 13.19 |
| Granite Rapids | Map8, 8 inputs, mixed | 19.68 | 18.28 | 28.90 |

All 12 large cells improve in each CPU's selected panel. Hot-cell median ratios
are 1.0044/1.0033. Their 18 primary kernels per CPU have identical normalized
instruction streams ([audit](evidence/prefetch-policy-code.json)); individual
hot timings still vary, with a largest Zen set timing difference of 14.1%
(2.68 vs 2.35 ns, MAD 0.40/0.34). Instruction preservation does not establish
identical physical placement or explain every timing difference.

Unconditional full coverage was rejected: it spends an extra prefetch instruction
on small owners. The complete negative panels remain for
[Zen](evidence/prefetch-span-zen5.csv) and
[Granite](evidence/prefetch-span-granite.csv).
The [prefetchw assembly](evidence/prefetch-issue.s) and llvm-mca models for
[Zen](evidence/prefetch-issue-zen5.txt) and
[Granite](evidence/prefetch-issue-granite.txt) predict one extra issue micro-op,
about one third cycle per bucket. These are scheduling costs, not cache-latency
or whole-merge predictions.

## Deferred gaps and limits

- **Wide hot maps:** small K47/eight-input disjoint map8 still trails Abseil.
  The full captured cells and MADs are in the prefetch scorecards. The large-table
  prefetch improvement does not repair hot comparison/packing costs. A smaller
  wide-key layout is an untested idea, not a promised gain.
- **Zen mixed small sets:** K14/eight-input/50% overlap takes 10.15 versus
  9.37 ns. Compact logical matching lanes could reduce comparison work; that
  experiment is deferred.
- **ARM:** small mixed and hit-heavy merges remain weaker. The independent
  [word-add panel](evidence/wordadd-neoverse.csv) also loses on small mixed
  map8 (14.04 versus 11.70 ns), so the byte reducer alone does not explain it.
- **Reducer/surface coverage:** captured x86 performance qualification uses a
  non-associative byte fold. Ordinary copy/add reducers on x86, wide map64 and
  partial-value throughput remain unqualified. Wide sets/map8 are now measured.
  Correctness checks cover partial values and all key widths; that does not
  establish throughput for those surfaces.
- **Composition:** the promoted leaf requires an exclusive disposable destination
  and caller-owned sizing, copying and publication. It acquires no key grants or
  MVCC damage admission; shared published mutation needs enclosing ownership.

The earlier refactor's largest paired timing outlier is Zen K55/two-input/mixed
map8: +8.8%, with identical primary and Swiss instruction streams. Unpaired
medians are 9.64/8.12 ns, MADs 1.38/0.96. Its cause remains unisolated; the
refactor is not claimed to improve every measured cell.

## Verification and retention

Pinned Linux Clang 21.1.8 and Abseil 20260817.0
(revision 2065f4ded0558c6f89fee67c8e5228feb4eb960e), matching native ISA/tuning,
release flags and function alignment. These x86 panels use three seeds and
four repetitions in a balanced Latin square. Each control prewalks its own
incoming storage before preparation, outside all timings. ARM's earlier panels
use three rotations; they are not equivalent control-order evidence.

Every timing checks full union membership/cardinality/payloads, fixed backing
and source preservation. Native and ASan/UBSan histories cover strict source
order, refusals, exceptions, W1/W64, bitmap transitions, split/fold, holes/tombs
and partial values. Independent physical byte oracles cover B64 E10..30/T2..5,
B96 E31..64/T1..2, and point writes E1..64. The prefetch oracle independently
enumerates physical cache lines for key-only and key/map8 plans at alignments
1..64, with V0/3/8/9/64 owners.

The refactor preserves 83 comparable primary/scalar bodies and 68 outlined
Swiss stages/callees ([x86](evidence/refactor-x86-code.json),
[ARM](evidence/refactor-neoverse-code.json)). Addresses, RIP-relative constant
addresses and NOPs are normalized in instruction comparisons. E32 comparison
and source expansion are shorter. Final cleanup preserved kernel tokens and
dropped an unused benchmark parameter name ([receipt](evidence/cleanup-source.json)).
Separate header repairs made the maintained and then-frozen benchmark headers
self-contained. The [39 standalone/include-order checks](evidence/frozen-header-checks.json)
and unchanged probe assembly describe that earlier reference. The frozen copy
has since been [archived](../../benchmarks/hashtable/archive.json); recurring
comparisons now build and check the maintained implementation.

Immediate/deferred map64 payload policy evidence remains for
[Zen](evidence/size-zen5.csv) and [Granite](evidence/size-granite.csv).
Deferral is selected only for Zen destinations at least 128 MiB.
Superseded implementations, runners, chronological reports and construction
summaries are SHA-verified in Linux `~/sixdb-archive/hashtable-merge-20261017/`
([receipts](archive.json)). Retained evidence keeps complete relevant panels,
including negative results. The [job index](evidence/construction-jobs.json)
recovers full sources, binaries, disassembly and raw timings from S3.
