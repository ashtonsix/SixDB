# Native system controls

Front A owns these private query controls, not an adapter for Engine.
The [phase-1 storage comparison](../../../frame-phase-1/encoding/duckdb_storage.py)
measures DuckDB checkpoint codecs and capacity; it is not a native query driver.
The [complete scan comparison](../../../frame-phase-1/scan/FINDINGS.md) preserves
actual Python Polars calls and native narrow-array controls. We reuse its exact
arithmetic, actual logical dtypes and conditioning distinctions, while adding
native system-call boundaries instead of attributing Python overhead to codecs.

The [native Rust Polars control](polars/README.md) now qualifies actual optimized
LazyFrame filter-to-SUM/COUNT/len calls over owned typed columns. Its 12,960
queries cover narrow/wide, NULL, empty and mismatched chunks, with independently
bounded natural accumulators and explicit widening when required. This is a
functional native AArch64 control, not a speed result or reconstruction of the
historical Python wheel's effective build.

The first DuckDB control uses the normal C query/prepared API over a normally
loaded typed in-memory table. Setup appends typed data chunks; queries consume
the table after the input chunks and appender are destroyed. An independent
signed-128-bit oracle checks SUM and both COUNT laws, including NULLs, no
matches, empty tables, changed parameters, full and partial 2,048-row chunks.
This is a functional control, not throughput or compressed-storage evidence.

The [native local receipt](qualification.json) records 90 passing cases on
Linux AArch64 with a Clang 21.1.8 caller: five row counts (0, 1, 2,048, 2,049,
4,097), nullable/non-nullable inputs and nine threshold bindings. The actual
HUGEINT result is consumed through the stable chunk API. The release library
and caller hashes are recorded. This is neither a sanitized engine build nor
native x86 execution.

The [provider](../../../../peers/duckdb.cmake) pins official DuckDB 2.0.0
release archives using their published SHA-256 digests. The caller compiles with
SixDB's pinned compiler; the engine is the actual prebuilt upstream release.
Caller march/tune flags do not describe its compiler or selected kernel ISA.
The old phase-1 Python DuckDB 1.5.4 and Polars 1.42.1 pins remain historical
controls; future native versions must be identified rather than relabelled.

Enable `SIXDB_FRAME_SYSTEM_CONTROLS` in the root CMake project with the phase-1
and phase-2 spikes, then build `frame_duckdb_check`. An existing release archive
can be passed as `WORKBENCH_DUCKDB_ARCHIVE`; the same digest remains mandatory.
No download occurs unless these optional controls are selected.

DuckDB's stored-table buffers are distinct from original Arrow or appender
input. Flushing those original inputs would not condition the table. A future
caller-buffer source cell can use a native table function and imported shared
vectors, with projection and ownership checked explicitly; it would remain an
external-source cell rather than evidence about compressed DuckDB storage.
The normal stored-table control must retain that residence limitation.
