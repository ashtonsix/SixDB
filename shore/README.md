# Shore

Owns SixDB's source-language front ends and binds the database to the external
world through user-facing tools, programmatic interfaces and data integrations.

Successor to `~/calico-ui/`, with a broader integration remit.

## Provisional scope and seams

- UI, interactive shell, SDKs and language bindings for operating and querying
  SixDB, plus the external protocols they use.
- Source-dialect parsing, initial abstract syntax trees (ASTs) and lowering into
  the high-level intermediate representation (IR) dialects accepted by Engine.
  Shore can support more source dialects than Engine has IR dialects.
- Connectors to external systems and object stores, including S3, for ingestion,
  export and ELT workflows.
- Arrow, Parquet and other format adapters; batch/stream interchange and
  utilities for exporting or restoring database contents.

[Engine](../engine/README.md) defines a small family of high-level IR dialects
and their conversion into structured plans. It owns IR semantics, catalog/schema,
database effects and transaction outcomes. Shore translates source programs into
those IRs and exposes results and outcomes to callers. Source-specific checks,
name/type resolution and the binding seam remain to be worked out together.

Candidate front ends include PostgreSQL-compatible SQL, SixDB-native SQL,
Polars/Pandas-compatible dataframe and pipeline operations, SixDB-native
dataframes, and other dialects worth investing in. Their compatibility scope
and the accepted Engine IRs are open; this list does not claim implemented
support or complete compatibility.

The working aim is reusable input/output paths with explicit lifetimes,
backpressure and partial-progress reporting. Storage formats need not dictate
the external interchange format.

Protocols, compatibility targets, serialization envelopes and the division of
streaming adapters remain open. They will develop through
[Workbench](../workbench/README.md) and concrete consumers; no interface is
implemented here yet.

The [Orbital Stage 2 handover](../workbench/spikes/orbital-mvcc/OUTCOMES.md#what-carries-into-later-composition)
records relevant ownership, completion and cancellation distinctions. It contains
no measured Shore, external I/O or end-to-end database cost.
