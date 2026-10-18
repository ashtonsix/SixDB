# ARM Local equality and first-match search

The AArch64 shifted-AND reduction is useful with runtime query preparation
included. Its clearest benefit is reciprocal throughput; latency gains are
smaller, and narrow four-/five-bit tiles can lose a fraction of a nanosecond.
This study measures Neoverse V2 (Graviton4 c8g), not V3 or Apple timing.

## Implementation

Ikea now has `seriespack::indexof` and `indexof_unchecked`, and the corresponding
prepared/composition operation. The result is the first active match relative
to the requested window, or count when absent. The window is at most 64 values.
The [usage guide](../../../ikea/docs/filters.md) owns the public contract.

On AArch64, four- through seven-plane ordinary Local equality uses an exactly
bounded word load and the prepared shifted-AND body. Ordered predicates and
the existing x86 filter choices are unchanged. Narrower plane widths keep their
previous ordinary lowering. Full filters retain full-mask evaluation.

For complete eight-row windows of ordinary headless Local expressions with a
four- through seven-bit residual, ARM `indexof` compares the body first and
checks only candidate groups' residuals. It prepares the residual query once
for the entire window. Arbitrary edges, authored replacements, heads, other
layouts and other ISAs retain the existing expression filter plus first-bit
reduction. No new stored flag, public mode or CPS boundary is introduced.

`composition::local_equal8<K>` is the reusable arithmetic body. It accepts an
in-domain query, then an already-loaded word containing Local planes in its low
K bytes, and returns original-position candidate bits. It never loads storage.
Iceberg–Swiss can reuse it with its own eight-byte readability proof; Ikea's
ordinary leaves never inherit that permission.

## What is measured

The first V2 capture is job `20261015T232934Z-b5b62df5`. The final source capture
is group `20261015T233813Z-9f725ff5`; confirmation job
`20261015T234847Z-1812a43f` additionally measures the actual full-filter entry.
[Qualification](evidence/arm-index/qualification.json) records all five successful
native profiles, 72 verified production/test source hashes and sample identities.
The three ARM captures contain 114,400, 124,800 and 52,000 timing observations.

The index driver tests 19 K/N pairs:4/8,5/8,6/8,7/8,10/40,12/32,13/32,14/32,15/32,
16/24,20/24,29/16,30/16,31/16,38/16,40/16,54/8,62/8,64/8. Each compares first,
last, uniform, absent, mixed and duplicate matches, clustered and random bodies,
shared-prefix cases, full/sparse activity, and both arm orders. Each timing has
80000 iterations and five repetitions per order. Sources start at byte offset1.
All implementations are checked against independent decoded-value truth.

Runtime query loading, expansion, body comparisons, candidate intersection,
rank extraction and trace recurrence are inside the measurement. A one-state
rank-to-next-address recurrence measures hit dependency latency; eight independent
states measure reciprocal throughput, in ns/query. Body-first all-miss exits
can bypass data dependence through predicted branches, so those serial numbers
are labelled **one-stream cost**, not dependent latency. Sparse and mixed cases
can contain these exits too. The finite 64-window trace occupies a small hot
working set; it does not establish DRAM behavior or unlimited random branch
prediction behavior. No cross-platform timing is inferred.

Controls are the previous complete filter plus first-bit extraction, early 8/16,
an independently implemented fused byte-column/plane consumer, old scalar or
plane residual verification, and new bounded/owner-wide reductions with either
body-first or full-window evaluation. The final panel additionally measures the
actual public `indexof` implementation. The frozen previous control preserves
its scalar-plane algorithm as the production helper changes. It is not a
byte-identical copy of the former public wrapper; code generation can differ.
The first capture retains the actual former wrapper for that reason. Do not
attribute changes between those two baseline captures to the new kernel.
The independent
fused control is a concrete alternative, not a proof of a hardware lower bound;
its wide odd-width byte-column approach can lose to the existing body-wide SIMD
comparison even before this change.

## First native signal

Values are dependent hit latency / reciprocal throughput in ns/query, full
activity and uniformly placed random hits. These are the pre-graduation candidates,
not timings attributed to the final public API.

| K / N | Previous filter + first bit | Independent fused | New bounded full mask | New bounded body-first |
| --- | ---: | ---: | ---: | ---: |
| 4 /8 | 7.40 /2.36 | 6.86 /2.24 | 7.89 /1.29 | 7.53 /1.31 |
| 6 /8 | 8.01 /3.02 | 7.89 /2.89 | 8.24 /1.55 | 7.89 /1.59 |
| 14 /32 | 16.34 /8.00 | 14.62 /7.24 | 13.63 /5.47 | 11.72 /2.90 |
| 30 /16 | 14.47 /5.12 | 17.05 /6.48 | 14.51 /4.02 | 13.34 /3.14 |
| 38 /16 | 13.93 /5.08 | 19.20 /8.14 | 13.33 /3.79 | 12.55 /3.10 |

Shared-prefix late K30 hits cost 14.18/4.23 for the new bounded body-first path
against 14.50/4.02 for its full-mask counterpart. This retains a real throughput
loss from the branch/early-search choice. Owner-wide reads are not universally
better: full K30 improves to 14.03/3.98, while body-first uniform hits are
13.42/3.07 versus bounded 13.34/3.14.

## Graduated consumer confirmation

The [complete table](evidence/arm-index/confirmation/table.md) and
[all-case medians](evidence/arm-index/confirmation/summary.csv) keep latency and
reciprocal throughput separate, including misses and shared-prefix losses.
Representative random-hit rows below are **dependent latency / reciprocal
throughput, ns/query**, with runtime preparation included and full activity:

| K / N | Previous algorithm | Independent fused | Ikea indexof | Ikea full filter + first bit |
| --- | ---: | ---: | ---: | ---: |
| 4 /8 | 7.29 /2.27 | 6.87 /2.23 | 7.94 /1.36 | 7.92 /1.35 |
| 6 /8 | 7.63 /2.95 | 7.89 /2.89 | 8.32 /1.59 | 8.56 /1.59 |
| 14 /32 | 14.48 /7.20 | 14.62 /7.24 | 11.56 /3.28 | 14.26 /5.14 |
| 30 /16 | 15.06 /5.19 | 17.06 /6.52 | 12.75 /3.08 | 14.21 /3.60 |
| 38 /16 | 14.42 /5.21 | 19.15 /8.14 | 11.97 /3.00 | 12.93 /3.45 |
| 62 /8 | 12.94 /4.78 | 22.59 /10.85 | 12.15 /4.13 | 13.64 /4.14 |

The narrow-tile latency loss is real: K5 `indexof` is 8.33 versus 7.37 ns for the
previous algorithm, while reciprocal cost falls from 2.63 to 1.59 ns. Wide bodies
can also expose a full-filter tradeoff: K62 full-mask latency rises from 12.94 to
13.64 ns while reciprocal cost falls from 4.78 to 4.14 ns. These are selected
throughput improvements, not universal latency improvements.

For shared-prefix late K30 hits, `indexof` is 13.42/4.06 against 14.22/3.60 for the
graduated full-filter consumer. Search saves latency but loses about 13% in
reciprocal throughput there. If a consumer needs all candidates anyway, keep
the full mask; dense prefix ties can make full-mask plus first-bit extraction
the better throughput choice even for search. This does not warrant a public
tuning flag. Body-first predictable misses are reported as one-stream cost,
never compared as strict serial dependency latency.

## Iceberg–Swiss consumer assessment

The peer reused the arithmetic seam with explicit eight-byte owner windows and
charged runtime preparation. Its job `20261015T234416Z-77cbc24a` measures W40,
K30, E30 at 85% occupancy and 13,926 keys, across three seeds and five repetitions.
Our [extract](evidence/arm-index/iceberg-consumer.json) reproduces its medians from
the raw CSVs and verifies the production filter/helper hashes match this graduation.

| Complete table lookup | Current SeriesPack path, latency / reciprocal ns | Whole-bucket LD3 + helper, latency / reciprocal ns |
| --- | ---: | ---: |
| Set | 7.00 /5.27 | 8.32 /6.39 |
| Eight-byte map | 18.29 /6.66 | 19.93 /7.78 |

The combined LD3 whole-bucket lowering loses and is not selected here. This
comparison changes body loading as well as residual evaluation; it does not
isolate the helper's cost. It also does not establish a whole-table before/after
gain from this graduation. The peer's table-based query preparation has a
slightly different tradeoff but does not reverse that result. Primitive and
index-consumer gains must not be advertised as blanket hash-table gains.

A later complete-table check directly substituted the public `indexof` (f11),
without the LD3 body change. Its [independently reproduced medians](evidence/arm-index/iceberg-indexof.json)
from job `20261016T000542Z-d0e784f0` also favor full filtering:

| Complete table lookup | Full filter, latency / reciprocal ns | Public indexof, latency / reciprocal ns |
| --- | ---: | ---: |
| Set | 7.00 /5.27 | 10.14 /7.30 |
| Eight-byte map | 18.32 /6.66 | 21.24 /8.94 |
| Associated singleton multimap | 21.64 /7.94 | 23.87 /11.29 |

The peer inspected the inlined complete loop: first-tile body and residual
branches delay the second tile, with unpredictable first-tile hits in this
query stream. There is no accidental outlined ordinary call. The peer retains
its full-mask path. The x86 follow-up adds a less learnable hot query stream to
check this branch risk before settling its defaults. This is a limit of the
standalone trace, not evidence that every consumer should use `indexof`.

## Code generation and validation

The K30 public witness expands the query once (`dup/cmtst/fmov`) and reuses it
for both groups. Its residual loads are at 24/26 and 54/56, ending at 29 and 59:
exact six-byte fields, unlike the owner-wide24/54 eight-byte reads. Shifted
ANDs remain single instructions; no outlined helper calls appear. The witness
accepts runtime query and activity; its instruction count is not an executed
path count or a timing estimate.

LLVM-MCA's V2 scheduling model estimates 0.8 cycles reciprocal throughput for
the five-instruction prepared word-load/arithmetic block and 1.5 with query
expansion included, assuming its weights are already live. These exclude
consumer work, domain/activity guards, rank extraction and the bounded-load
assembly. They are modeled costs, not measured timings.

Focused ASan/UBSan checks passed for the new index operation over Local, Striped,
headed and unaligned sources, sparse/empty activity, arbitrary windows, query
domains and exact inaccessible-page ends. The arithmetic body is checked for
all K1..7, every in-domain query, random planes and arbitrary unrelated upper
bytes. The full native regression also exercises all 64 widths, authored children,
duplicates, counts through 64 and the relative-position sentinel.

[Native loop audit](evidence/arm-index/codegen-audit.json) confirms query expansion
inside the measured recurrence; [selected native loop](evidence/arm-index/native-index30-loop.txt)
and [standalone witnesses](evidence/arm-index/arm-index-codegen.txt) retain the inspected code.
All five final profiles passed the complete filter/index regression and ordinary
example. Focused sanitizers and standalone-header checks passed. Production and
test hashes match the native capture and final confirmation. No SVE dependency,
borrowed-padding contract or additional public filter mode was graduated.
