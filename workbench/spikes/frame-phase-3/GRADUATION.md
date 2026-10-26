# Initial Frame landing

The read/build core is in `ikea::frame`: fitting, construction/emission and
checked immutable operations over non-null u64 columns, composing pack, bound
and intervals (sizes or relative ends). The [usage guide](../../../ikea/docs/frame/usage.md)
and [source/contract map](../../../ikea/docs/frame/reference.md) are the consumer
entry points. [PROPOSAL.md](PROPOSAL.md) records the accepted boundary.

**The initial read/build core is qualified and ready for consumers** on Zen 5
AVX2/AVX-512 and Graviton 4 NEON. The carried-region Intervals follow-up
85f22f57 is qualified below; the current scorecard uses that source.
Private preparation, point mutation, mutable rebuild and their scratch remain
research. This landing establishes a foundation for Engine research; it does not
make Frame the default representation for every column or operation.

## What is qualified

Initial full qualification (26afac10) passed all 16 maintained checks on all
three native profiles: Frame description,
manifest, guarded images, readers and stage oracles; ordinary and actual Orbital
owner examples; shared selection, Intervals search/seams/wide; SeriesPack full
width-matrix, descriptor, ownership, admission and filters. Native-control
conformance passes 692,915 checks, 16 refusals and five protected cases per
profile. Each profile's 600 timed records also checks consumed results against
original values, including every materialized point output.

The owner example retains old readers through external replacement and both
sizes/relative-ends same-version conversions at G16/G64. Interpretation, placement,
proof and payload ownership remain distinct. Frame has no writer or dependency
on Orbital/Loom; the owner example links Orbital separately.

The extracted Frame sources and six fixtures also pass local ASan/UBSan:
description (559 checks), manifest (284), group images (1,953 valid, 991 guarded
prefixes, 26 malformed), readers (5,951), stage oracles and wide Intervals.
The [latest sanitizer receipt](landing/evidence/carried-sanitize/receipt.json) records flags and
source/header/archive/binary hashes. Its linked SeriesPack/core/Intervals archives
are uninstrumented; this is not a claim of fully instrumented dependency coverage.

## Carried-region traversal follow-up (85f22f57)

The visitor now carries its checkpoint-region index instead of recomputing
rank/spacing inside every region. For full blocks, the generated AArch64 decode
loses both visitor-loop division sites; two checkpoint-setup division sites
remain. At G16 this removes about 4,096 executed divisions per 65,536-row unit.
The format, recipe, emitted size and API remain unchanged.

The original EventTime selected-SUM gap closes on repeated queries across all
three profiles. The table gives **Ikea/spike time ratios**, so lower is better.
Each ratio uses its contemporaneous spike control; before/after ran on different
workers, not a paired same-host experiment.

| Profile | Selected SUM repeated: before → after | Selected SUM sweep: before → after | COUNT repeated: before → after |
| --- | ---: | ---: | ---: |
| Zen 5 AVX-512 | 1.47 → 1.01 | 1.29 → 1.12 | 0.74 → 0.43 |
| Zen 5 AVX2 | 1.39 → 0.96 | 1.26 → 0.98 | 0.76 → 0.48 |
| Graviton 4 NEON | 1.22 → 1.01 | 1.26 → 1.08 | 1.17 → 0.97 |

Eviction-conditioned selected SUM still takes 12% longer on AVX-512 and 8%
longer on NEON; AVX2 is slightly faster than the spike. Point and dense native
scan limitations remain. The complete updated surface, including losing cells,
is in the [current scorecard](landing/SCORECARD.md).

Both Zen profiles pass the full 16 checks. Graviton passes the eight affected
Frame/owner/wide checks; unchanged leaf/search/selection sources retain their
original full qualification. All profiles pass 692,915 native-control checks
and all 600 unique measured-result oracles. Locally, reader/stage/wide checks,
rebuilt owner examples and all six Frame/wide sanitizer fixtures pass.

| Profile | Follow-up job | Receipt |
| --- | --- | --- |
| Zen 5 AVX-512 and AVX2 | 20261026T032419Z-ae3ebd4a | [Archive/provenance](landing/evidence/carried-zen5/artifact.json), [AVX-512 checks](landing/evidence/carried-zen5/avx512/checks.txt), [AVX2 checks](landing/evidence/carried-zen5/avx2/checks.txt) |
| Graviton 4 NEON | 20261026T033647Z-410eed8d | [Archive/provenance](landing/evidence/carried-graviton4/artifact.json), [affected checks](landing/evidence/carried-graviton4/neon/checks.txt) |

The only compiled Ikea/Orbital change from the initial qualification is
`ikea/include/ikea/intervals/wide.h`. Committed-source capture digest:
`b594265202c72de4215ed7a23f72e371f979fc6ddc79155196efd7e9111fe56c`.
The Graviton retry differs only in the worker's check selection, with digest
`4cb6e0c355b794b4f04af19785e714aee14fa239409c0b9e25841c1616efe699`.
Both workers' complete source manifests match their captures.

An initial 8 GiB Graviton full-matrix build (20261026T032419Z-03e91421) ended
with a killed compiler before tests ran. The retry selects the eight affected
checks with `SIXDB_FRAME_CHECKS=intervals`, plus the unchanged complete regression
panel and native controls. It does not recompile unchanged SeriesPack test TUs.
All workers are small, terminate after collection and use the shared input cache.

## Initial control results and decisions (26afac10)

The [complete scorecard](landing/SCORECARD.md) shows every column, operation,
conditioning mode and profile. [The panel contract](landing/README.md) gives the
trace and comparison boundaries. The original source-26afac10 results below
are retained for comparison. The following are repeated-query medians;
the first four rows give speedups, where values over one favor Ikea. The final
row gives an Ikea/spike time ratio, where values over one are a cost increase.

| Cell | Comparison | Zen 5 AVX-512 | Zen 5 AVX2 | Graviton 4 |
| --- | --- | ---: | ---: | ---: |
| Generated timestamps, sizes16 SUM | Spike / Ikea | 3.53× | 1.46× | 3.16× |
| URLHash exact SUM | Spike / Ikea | 3.78× | 0.88× | 1.36× |
| URLHash period-3 selected SUM | Spike / Ikea | 2.89× | 1.84× | 1.70× |
| UserID COUNT >= median | Typed native / Ikea | 5.52× | 8.33× | 4.89× |
| EventTime relative16 selected SUM | Ikea / spike (cost increase) | 1.47× | 1.39× | 1.22× |

These are specific operation/recipe cells. The native controls still win most of
this dense, one-unit panel. In particular, independent points take 6.5–74× as long as the
native materializing gather in these traces, and wide/full scans often lose.
Ikea executes repeated checked `get` calls; native executes one pre-admitted
batch gather. That is the current consumer cost, not a kernel-latency ratio.
AVX2 URLHash SUM also took 14% longer than the spike in this initial cohort.
The EventTime selected-SUM gap in that cohort is addressed by the traversal
follow-up above. Ikea uses checked calls over placed images; the spike uses
trusted fitted-builder storage. Those comparison boundaries remain. Even a
metadata-only fast COUNT has visible checked-call overhead (Zen 5 UserID:
378 ns versus the spike's 245 ns for the whole 65,536-row query).

Keep the exact SUM/weighted-SUM kernel gains and descriptor skipping. Use a real
native alternative for point-heavy or dense scan-heavy columns. The next useful
read investigations are batched/staged point consumption, selection normalization
and cross-group traversal; they do not block this foundation. Engine's recipe
choice and regret study follows landing, as Ashton directed. This panel does not
re-measure the earlier sparse correlated A->B seam, Polars/DuckDB match-ups or
saturated memory bandwidth.

Emitted Ikea bytes versus typed native value bytes are below (same on all three
profiles). This excludes execution-state heap, source/oracle buffers
and allocation overhead: it is not a complete retained-memory comparison.

| Column / recipe | Ikea emitted bytes | Native value bytes |
| --- | ---: | ---: |
| OS, bound→pack | 74,721 | 65,536 |
| UserID, bound→pack | 429,501 | 524,288 |
| URLHash, bound→pack | 528,864 | 524,288 |
| EventTime, relative16→bound→pack | 162,741 | 262,144 |
| Generated timestamps, sizes16→bound→pack | 100,094 | 262,144 |

Repeated queries average 32 executions; eviction-conditioned queries run once
after a 256 MiB sweep. Neither label proves a cache tier or DRAM service. The
scorecard reports nanoseconds per whole query and request/row counts, not
silently interchangeable ns/value and ns/point units.

## Initial source identity and receipts

Core source: 26afac10, including the exact-u128 SeriesPack dependency (2731574e),
shared selection/geometry helpers (22a56d7c), stages (42da0168), checked public
surface/examples (d855e44a), compact pack state (6c23d0f4), region-wise Intervals
traversal (52fd38a8) and in-place offset-binary reconstruction (9e1788ee).
The first extraction's extra per-row division and separate conversion pass were
removed before this native run; reconstruction remains owned by Intervals.

Base capture digest:
`2eb40a40dc2bdca47ebca094211e14865652aa9eb9a98e18a4d623ea1ec89c3e`.
Both initial worker manifests match all 4,017 committed source files. The AVX2
retry capture differs only in `landing/worker.sh`, adding profile selection:
`2c2d0566947043ff63e5a0e06f1d1e7af73e88487a3bcfdcf6a7bb74e9ae49f7`.
Frame, SeriesPack, controls, benchmark code, flags and trace are unchanged.

| Profile | Job | Receipt |
| --- | --- | --- |
| Zen 5 AVX-512 | 20261026T011106Z-c097ed7a | [Checks](landing/evidence/zen5-avx512/avx512/checks.txt), [archive/provenance](landing/evidence/zen5-avx512/artifact.json) |
| Graviton 4 NEON | 20261026T011106Z-b0dc1fa4 | [Checks](landing/evidence/graviton4/neon/checks.txt), [archive/provenance](landing/evidence/graviton4/artifact.json) |
| Zen 5 AVX2 | 20261026T013904Z-fd0c1c85 | [Checks](landing/evidence/zen5-avx2/avx2/checks.txt), [archive/provenance](landing/evidence/zen5-avx2/artifact.json) |

The first Zen 5 Spot worker was interrupted during its AVX2 build, after the
AVX-512 profile had fully completed. Its job receipt remains **interrupted**;
only the completed profile is qualified. The AVX2-only Spot retry
20261026T013027Z-9c451a43 was also interrupted during compilation. The last retry
uses one small c8a.xlarge on-demand worker (8 GiB), with immediate shutdown after
collection. All other work used small Spot workers; no large saturation node.
Inputs use the shared artifact cache and are not duplicated in collected results.
Full compiler output and source bundles stay in the recoverable worker archives.

## Research boundary

Checked private preparation and Frame mutation/effects protocols stay research;
the 21–26 µs private preparation experiment is not imported or endorsed as a
write path. Future changes still need explicit access/resource admission,
logical contributions, issued byte ranges, evidence invalidation and publication.

RLE, physical leading-byte planes, TuplePack/shared records, presence/rank,
logical split and additional value domains retain described/refused shapes where
implemented in the canonical description. They are not executable initial images.
Costs and recipe policy remain steerable; no optimizer or universal cost model
lands. The old `proto/` read driver and writer remain frozen research/control
material, while consumers use Ikea's implementation.
