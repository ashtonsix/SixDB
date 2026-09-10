# Shore

Binds SixDB to the external world through user-facing tools, programmatic
interfaces and data integrations.

Successor to `~/calico-ui/`, with a broader integration remit.

## Provisional scope and seams

- UI, interactive shell, SDKs and language bindings for operating and querying
  SixDB, plus the external protocols they use.
- Connectors to external systems and object stores, including S3, for ingestion,
  export and ELT workflows.
- Arrow, Parquet and other format adapters; batch/stream interchange and
  utilities for exporting or restoring database contents.

[Engine](../engine/README.md) owns query meaning, schema and transaction
outcomes. Shore translates external representations and exposes those outcomes
to callers. The working aim is reusable input/output paths with explicit
lifetimes, backpressure and partial-progress reporting. Storage formats need
not dictate the external interchange format.

Protocols, compatibility targets, serialization envelopes and the division of
streaming adapters remain open. They will develop through
[Workbench](../workbench/README.md) and concrete consumers; no interface is
implemented here yet.
