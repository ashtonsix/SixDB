# Native Rust Polars query control

This front-A control runs actual optimized `LazyFrame` queries on normally
constructed, owned immutable integer columns. It has its own Cargo lockfile
and pinned nightly toolchain. It does not delegate queries to Python.

The [phase-1 scan findings](../../../../frame-phase-1/scan/FINDINGS.md) preserve
complete scalar consumption, exact arithmetic and useful cold-access negatives;
their Python runtime/conditioning costs do not isolate a frontend effect.
The adjacent [DuckDB control](../README.md) supplies the independent SUM and
two COUNT-law starting point. Here Polars' natural empty/all-null SUM is checked
as zero, separately from DuckDB's SQL NULL law. No timing, cache-residence or
native x86 claim follows from this functional check.

The source pin is Python Polars 1.42.1 / Rust workspace crate 0.54.4,
`0df0c25d4db895ec8cad773bedc4e98400e3135e`, with upstream's
`nightly-2026-04-01`. The leaf Cargo package explicitly retains the optional
upstream `object_store` resolver patch at its locked revision. Source archives
and the effective dependency graph remain independently recorded; a leaf package does
not inherit upstream workspace patches, profiles or lockfiles automatically.

## Build choices

The requested features are `lazy`, `nightly` (including SIMD), `cse`, signed
and unsigned 8/16-bit dtypes, and signed/unsigned 128-bit accumulators; ordinary
32/64-bit dtypes are already available. LazyFrame's normal optimizations remain
enabled. The compiled planner activates IO's async/CSV support; this control
performs no IO. Cargo metadata reports a broader feature union, including cloud
and operations absent from the release artifacts. The receipt uses actual
compiler-artifact features for the effective build. The optional `object_store`
patch preserves resolver continuity but is not compiled into this executable;
the backtrace and jemalloc patches are also absent from the compiled graph.

The executable uses the native system allocator, release optimization level 3,
thin LTO, 16 codegen units, line-table debug information, and explicit
`target-cpu=generic` / `target-feature=+neon`. Build concurrency and CPU affinity
are one job on CPU 8; `POLARS_MAX_THREADS=1` controls query execution. Cargo's
effective compiled features and profiles are recorded alongside `rustc -Vv`.

This is a separately built native control at the historical source epoch.
The upstream Python release recipe selects `dist-release` (fat LTO, one codegen
unit), broad Python/runtime features and a different allocator configuration.
Matching the source version does not reconstruct the historical wheel's exact
effective build. These differences preclude a pure frontend-cost comparison.

## Query and arithmetic laws

Each call constructs a typed threshold expression and executes
`filter(A >= threshold).select(SUM(B), COUNT(B), len())`, then reads all three
returned integer scalars. Repeated and nonmonotonic literal bindings reuse the
same immutable columns; this is not a prepared SQL-parameter API. Independent
Rust loops over the original logical `Option` values compute signed `i128` or
unsigned `u128` expectations, dropping null predicates and excluding null B
only from SUM and COUNT(B).

SUM keeps the natural Polars accumulator whenever a proof covers every subset
and intermediate. The proof sums all negative and positive payloads separately;
it does not rely on a cancelling final answer. Small lanes naturally promote
to Int64. Int32/UInt32/Int64/UInt64 retain their ordinary accumulator when safe;
larger bounds select Int64/UInt64 or Int128/UInt128 as needed. An explicit cast,
when needed, stays inside the query expression; the stored column keeps its
original logical dtype. The actual returned accumulator dtype is checked too.

Fixtures combine eight integer lanes, row counts 0/1/256/257/1025, six validity
patterns, bounded/full-width edge values, and single/aligned/mismatched input
chunks. Mismatched A/B chunks use 127/193 rows; aligned chunks use 256. The
validity cases include independent A/B nulls, all-null A and all-null B. They
distinguish no selected rows, selected rows with no valid B, and COUNT(B)
different from selected rows. Nine threshold bindings per fixture include
repeats and domain endpoints. These are deliberate functional combinations,
not a natural workload distribution or a count of unique source tables.

## Reproduction

On Linux AArch64, from the repository root (prefix with `orb -m ubuntu` on Mac):

```sh
python3 workbench/spikes/frame-phase-2/instruments/systems/polars/run.py --bootstrap
```

Bootstrap installs the pinned Rust toolchain only under
`build/frame-helper/polars/{rustup,cargo}`. It changes no global toolchain,
shell profile or environment file. The runner verifies the official source
archives and extracted files, builds with this directory's own `Cargo.lock`
and `--locked --release`, executes the native binary, then rechecks inputs.
Existing build output is reused incrementally. Logs, effective dependency graph,
plans and executable stay under ignored `build/frame-helper/polars`.

## Native qualification

[The compact receipt](qualification.json) records 12,960 passing queries and
38,880 returned scalar checks on native Linux AArch64. Coverage includes 6,840
zero-selected-row calls, 1,476 calls selecting only null B, 2,844 calls where
COUNT(B) differs from selected rows, and 4,320 calls over multichunk inputs.
Both empty and all-null SUM return an integer zero with the expected dtype.
There are 11,340 natural-accumulator queries and 1,620 explicitly widened ones:

| Actual SUM dtype | Queries |
| --- | ---: |
| Int32 / UInt32 | 1,215 each |
| Int64 | 8,100 |
| UInt64 | 1,620 |
| Int128 / UInt128 | 405 each |

Representative optimized plans for all eight input dtypes retain the actual
filter and aggregate expressions. The linked executable retains Polars memory
engine filter/projection and aggregate bodies. Those are execution-boundary
checks, not instruction-cost or throughput measurements.

All 3,369 Polars and 92 optional patch files match their official pinned
archives before and after qualification. The leaf lock resolves 360 packages;
its 335 registry identities match upstream's lock. The release build actually
compiles 124 registry packages, whose archives also match the leaf checksums.
The effective compiled graph is retained separately from Cargo metadata's
broader resolution feature union. The native engine artifacts all report
optimization level 3; actual fingerprints confirm the explicit target flags.

Selected native results, plans, compiled graph and full command/source receipt
have a [verified recovery reference](evidence/artifact.json). Routine compiler
output, toolchain/cache and rebuildable binary remain ignored. This is an
optimized functional check, with no sanitizer, native x86, floating SUM,
cache-residence, zero-copy import or speed qualification. Parent integration
and subsequent Frame comparisons remain with front A.
