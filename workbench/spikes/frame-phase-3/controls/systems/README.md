# Six local system-query cells

Actual DuckDB and Rust Polars over the same original `.u64` prefixes used by
Frame 3. This is the bounded B12 addition described in the
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

Before timing, each executable checks 72 empty, zero, full-u64-extreme,
high-base narrow and
all/none query combinations against independent original-value arithmetic.
These 72 synthetic cases currently use self-pairs (`A = B`); distinct A/B
alignment and typed-FOR UInt16/UInt32 paths must be qualified before a corpus
or performance claim.
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
The retained `prepared.json` currently contains `runs: []`: it records 72
synthetic preflight checks per engine and Python corpus expectations, not engine
execution of the corpus queries. Its earlier runner hash is superseded by the
cached-only and identity-sealing corrections. The unchanged driver-source and
binary preflight evidence remains valid; it does not qualify the revised runner
or establish timings.
Root owns matching these snapshots and result laws to current Frame/native
controls; this entry changes no shared prototype or paused system control.
