# Frame 3: qualified comparisons and remaining cells

25 October 2026 audit of the 24 October native captures. This is the B12
comparison account, not a graduation score. The qualified native COUNT and
materialized gather now have matching timed rows; narrow native whole SUM,
supplied-mask controls and native system-query comparisons still have gaps.
The six local VM cells now include actual DuckDB, Polars, Frame and narrow
native execution; their separate scope is recorded below.

## What can currently be compared

| Useful work | Zen 5 AVX-512 | Graviton 4 NEON | Remaining comparison limit |
| --- | --- | --- | --- |
| Whole exact SUM | Repeated complete columns, `measure-v5` | Same | Control remains own plain u64, not qualified narrow native SUM. |
| Predicate COUNT | Genuine narrow native COUNT, `seams-v3`, four columns | Same | One process, seven rotated rounds; URLHash and matching system-prefix cells remain. |
| A predicate → exact SUM(B) | Qualified narrow native pair, `seams-v3` | Same | Clustered wins and dispersed losses; no proved cold/residence panel. |
| Supplied fine-mask exact SUM | Own scalar-u64 control, `measure-v5` | Same | Qualified sparse/dense native routes are not yet timed in these captures. |
| Independent materialized points | Real native gather, `seams-v3`, repeated/eviction-sweep states | Same | Large losses remain; neither state proves a cache tier or DRAM. |
| Changes and owner history | Prior write-core timings; captured sizes-owner correctness | Same | No strong adaptive-native owner comparison; new relative-owner proof is separate. |
| Actual Polars / DuckDB query | Native comparison pending | Native comparison pending | Six checked cells complete locally with Frame/native; local VM results below do not fill these CPU cells. |

No matching AVX2 capture accompanies this cohort. Repeated execution, an
eviction sweep and demonstrated cache residence are different conditions.

## Source and qualification scope

Shared jobs `20261024T223705Z-dec974e4` (Zen 5) and
`20261024T223751Z-8ddba6a7` (Graviton 4) use Clang 21.1.8 and the runtime
leaf. The actual captured manifests match the shared timed/check sources at
`a74e1c44`: `frame.h`, `stages.h`, `trees.h`, `placeholders.h`, `runtime_width.h`,
`widths.cpp`, measure/seams/check and their CMake file. The four native-control
source/build/check files match too, including genuine COUNT and gather.
The 26 SeriesPack batch files match `0d44d527`; the worker script matches
`17191b6f`. The job's `source_commit=17191b6f` alone would miss the captured
working-tree additions. Source-manifest comparison, rather than that label,
establishes the relevant source identity.

Both native control checks pass **692,915 checks, 16 refusals and 5 protected
cases**. The source-qualified suite independently covers all residual widths,
u64 extremes, masks, duplicate/order-preserving output and refused overlaps.
These are optimized native checks, not native sanitizer results. Actual build
logs, complete output cohorts and empty seam error logs were inspected: the
worker deliberately tolerates some command failures, so its final exit zero
alone is insufficient evidence. Seams output is byte-identical to the retained
`seams-v3` files; raw round samples and linked-binary hashes are not retained
in this lean collection. An ignored audit receipt is at
`build/frame-helper/native-controls-v1/shared-native-b12.json`.

**Owner qualification is earlier than the shared relative experiment.** All
23 captured owner files match `e90542d9`; all ten changed by `d4ad21a4` differ.
The captured owner passes seven tests with 142 manifest, 2,700 binding and
87,718 stream-history checks, for **sizes form only**. Shared `a74e1c44` stage
checks exercise both sizes and relative ends, including their stream round
trips, but are not the checked relative-owner admission/history proof. Do not
attribute the later 284/5,885/350,630 coverage to these jobs.
[Captured checks](../proto/evidence/check-v4-zen5.txt),
[Graviton checks](../proto/evidence/check-v4-graviton4.txt);
[owner evidence](../owner/EVIDENCE.md) owns the separate newer qualification.

## Whole operations: preserve the weak-control label

`measure-v5` uses 2,999,808 original rows, 11,718 complete 256-row blocks,
rotated treatments and three process repeats. These are ns per block for
`frame_bound_pack_compact`, from the
[Zen summary](../proto/evidence/measure-v5-zen5.txt) and
[Graviton summary](../proto/evidence/measure-v5-graviton4.txt).
The own plain-u64 endpoint uses a carry-chain wide SUM; beating it does not
establish a lead over qualified narrow native or either actual system.

| CPU / ISA | Column | Frame SUM | Plain-u64 SUM | Frame COUNT ≥ median | Plain-u64 COUNT |
| --- | --- | ---: | ---: | ---: | ---: |
| Zen 5 / AVX-512 | OS | 34.66 | 64.92 | 11.05 | 36.12 |
| Zen 5 / AVX-512 | UserID | 72.72 | 63.55 | 2.84 | 34.67 |
| Zen 5 / AVX-512 | URLHash | 173.03 | 63.91 | 47.30 | 34.21 |
| Graviton 4 / NEON | OS | 33.60 | 134.46 | 42.98 | 86.71 |
| Graviton 4 / NEON | UserID | 128.50 | 135.12 | 8.05 | 86.07 |
| Graviton 4 / NEON | URLHash | 135.91 | 135.59 | 115.89 | 87.96 |

Relative ends improve G16 sizes SUM, but do not displace plain bound/pack
uniformly. For ClientIP, sizes → relative is 182.56 → 66.15 ns on Zen and
312.04 → 103.69 on Graviton; plain bound/pack is 35.27 and 61.08. Storage
rises from 21.17 to 32.37 bits/value. UserID rises from 28.65 to 48.14
bits/value. These are a representation tradeoff on the shared stage source,
not the later checked relative-owner result.

Supplied-mask SUM remains incompletely controlled. For URLHash, `measure-v5`
Frame supplied-mask / same-column FILTER-then-SUM is 409.75 / 909.20 ns on Zen
and 376.76 / 966.50 on Graviton. The own scalar-u64 routes are 346.47 / 62.86
and 307.49 / 124.70. The supplied-mask and predicate-producing operations have
different work; both qualified native supplied-mask routes still need matching
rows. No older, faster cohort substitutes for these current observations.

## Predicate COUNT: the real count-only control

[Zen seams](../proto/evidence/seams-v3-zen5.jsonl) and
[Graviton seams](../proto/evidence/seams-v3-graviton4.jsonl) use one process,
seven rounds with arm order rotated, and an independent original-value
`value >= median` oracle after each timer. `native_narrow_count` calls
`native_count_once` directly; it performs no payload SUM. Fitting and binding
are outside the timer. All values below are ns per original 256-row block;
these seam timings should not be spliced into the different measure harness.

| CPU | Column | Frame per-block law | Frame descriptor pass | Native narrow COUNT |
| --- | --- | ---: | ---: | ---: |
| Zen 5 | UserID | 4.03 | 1.07 | 26.14 |
| Zen 5 | CounterID | 1.01 | 0.48 | 6.08 |
| Zen 5 | OS | 17.31 | 8.96 | 5.98 |
| Zen 5 | EventTime | 13.65 | 12.91 | 8.49 |
| Graviton 4 | UserID | 6.29 | 6.02 | 85.56 |
| Graviton 4 | CounterID | 1.90 | 2.72 | 57.37 |
| Graviton 4 | OS | 46.57 | 39.66 | 58.78 |
| Graviton 4 | EventTime | 36.57 | 36.00 | 54.19 |

Descriptor COUNT wins substantially on UserID and CounterID. It loses to
native on **OS and EventTime on Zen** (1.50× and 1.52× in this process).
Graviton Frame beats this native control on all four columns; descriptor
fusion itself is slower than the per-block law on CounterID. These exact raw
rows take precedence over rounded prose or earlier cohorts. The old
`native_narrow_pair_count` SUM-and-discard arm is not a COUNT control.

## Cross-column SUM: topology decides the outcome

This same `seams-v3` cohort consumes exact u128 SUM answers and includes the
compiled narrow native `native_pair_once` control. B is ClientIP; thresholds
are p90 of A. Values are ns per original block, including predicate production.

| Predicate A | Selected / touched | Zen fused / retained mask / native | Graviton fused / retained mask / native |
| --- | --- | --- | --- |
| CounterID ≥ p90 | 30.7% / 30.7% | 10.11 / 14.91 / 79.65 | 21.08 / 28.21 / 50.62 |
| EventTime ≥ p90 | 10.0% / 74.0% | 62.86 / 82.50 / 84.65 | 112.71 / 120.21 / 62.55 |
| WatchID ≥ p90 | 10.0% / 100% | 153.97 / 167.50 / 95.51 | 310.82 / 316.44 / 106.17 |

The clustered win remains useful. WatchID Frame loses **1.61× on Zen and
2.93× on Graviton**; partly clustered EventTime also loses 1.80× on Graviton.
CounterID ties select 30.7%, so this is not a controlled relocation of one
identical 10% selection. SUM+MAX is a distinct demanded result; its one-pass
experiment does not repair these SUM-only losses or qualify standalone MAX.

## Points: materialized values, significant losses

The compact plan now uses 24-byte entries. The same column is replicated
eight times, adding the copy index with wrapping-u64 arithmetic: 23,998,464
rows. Each materializing arm writes the same 65,536 independent requested
values, with duplicates/order preserved. **Every output** is checked against
original values after every timer. Gather admission and preparation are outside
timing. The output is cleared before timing for every arm; output stores and
Loom draining are inside it.

`native_narrow_gather` calls the qualified typed/FOR materializing control.
The recorded `native_narrow_points_sum` is a different operation (reduction,
truncated checksum and output filling); none of its timings enters this table.
Ns per materialized request:

| CPU / column | State | Plain u64 | Native gather | Ready plan | Batch32 | Pipeline16 / 32 | Loom32 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Zen / ClientIP | repeated | 2.49 | 3.20 | 8.21 | 7.38 | 7.21 / 7.54 | 58.64 |
| Zen / ClientIP | eviction sweep | 4.62 | 6.02 | 37.57 | 23.32 | 22.63 / 22.68 | 31.35 |
| Zen / UserID | repeated | 2.65 | 4.38 | 66.40 | 17.58 | 15.14 / 60.25 | 25.93 |
| Zen / UserID | eviction sweep | 4.39 | 6.64 | 52.96 | 41.99 | 42.44 / 42.28 | 58.73 |
| Zen / OS | repeated | 2.50 | 3.59 | 65.23 | 19.54 | 17.30 / 17.59 | 64.70 |
| Zen / OS | eviction sweep | 4.55 | 5.08 | 56.36 | 34.86 | 31.95 / 32.10 | 43.50 |
| Graviton / ClientIP | repeated | 9.05 | 8.52 | 69.17 | 26.86 | 21.25 / 20.83 | 48.92 |
| Graviton / ClientIP | eviction sweep | 9.17 | 8.64 | 38.54 | 28.60 | 22.79 / 23.06 | 40.43 |
| Graviton / UserID | repeated | 9.05 | 9.60 | 55.51 | 32.41 | 18.40 / 20.41 | 77.43 |
| Graviton / UserID | eviction sweep | 9.08 | 9.43 | 62.17 | 38.02 | 24.38 / 24.87 | 44.71 |
| Graviton / OS | repeated | 8.79 | 7.45 | 68.55 | 46.77 | 35.77 / 34.49 | 57.69 |
| Graviton / OS | eviction sweep | 9.07 | 7.51 | 71.87 | 49.58 | 38.79 / 38.13 | 59.91 |

After the eviction sweep, Pipeline16 trails the real gather by **3.76–6.39×
on Zen and 2.59–5.17× on Graviton**. Shrinking entries has not closed the
point gap. Ready-plan and Loom costs are often worse. This is a strong reason
to retain plain/narrow native alternatives when independent points dominate.

The source's `flushed` label means visiting a separate **2 GiB buffer**, one
u64 per line; it is not cache maintenance on the queried allocations, and the
serving memory tier is unproved. `warm` means repeated execution, with no
stable hot-residence demonstration: arms, oracle reads and other allocations
can change one another's cache state. These are within-cohort observations,
not a cross-run cold ranking. Independent ns/request is throughput, not line
latency; neither line counts nor the plain arm's ~4.4 ns imply an irreducible
2× floor. Descriptor placement, dependent acquisitions and scheduling remain
hypotheses to test, not a demonstrated lower bound.

## Writes: prior core cost and owner cost are different rows


[writes-v2](../proto/evidence/writes-v2-zen5.txt) times mutable Frame policies
over 524,288 rows. For OS, hot-row slack32/buffer64 cost 168.5/90.3 ns/write;
outlier writes cost 394.2/387.4. The latter slack history performs eight group
re-encodes; this is not a forever-reserved append. These compare experimental
Frame policies, not native or system writers.

The separate four-Object, 262,144-row embedded history publishes 168 updates
and retains old readers. [Zen](../proto/evidence/e2e-v2-zen5.jsonl) and
[Graviton](../proto/evidence/e2e-v2-graviton4.jsonl) observations:

| CPU / column | Load+publish ms | Attach µs | Draft/apply/emit µs/write | Capture+publish µs/write | Fresh image bytes/write |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen / OS | 1.30 | 39.8 | 12.0 | 1.0 | 4,575 |
| Zen / UserID | 1.69 | 36.5 | 17.3 | 1.3 | 26,253 |
| Graviton / OS | 2.37 | 62.7 | 18.6 | 1.6 | 4,575 |
| Graviton / UserID | 3.17 | 62.8 | 25.8 | 1.8 | 26,253 |

These are per-history observations, not balanced native-control rankings or
complete history throughput. The apply timer does not include every piece of
outer owner work; group-local drafting is being improved. Fresh image bytes
are not copied bytes, peak retained backing, traffic or RSS. The history checks
matching old/new answers, reuse and contributions; it is not durable MVCC,
concurrent publication or an Engine summary benchmark.

The owner-side preparation investigation is complete and **excluded from the
initial build**, per Ashton's 25 October decision. These timings preserve the
research result; they do not qualify a point-write API. A frozen
O3 local AArch64 comparison uses the checked `ef08bed8` owner, shared
`a74e1c44` stages and matching SeriesPack `0d44d527`. Microseconds per
independent batch, **group-only / attach-all**:

| Column | 1 command | 8 commands | 64 commands |
| --- | ---: | ---: | ---: |
| OS | 21.282 / 30.318 | 45.740 / 54.928 | 57.069 / 67.017 |
| UserID | 26.493 / 35.475 | 54.486 / 64.876 | 70.832 / 82.957 |

Both arms copy and emit identical images (4,691 B OS, 25,620 B UserID), verify
every result and keep the source unchanged. Batches 8/64 rebuild once after
slack exhaustion. Group-only removes about 9–12 µs of eager attachment work
per batch here. Both paths allocate 4,096 journal entries and perform real
input/output validation plus exact final facts; the result does not isolate
those costs.

These are eight independent drafts from one original per round, not successive
publications. Construction, independent verification, returned-image disposal,
publication and durability are outside. The [owner evidence](../owner/EVIDENCE.md#completed-o3-preparation-experiment)
and [raw records](../owner/evidence/prepare-local-o3.json) preserve the boundary.
No native-owner ranking or complete write-throughput claim follows. The current
experiment is closed; scratch/source-proof and adaptive-native work is deferred.

## Deferred comparisons

The current cohorts establish the losses and useful pruning cases above.
With the preparation experiment closed, the following cells remain research
for the claims they would qualify. They are not another
matrix to run during consolidation or prerequisites invented for every initial
integration:

1. Match larger/native-CPU whole SUM and URLHash COUNT to the qualified narrow
   native entries. The six local 65,536-row prefix cells are complete below;
   neither those cells nor the existing four-column native count-only timings
   need repeating merely to obtain an already-qualified operation.
2. On the existing EventTime/WatchID → ClientIP masks, time both named sparse
   and dense native supplied-mask SUM routes. Keep predicate production as a
   separate complete operation and retain the dispersed losing case.
3. If point access is to be improved, carry forward the real gather and entire
   output oracle. One targeted representation/access change should discriminate
   the cause; an extra copy of the old staging matrix would not answer it.
4. Match a repeated-hot and dispersed/outlier owner history to a strong native
   owner at the same grain, charging preparation/copy/emit/publication, retained
   bytes and finalization. New relative-owner native qualification is separate
   from a timed writer comparison.

Actual-buffer cache maintenance or a declared large sequential population must
have its own allocation/access account if that research is resumed. Actual
Polars/DuckDB local timing is complete below; repeated local VM observations
cannot fill native Zen/Graviton cells. The current evidence already supports
retaining plain/narrow choices for wide scans, dispersed selections and points.

## Actual system cells

The pinned [Rust Polars control](../../frame-phase-2/instruments/systems/polars/README.md)
uses real LazyFrame queries and all eight integer dtypes; the
[DuckDB control](../../frame-phase-2/instruments/systems/README.md) uses prepared
C API queries over engine-owned tables. Neither existing executable accepts
the Frame corpus/timing work: timing the qualification process would include
construction and unrelated fixtures. DuckDB's current SMALLINT/BIGINT fixture
does not qualify arbitrary unsigned corpus values.

The bounded addition is six **local repeated** cells per engine, using identical
original 65,536-row prefixes: OS/URLHash SUM and median COUNT, EventTime/WatchID
p90 → SUM(ClientIP),COUNT. Record actual prefix hashes, threshold, selected and
touched counts. CounterID's ties must never be silently relabelled as 10%.

Sidecar UInt64 is contract parity, **not necessarily the strongest system
storage**. OS/EventTime include order/sign bias; original ClickBench SQL types
and arithmetic are not inferred from those bits. Native typed/FOR is the
primary computational floor. Each logical system cell therefore retains two
named arms: raw `sidecar_uint64`, and `external_typed_for`, which subtracts a
column minimum, stores actual UInt8/16/32/64 values, translates the predicate
and restores `B.base * selected_count` after SUM. Fitting, conversion and query
preparation are charged separately. The adapter is not a claim that either
engine automatically selects this representation.

Another direct route is to undo the known input-metadata bias (for example
`2^63` for signed OS/EventTime), use the actual original signed/narrow dtype,
translate the threshold, and restore that bias times the selected count to
compare stored-domain sums. The current typed-FOR arm covers the stronger
bounded alternative without silently calling a wide UInt64/Int128 OS query
the best original-dtype system control. Neither arm establishes original
ClickBench SQL performance or full system superiority.

- Preserve UInt64 input bits, including values above INT64_MAX. Exact totals
  for these cells fit positive i128; natural overflowing UInt64 SUM is excluded.
  Polars retains a safe natural accumulator or names its query-time Int128
  widening. DuckDB ingests UBIGINT and must return an exact HUGEINT/UHUGEINT
  aggregate, never a DOUBLE conversion. Check extrema, empty and all/none
  predicates before timing. DuckDB empty SUM is NULL; Frame/Polars zero is
  compared through the explicitly stated result law.
- Use one query thread and immutable owned input. Exclude process startup and
  file reading. Report typed-column/table construction and query preparation
  separately. Execute the retained query/prepared statement repeatedly,
  consuming every demanded scalar; query cloning, binding, optimization and
  result destruction remain assigned to their actual timed boundary.
- Repeated execution is not proof of L1/LLC residence. Do not flush original
  appender buffers and call DuckDB's owned table cold. Engine-private retained
  capacities and transient peaks remain unknown when unavailable; report known
  source buffers and visible Polars payload separately instead of equating
  input bytes with total retained memory.

The [bounded system entry](systems/README.md) now retains executed raw UInt64
and explicit external typed-FOR arms: both engines passed 108 synthetic
checks and all 24 corpus records passed independent original-row oracles.
The shared-stage Frame/native executable passed 90 synthetic answers and all
12 matching corpus records. All use the same five 65,536-row snapshots,
stored-domain arithmetic, thresholds and result law. On this prefix,
median selects 40,054 OS and 33,055 URLHash rows; p90 EventTime selects 6,555
rows touching 231/256 blocks, WatchID 6,554 touching all blocks. These counts
are not the larger seam population. Compiler activity is an explicit timer
admission rejection. Each cohort passed that gate and checked every timed
answer. The complete receipts remain in the two small local control outputs;
[compact retained evidence](systems/evidence/local-arm-six-cells.json)
preserves inputs, sources, result boundaries and samples.

Microseconds per complete query, nine samples, **local Linux AArch64 VM**:

| Case | Frame G16 | Narrow native | DuckDB UInt64 / FOR | Polars UInt64 / FOR |
| --- | ---: | ---: | ---: | ---: |
| SUM(OS) | 4.083 | 2.000 | 278.314 / 134.552 | 176.056 / 9.667 |
| COUNT(OS ≥ median) | 5.333 | 10.125 | 159.763 / 183.431 | 20.669 / 14.626 |
| SUM(URLHash) | 30.793 | 8.000 | 294.731 / 299.732 | 176.222 / 180.764 |
| COUNT(URLHash ≥ median) | 18.709 | 6.334 | 158.679 / 255.562 | 20.376 / 21.168 |
| EventTime ≥ p90 → SUM(ClientIP), COUNT | 14.375 | 7.000 | 222.226 / 202.891 | 67.130 / 46.462 |
| WatchID ≥ p90 → SUM(ClientIP), COUNT | 45.001 | 13.250 | 286.189 / 216.434 | 65.755 / 54.087 |

Frame uses the shared bound/compact stages with **16-block placement**, the
owner image grain. It executes built stage arrays, while actual group images
are emitted separately for the format-size account; this is not a timing of
the checked owner facade or attachment. The larger seam tables above use
part-wide placement. The native floor uses the qualified typed/FOR SUM,
count-only and fused pair entries. Matching SeriesPack `0d44d527` headers and
archive are retained; the later partial READ patch is not substituted into
this receipt. These query families use the unchanged compiled reduction and
filter operations.

Frame is ahead of both actual engines in these cells, but their query
planning/result boundaries are larger. The computational gaps remain useful:
Frame trails narrow native 2.0× for OS SUM, 3.85× for URLHash SUM, 2.95× for
URLHash COUNT and 2.05×/3.40× for the two pairs. OS COUNT wins 1.90×. These
observations support keeping a plain/narrow native choice for wide scans and
dispersed selection; engine overhead does not make those Frame costs disappear.

There is no compression win on incompressible URLHash here: Frame emits
528,864 image bytes versus 524,288 native payload bytes. OS emits 74,721 versus
65,536; the two pairs emit 407,151 / 776,209 versus 524,288 / 786,432. Images
and typed payloads are different accounts from complete retained memory.
Frame private capacities and engine-private memory remain unknown.

The engine and Frame/native cohorts are separate invocations, with one corpus
process each; Frame/native arm order rotates, engine order does not. Source
qualification and result parity are complete, while a balanced repeated-process
ranking, proven cache residence and native Zen/Graviton system comparison
remain open. These local observations cannot fill those cells by substitution.
