# Frame 3: qualified comparisons and remaining cells

24 October 2026. B12 account of the retained phase-3 prototypes, not a
graduation score or a combination of phase-1/phase-2 performance ratios.
The new [native controls](README.md) are functionally qualified locally;
their current-source native performance has not been measured.

## What can currently be compared

| Useful work | Zen 5 AVX-512 | Graviton 4 NEON | Missing before a strong comparison |
| --- | --- | --- | --- |
| Whole exact SUM / predicate COUNT | Repeated complete columns, `measure-v4` | Same | Rerun narrow native SUM and genuine standalone COUNT; the old plain-u64 endpoint remains useful but is insufficient. |
| A predicate → exact SUM(B) | Repeated, qualified narrow native pair in `seams-v1` | Same | Preserve the real pair control and losing cases; repeat at declared residence and smaller/larger populations. |
| Supplied fine-mask exact SUM | Repeated, own scalar-u64 control in `measure-v4` | Same | Rerun both new sparse and dense supplied-mask controls. Predicate production is a separate operation. |
| Independent points | Materialized results, repeated and eviction-sweep states in `seams-v1` | Same | Rerun the qualified narrow FOR gather at the same materialized-output boundary, or exact consumed reduction shared by all arms; actual target-buffer conditioning remains separate. |
| Changes and owner history | Write-core policies plus separate embedded Orbital witness | Same | Adaptive native owner at the same grain and history; include preparation/copy/emit/publication and retained memory. |
| Actual Polars / DuckDB query | No current phase-3 timed comparison | No current phase-3 timed comparison | Existing native checks establish semantics, not speed. The finite system cells below precede any native claim. |

AVX2 exists in earlier phase-3 cohorts; no matching latest `measure-v4` or
`seams-v1` AVX2 export is retained here. Do not silently put an older AVX2
number beside the latest kernels. There is no bandwidth-bound current-Frame
panel. Repetition, an eviction sweep and demonstrated cache residence are
different conditions.

## Retained phase-3 numbers

`measure-v4` runs 2,999,808 original ClickBench rows (11,718 complete blocks),
single core, with rotated treatments and three process repeats. The recorded
source baseline pins Ikea/bounds to `9e0cf2a8`; later erased-leaf and new control
changes are not measured. These are the retained summaries from jobs
`20261024T190557Z-904b94ed` and `20261024T190629Z-91076505`:
[Zen 5](../proto/evidence/measure-v4-zen5.txt),
[Graviton 4](../proto/evidence/measure-v4-graviton4.txt).

Whole operations, ns per 256 rows. Frame is `frame_bound_pack_compact`.
The plain endpoint stores 64-bit values and uses a carry-chain wide SUM;
it is **not the qualified narrow native FOR control**. Raw timings are kept
without a claim that beating this endpoint establishes the 2× system bar.

| CPU / ISA | Column | Frame SUM | Plain-u64 SUM | Frame COUNT ≥ median | Plain-u64 COUNT |
| --- | --- | ---: | ---: | ---: | ---: |
| Zen 5 / AVX-512 | OS | 32.89 | 63.94 | 20.73 | 34.24 |
| Zen 5 / AVX-512 | UserID | 71.17 | 63.63 | 4.85 | 32.43 |
| Zen 5 / AVX-512 | URLHash | 164.16 | 61.88 | 42.81 | 29.16 |
| Graviton 4 / NEON | OS | 30.34 | 128.02 | 44.03 | 62.18 |
| Graviton 4 / NEON | UserID | 110.18 | 128.29 | 6.99 | 61.72 |
| Graviton 4 / NEON | URLHash | 124.71 | 127.80 | 97.87 | 60.22 |

The later descriptor-pass experiment changes COUNT again: UserID per-block
law → descriptor pass is 2.50 → 1.02 ns/block on Zen, but 3.77 → 4.25 on
Graviton. CounterID is 0.94 → 0.48 and 1.85 → 2.64 respectively. This is a
same-cohort Frame seam comparison. Its `native_narrow_pair_count` arm computes
SUM as well as COUNT and discards the SUM: **exclude it from standalone COUNT
rankings** and rerun `native_count_once`.

The corresponding selected-SUM rows include random roughly 10% supplied row
bits and a complete same-column FILTER-then-SUM. For URLHash, Frame is
116.02 / 145.95 ns/block on Zen and 322.66 / 834.35 on Graviton, respectively.
The own scalar-u64 control is 309.75 / 62.34 and 306.68 / 116.26. These algorithms
are materially different; neither row is evidence of a lead over the new
qualified supplied-mask controls. The second number includes predicate
production; the first does not.

### Cross-column operation: a real narrow native control

The [Zen](../proto/evidence/seams-v1-zen5.jsonl) and
[Graviton](../proto/evidence/seams-v1-graviton4.jsonl) seam exports use one
process, seven rotated rounds, the same original rows and actual exact u128
SUM answers. `native_fused_pair_sum` calls the compiled narrow typed/FOR
`native_pair_once`, producing the A predicate within timing. Frame's per-part
fusion skips blocks whose mask is empty. All numbers below are ns per original
256-row block; B is ClientIP throughout.

| Predicate A | Selected rows / touched blocks | Zen Frame fused / retained mask / native | Graviton Frame fused / retained mask / native |
| --- | --- | --- | --- |
| CounterID ≥ p90 | 30.7% / 30.7% | 6.79 / 12.17 / 79.25 | 16.70 / 24.14 / 47.90 |
| EventTime ≥ p90 | 10.0% / 74.0% | 47.55 / 64.34 / 79.37 | 80.20 / 87.71 / 54.29 |
| WatchID ≥ p90 | 10.0% / 100% | 113.75 / 138.10 / 93.24 | 272.70 / 279.71 / 89.91 |

There is a useful clustered win and a substantial dispersed loss, especially
on Graviton. CounterID ties select 30.7%, not 10%; these three predicates are
not a controlled relocation of one identical selection. No cold or 2×-system
claim follows. SUM+MAX uses a different projection/materialization path and
its scalar native control is not interchangeable with this table's SUM arm.

### Points: preserve the observed loss and the actual operation

`seams.cpp` replicates the column eight times, adding the copy index with
wrapping u64 arithmetic: 23,998,464 rows. Each arm materializes the same 65,536
requested values into an output vector, then checks a modulo-u64 checksum
outside timing. This is neither dependent latency nor the new exact-u128
consumed-point reduction. Selected examples, ns per materialized point:

| CPU / column | State | Plain u64 | Ready plan | Prefetch batch32 | Loom Ring32 |
| --- | --- | ---: | ---: | ---: | ---: |
| Zen / ClientIP | repeated | 2.17 | 7.51 | 7.29 | 12.28 |
| Zen / ClientIP | eviction sweep | 4.41 | 38.89 | 23.45 | 35.86 |
| Zen / UserID | repeated | 2.42 | 16.48 | 60.24 | 21.42 |
| Zen / UserID | eviction sweep | 4.43 | 54.90 | 33.02 | 50.57 |
| Graviton / ClientIP | repeated | 4.45 | 33.37 | 11.10 | 18.79 |
| Graviton / ClientIP | eviction sweep | 8.41 | 36.54 | 26.07 | 30.36 |
| Graviton / UserID | repeated | 4.76 | 71.20 | 19.82 | 75.94 |
| Graviton / UserID | eviction sweep | 8.41 | 53.69 | 34.80 | 42.15 |

The source's `flushed` label means visiting a separate vector of `256<<20`
u64 elements, one per line: **a 2 GiB eviction sweep**, not cache maintenance
on the queried allocations. Its efficacy/serving tier is unmeasured. The
plain array is a valid materialized-output reference and wins substantially;
new native narrow gather timings are still owed. `native_gather_once` now
materializes every requested logical u64 in order, preserving duplicates and
writing zero for coarse-excluded requests. It has independent per-output
qualification. A subsequent `native_narrow_points` attempt that reduced SUM
and filled the output with zero plus one checksum was rejected: matching a
checksum does not make it the same operation as materialization. To compare
the separate `native_points_once`, make every arm reduce the same request list
to exact sum/count. Do not divide the timings of unlike results.

### Writes: core cost and owner cost are different rows

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

## Finite next cells

Keep the current arms and losing cases. Reuse the existing driver and the
compiled controls rather than changing the representation for this comparison.

1. OS and URLHash: whole SUM and median COUNT on one 65,536-row part, then
   the existing larger population. Add `native_sum_once`/`native_count_once`;
   keep plain u64 and hand glue as named diagnostics.
2. EventTime and WatchID → ClientIP: exact SUM/count at the frozen p90, with
   retained and per-part masks against `native_pair_once`. Add supplied-mask
   SUM on those same masks against both named native mask routes; a random
   roughly 10% mask stays a separate topology control.
3. ClientIP and UserID independent requests: materialized values, duplicates
   and order preserved, against `native_gather_once`, checking the whole output.
   An exact SUM/count reduction is a separate operation against
   `native_points_once` and requires every arm to consume that same result.
4. One repeated-hot and one dispersed/outlier owner history at the same group
   grain, through reserve exhaustion. Match an adaptive native owner and
   demanded reads; report publication and outstanding/finalized debt separately.

Run the ready repeated path first on each actual CPU/ISA. Actual-buffer
cache-maintenance or a declared large sequential population is a separately
labelled addition, with its own allocation and access account. Never promote
an eviction label to a DRAM claim. Balance treatments and retain raw samples;
close one-process rankings need repetition before selecting a policy.

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

Local system observations, once available, will qualify the executable cells
and their costs on the local Linux ARM VM. They cannot supply a Zen/Graviton
speedup or fill the native scorecard by substitution. Root owns the matched
current-Frame/native measurements on these same snapshots.

The [bounded system entry](systems/README.md) now builds from the pinned local
cache and passes 72 preflight cases per engine for both storage arms. Exact
65,536-row prefixes and the source-artifact reference are retained with the
prepared receipt. On this prefix the median selects 40,054 OS rows and 33,055
URLHash rows; p90 EventTime selects 6,555 rows touching 231/256 blocks, and
WatchID selects 6,554 touching all blocks. These differ from the larger seam
population. Query timing remains deferred during concurrent template compilation;
the runner now rejects an active compiler before starting timers and records
that admission evidence. Preflight success fills no timing cell.
