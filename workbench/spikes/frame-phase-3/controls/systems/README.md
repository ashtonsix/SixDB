# Six local system-query cells

Actual DuckDB and Rust Polars over original `.u64` prefixes from Frame 3's
corpus. This is the bounded B12 addition described in the
[scorecard](../SCORECARD.md), not another representation, a native fleet panel
or a comparison against a process running all qualification fixtures.

Each engine runs six logical cases on 65,536 rows: whole SUM and median COUNT on OS
and URLHash, then p90 EventTime/WatchID → SUM(ClientIP),COUNT. Inputs are
**stored-domain unsigned UInt64**, preserving the sidecars' exact bits and row
order. This deliberately does not reinterpret an order-key sidecar as its
original signed SQL dtype. Input hashes, actual thresholds, selected rows and
touched 256-row blocks are recorded. A prefix is a forcing input, not a
population sample. Each has both raw sidecar UInt64 and an explicitly named
external typed-FOR arm; these are twelve result records per engine, not extra
logical queries. The source artifact is the existing
[phase-1 input reference](../../../frame-phase-1/encoding/worker-inputs.json).

## Same answers, explicit arithmetic

Before timing, each executable requires 108 synthetic query checks against
independent original-value arithmetic. The original 72 empty, zero,
full-u64-extreme, high-base narrow and all/none cases use self-pairs (`A = B`).
Another 36 cases use distinct, noncorrelated A/B values with high nonzero bases:
UInt16 predicate/UInt32 payload and UInt32 predicate/UInt16 payload, each in raw
and typed-FOR form for SUM, COUNT and SUM+COUNT at all/none/interior thresholds.
Typed lanes are asserted against actual engine types; the UInt32 payload's
interior selected residual SUM exceeds UInt32 and requires Polars' UInt64
query-cast route. **Both engines passed all 108 checks**, then executed every
corpus case in both storage arms. The completed receipt is described below;
its source epoch includes the distinct-column and UInt16/UInt32 checks.
The runner independently reads the exact corpus bytes and recomputes every
timed case using Python integers. Every measured result is consumed and checked
after its clock stops. Whole SUM requests only SUM; standalone COUNT requests
only COUNT; the pair requests SUM and COUNT. A row count reported for whole SUM
is known input cardinality, not a hidden extra engine query.

All nonnegative totals are bounded below `2^80`. Polars retains natural UInt64
SUM only when the full input total proves every subset/intermediate safe;
otherwise the query casts B to Int128 before SUM, retaining UInt64 storage.
The output names that `Int128_query_cast` route. It is an exact widened scalar
equivalent, not a claim about the speed of overflowing UInt64 arithmetic.
COUNT does no payload SUM or accumulator-bound scan.

DuckDB ingests UBIGINT columns through its ordinary chunk/appender API and
executes natural `SUM(b)`. The caller accepts only an actual exact
HUGEINT/UHUGEINT result, checking the nonnegative high half; it never converts
through DOUBLE. SQL NULL SUM for an empty domain is retained as validity and
compared with zero plus empty validity. Polars returns integer zero for the
same empty case. No NULL input rows, missing-value semantics or floats are
claimed by these cells.

The `external_typed_for` adapter subtracts the minimum and stores actual
unsigned 8/16/32/64-bit engine columns. It translates thresholds and restores
the payload base times the selected count in the timed exact result. Widths,
bases and actual accumulator routes are recorded; fitting and conversion are
inside construction. Polars small lanes keep their natural Int64 SUM, UInt32
uses its natural accumulator only when proved safe (otherwise UInt64), and
wide residuals widen only as necessary. No new dependency or feature is needed.
This is a deliberately supplied representation, not an automatic system policy.

The raw sidecar arm is not the strongest original ClickBench dtype benchmark.
For example, undoing a known `2^63` order/sign bias permits the original
signed/narrow column and translated predicate, with the bias restored times
selected count for stored-domain comparison. Native typed/FOR remains the
primary floor. Neither external arm establishes full-system superiority.

## What the clocks and memory mean

Both engines use one query thread, owned immutable columns and an untimed warm
query before repeated samples. Process startup, corpus IO and preflight checks
are outside clocks. Construction is a separate one-shot observation, in fixed
case order: Polars constructs its typed DataFrame; DuckDB opens/configures the
database, creates the table, appends typed chunks and closes the appender.
Preparation separately includes the actual logical query/statement construction
and threshold binding. It is not balanced encoding/fitting throughput.

The repeated DuckDB call executes its retained prepared statement, fetches and
consumes the exact scalars, then destroys result chunks/results. Polars clones
the retained logical LazyFrame, optimizes/collects it, consumes the scalars and
destroys the result. Their preparation mechanisms differ; the records name
these real boundaries rather than calling them identical microkernels.

Repeated execution is not evidence of a serving cache level. There is no flush
path: original appender buffers are not DuckDB's owned storage. No DRAM or
native Zen/Graviton speedup can be inferred from these local ARM VM samples.

The driver retains five original column vectors, regardless of the active
case. `known_source_capacity_bytes` identifies the active source buffers;
the full five-column population remains explicit in the receipt. Polars'
`estimated_size()` is visible array payload, not total allocation capacity.
DuckDB private table/plan capacities and both engines' private allocator/peak
costs are unknown. Source bytes plus visible payload must not be presented as
complete retained memory.

## Qualified local execution

The completed `build/frame-helper/system-corpus-v1/qualification.json`
(SHA256 `236dc62a1edc6d69d5f01f989af1ec76883e0dcc9dd8c7250566301759cf8891`)
records two engines × six corpus cases × two arms: **24 result records**, each
with nine raw query samples on 65,536 rows. It was written at
2026-10-24 23:04:21 UTC. This is the local Linux AArch64 OrbStack VM, pinned
to CPU 8 with one query thread. Timer admission observed 100% idle ticks on
that CPU for 300 ms and no active compiler before or after the queries; it
does not establish quiet execution throughout the interval or a cache tier.

Medians below are microseconds per complete retained query call, including
the exact scalar consumption and result destruction described above.
Construction and preparation are outside these query clocks.

| Case | DuckDB UInt64 | DuckDB external FOR | Polars UInt64 | Polars external FOR |
| --- | ---: | ---: | ---: | ---: |
| SUM(OS) | 278.314 | 134.552 | 176.056 | 9.667 |
| COUNT(OS ≥ median) | 159.763 | 183.431 | 20.669 | 14.626 |
| SUM(URLHash) | 294.731 | 299.732 | 176.222 | 180.764 |
| COUNT(URLHash ≥ median) | 158.679 | 255.562 | 20.376 | 21.168 |
| EventTime ≥ p90 → SUM(ClientIP), COUNT | 222.226 | 202.891 | 67.130 | 46.462 |
| WatchID ≥ p90 → SUM(ClientIP), COUNT | 286.189 | 216.434 | 65.755 | 54.087 |

The UInt64 Polars SUM/pair cells use the named Int128 query cast. External-FOR
OS uses UInt8 storage and natural Int64 SUM; ClientIP uses UInt32 residual
storage and a UInt64 SUM cast. URLHash remains UInt64 with Int128 SUM. Thus the
OS improvement changes both physical storage and aggregate arithmetic; it is
not an isolated dispatch comparison. All DuckDB SUM cells return HUGEINT.

One-shot construction observations span 9.02–11.15 ms for DuckDB and
43.75–164.72 µs for Polars; preparation spans 246.60–588.96 µs and
0.58–30.84 µs respectively. DuckDB construction opens/configures a database
per case, whereas Polars constructs its DataFrame. Those boundaries are real
and different; they are not equivalent encoding costs. Complete engine-retained
memory and peak allocation remain unknown.

This is one corpus process per engine with fixed case/arm order, not repeated-process
ranking evidence. Some nine-sample ranges are wide: DuckDB raw URLHash SUM
269.521–575.796 µs, Polars external-FOR EventTime pair 44.254–90.715 µs.
The URLHash COUNT FOR slowdown in DuckDB therefore needs confirmation before
attributing it to representation; FOR also changes values, thresholds and
engine statistics. These numbers establish executed, checked corpus cells.
They supply no native Zen/Graviton result. The separately qualified
Frame/native cohort below has matching answers and inputs, with its own
execution and preparation boundary.

The seven captured caller/helper source hashes match the current files.
The measured driver epochs are C++ `35e5e7db4182…`, Rust
`12c7153f318f…`, runner `f0c7355c00fb…`; retained binaries are DuckDB
`cc8e2e22bbd8…` and Polars `80a7611b99b1…`. Full identities, independent
Python expected answers, snapshot hashes and raw samples are in the receipt.
`review108.json` records the subsequent read-only audit: all source/binary/input
identities verified, medians recomputed, and corpus answers independently
recalculated from the retained snapshots. No extra engine execution was needed.

## Matching Frame and narrow native

[frame.cpp](frame.cpp) reuses the shared Frame stages and qualified native
SUM/count-only/fused-pair controls. Its 90 synthetic answers include empty,
zero and UInt64 extremes, distinct UInt16/UInt32 operands in both directions,
and all/none/interior predicates; actual native lane widths are asserted.
Image emission is included in preflight. All twelve corpus records match the
same independent original-row answers, thresholds and selected/touched counts
as the engine cells. The [scorecard](../SCORECARD.md#actual-system-cells) gives
the matched timings and their computational gaps; [retained evidence](evidence/local-arm-six-cells.json)
keeps all 36 records and raw samples from the three cohorts.

This Frame recipe constructs bound/compact stages at **16-block placement**,
the owner image grain. The shared convenience `bound_pack()` uses part-wide
placement, whose arrays cannot emit these group images. The query executes
built stage arrays; separately emitted images are discarded after counting
their actual bytes. Thus format size is measured, but execution through an
attached/checked owner binding and complete live capacity are not. The record
names both `execution_storage` and the two co-resident Frame/native treatments.
This differs from the part-wide large-corpus seam experiment.

Frame/native construction includes fitting, building and domain admission.
Repeated queries use prepared scratch and rotate the two arms each round;
they consume exact scalars. Source IO, fitting, serialization and the
independent oracle are outside query clocks. Matching SeriesPack `0d44d527`
headers/archive and shared `a74e1c44` stages are sealed in
`build/frame-helper/frame-system-match-v1/qualification.json`. The later
partial READ patch is not substituted into that measured binary. These cells
use the unchanged compiled reduction/filter kernels. This is another local
VM observation, not a balanced cross-engine or native CPU ranking.

The CMake target is `frame3_frame_corpus`, selected with the phase-3 spike;
`FRAME3_LEAF=runtime` avoids all typed width TUs. From a configured release
build, build that target and run its `--check-only` before timing:

```sh
cmake --build BUILD --target frame3_frame_corpus
BUILD/workbench/spikes/frame-phase-3/controls/systems/frame3_frame_corpus --check-only
taskset -c 8 BUILD/workbench/spikes/frame-phase-3/controls/systems/frame3_frame_corpus \
  build/frame-helper/system-corpus-v1/inputs 65536 9
```

The standalone driver does not admit the host's load; the caller must coordinate
compilers and competing work. The retained run reused `run.py`'s CPU-idle and
active-compiler gate, source/input/binary sealing, and exact corpus expectations.
`frame3_frame_corpus_check` registers its synthetic preflight with CTest.

## Cached, pinned build

The runner reuses the official cached DuckDB 2.0.0 library/header from the
[qualified control](../../../frame-phase-2/instruments/systems/README.md),
checking the actual library digest. Its caller uses Clang 21.1.8; caller flags
do not describe the prebuilt engine's compiler or ISA.

The Rust leaf reuses the existing Polars 0.54.4 source epoch, exact Cargo.lock,
nightly-2026-04-01, effective features, release profile, target flags and task-local
toolchain/cache described by the
[qualified native control](../../../frame-phase-2/instruments/systems/polars/README.md).
Only the new leaf and executable link should build. `--offline --locked` avoids
dependency acquisition; the runner stops if Cargo starts compiling another
package. Existing engine dependencies need not be rebuilt. This is not the
historical Python wheel's build, nor a sanctioned source-cache mutation.
The runner requires the actual cached nightly Cargo/rustc executables and calls
them directly, so a Rustup shim cannot install a missing toolchain. Its source
verifier only reads the pinned archives and extracted trees; it never downloads
or extracts a missing prerequisite.

From the repository root, with Linux commands prefixed `orb -m ubuntu` on Mac:

```sh
python3 workbench/spikes/frame-phase-3/controls/systems/run.py \
  --inputs build/workers/20261022T235121Z-970b6dfa/results/in/inputs/cols \
  --rows 65536 --rounds 9 --cpu 8
```

Use `--prepare-only` while another compiler is active: it builds and qualifies
without starting query timers. Each executable also accepts `--check-only`.
The runner admits timers only after its own builds and preflight have finished,
with no active compiler and at least 90% idle ticks on the selected CPU over a
300 ms observation. It records host/load evidence and rejects a compiler still
present after queries. This bounded check cannot prove the whole interval was
quiet; coordinate other builds too. Rejected attempts write
`timing-rejected.json`, without a new `qualification.json`. Use an odd number of
rounds (1–101), matching the executables' middle-sample median.
The exact five input prefixes are copied to `inputs/` with hashes and the
original artifact/source-file identities, so a standalone results archive
retains its runtime data. The Rust executable is retained beside the C++ one.

No download/bootstrap fallback exists. Missing cached prerequisites are reported.
The default ignored output is `build/frame-helper/system-corpus-v1/`.
`source-receipt.json` freezes source, binary, toolchain, upstream and exact input
identities **before** the timer executables run. Caller-source hashes are taken
before compilation and checked after builds and preflights; retained input and
binary identities are checked before either prepare-only completion or timing.
`qualification.json` adds
commands, actual raw samples, independently checked answers and before/after
identities. Construction, preparation and query samples remain separate.
The current `prepared.json` contains `runs: []` and **108 passing checks per
engine**. Its source, binary, input, expected-answer and upstream identities
match `source-receipt.json` and the completed `qualification.json`; it is the
same qualified epoch, rather than the earlier 72-case preparation.
`source-receipt.json` additionally records accepted timer admission before query
execution. `qualification.json` adds both successful 12-record engine runs,
and `run108.log` names that completed receipt. The preparation built only the
new Rust leaf/link; the subsequent timed invocation found every Cargo artifact
fresh and performed no dependency rebuild.
Root owns matching these snapshots and result laws to current Frame/native
controls; this entry changes no shared prototype or paused system control.
