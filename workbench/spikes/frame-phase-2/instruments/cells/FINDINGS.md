# Compression pays differently under shared capacity pressure

24 October 2026. This first complete-cell panel establishes useful capacity
reversals on the shared B/C image, alongside real small-query losses to strong
native controls. It does not establish Calico parity or system-engine wins.

Each query consumes 2,048 stored-domain rows: eight evenly spaced original
256-row blocks from the same ClickBench source. A pool contains 1, 16 or 512
identical logical replicas, privately built on each pinned worker. Queries,
thresholds, answers and per-cell layouts remain fixed across pool sizes. This
many-small-queries forcing workload is distinct from a full-column scan and
from arbitrary independent data. All results below include the complete query
call, exact u128 consumption and per-call answer check.

Two reused small Spot guests supplied eight physical cores each: c8a.2xlarge
(Zen 5, AVX2 and AVX-512 builds) and c8g.2xlarge (Graviton 4, NEON). Each panel
uses one or eight workers, three pools, three queries, repeated and flushed
conditions, and a six-treatment Williams schedule with exact duplicates.
Both guests are terminated. Scripts took 47 and 37 seconds; no full-host
instance was used for this panel.

## The reversals

Warm-repeat reciprocal throughput, ns per complete query. Eight-worker values
are cohort wall time divided by total completed queries: aggregate reciprocal
throughput, **not per-query latency**. These selected rows use segment Frame
SUM and per-block fused Frame pair; the indexed/staged alternatives and every
losing/control cell remain in the evidence.

| ISA | Query | Workers | Pool/worker | Frame | Native FOR | Observation |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| AVX2 | UserID SUM | 1 | 1 | 102.5 | 120.4 | Frame ahead at this small-query endpoint |
| AVX-512 | UserID SUM | 1 | 1 | 82.6 | 67.1 | Native ahead with wider compute |
| AVX-512 | OS SUM | 1 | 1 | 49.7 | 23.7 | Native u8 remains about 2.1× faster |
| AVX2 | UserID SUM | 8 | 512 | 19.9 | 188.8 | Large shared-footprint advantage |
| AVX-512 | UserID SUM | 8 | 512 | 19.0 | 111.2 | Advantage persists against the faster native build |
| AVX-512 | A=UserID→SUM(URLHash) | 1 | 1 | 399.2 | 166.0 | Frame loses about 2.4× in this repeated small cell |
| AVX-512 | A=UserID→SUM(URLHash) | 8 | 512 | 184.2 | 579.6 | Concurrent larger pool reverses that loss |
| NEON | A=UserID→SUM(URLHash) | 1 | 1 | 996.3 | 671.9 | Native ahead at the small endpoint |
| NEON | A=UserID→SUM(URLHash) | 8 | 512 | 213.5 | 240.9 | Modest larger-pool advantage, not Zen's large reversal |

The UserID SUM arm's per-worker logical input ranges at pool 512 are 1,910,272
Frame bytes versus 8,388,608 native bytes, including Frame descriptors and its
readable suffix. Across eight workers that is 14.6 versus 64 MiB. The Zen guest
reports a shared 32-MiB LLC group for those eight cores. This is consistent with
a useful compressed-capacity regime. Neither sizes nor these timings identify
the servicing cache level or measure memory-bus traffic. All representations
coexist in process memory, and vector capacities/object/preparation storage are
accounted separately.

The pair query's fine selection derives from A, not a free supplied row mask.
Its threshold is the sampled A median; every arm selects the same 1,024 rows.
All blocks are admitted by the coarse mask in this first native panel. Therefore
these rows do **not** qualify scattered coarse selection, external validity,
Loom point staging or arbitrary predicate selectivities.

Flushed-first-pass pool-512 observations show the same useful Zen throughput
region: eight-worker AVX-512 UserID SUM is 96.7 versus 307.9 ns/query, and the
pair is 276.9 versus 619.3. These are completed-batch rates over distinct actual
replicas. One-worker flushed pair is 1,554 versus 1,522 ns/query: no meaningful
lead. Pool-1 flushed batches are too short to settle close ranks and retain
large duplicate spread; they are not evidence of cold saturation.

## What this changes

The strong AVX-512 native pair is a necessary control. The same Frame image can
lose materially in a small repeated cell and win under shared capacity pressure.
Substrate probes should link the qualified native controls rather than reuse a
weaker scalar or needlessly wide accumulator. Indexed/staged Frame variants
remain workload choices: whole-image indexing is not universally better as
pool pressure increases. The useful research question is which complete
consumer/footprint selects each program, not a universal compressed hot-loop win.

Calico remains an explicit missing executable comparison. Ikea HELPER is
qualifying unchanged pinned Calico Type-bound and raw-array entry points, with
unsigned and signed exact aggregate laws kept separate. Historical Calico
numbers and source surveys do not establish current parity. Native Polars,
DuckDB and codec controls likewise need complete comparable timed cells beyond
their existing functional qualification.

## Verification and recovery

Each native profile passes 6,012 aggregate cases, 204,800 point values and five
domain refusals in optimized and ASan/UBSan builds. The checks compile the C++
caller and header leaves; assembly instrumentation limits remain. Linked native
pair code has real SIMD comparisons in all 16 lane specializations on each ISA.

Across 108 cells, all 3,888 cohort samples pass independent Python row oracles,
per-call mismatch flags, exact consumed totals, schedule/predecessor balance,
actual allocation/nonalias and interval audits. The 3,745 frozen source files
match both recovered worker archives. Executables match their before/after
hashes and recovered artifacts. No moving shared-checkout source is substituted.

Minimum common-overlap fractions are 97.95%, 98.09% and 98.10% for AVX2,
AVX-512 and NEON; maximum deadline overshoot is 2.20%, 2.19% and 1.89% of the
10-ms window. Warm primary/duplicate median ratios span 0.956–1.029,
0.978–1.054 and 0.962–1.033. Large reversals survive that variation; small
rank differences do not. These are guest-level repeated throughput intervals,
not a fully saturated socket claim.

[Evidence](evidence/native-cells) retains every arm/cell and repetition in compact
CSV, with source/host/input/functional audits and full worker recovery references.
Raw actual ranges, per-worker timestamps, maps, exact images/inputs, compile
commands, linked assembly and binaries remain recoverable from those archives.
`curate.py` independently reconstructs the compact evidence from the two worker
collections, original byte-identical corpus and archived source manifests
(the original capture receipt can additionally be checked). The local
[qualification](qualification.json) is separately scoped to protocol/functional
checks; nonquiet VM timings supply no performance ranking.
