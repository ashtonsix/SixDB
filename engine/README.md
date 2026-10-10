# Engine

The database core: a small set of high-level intermediate representation (IR)
dialects, their conversion into structured plans, execution, and core data
structures. Multi-shard transactions are in scope.

Successor to Calico's `engine` and `foyer`, with database-structural concerns
from `arbor`; see the [Calico reference map](../workbench/notebook/calico.md).

## Provisional scope and seams

- Definition and semantics of the accepted high-level IR dialects, structured
  plan construction, optimization and execution. Retained plans and inspectable
  compositions can support improvement as evidence accumulates.
- Catalog/schema and the database meaning of operations represented in those IRs.
- Core data structures: logical keys and identity, routing, record membership,
  columns, indexes and the mapping to actual representations.
- Database access and effect scopes, including atomic visibility of data,
  structural changes and required summary/index effects across shards.
- Bulk ingestion, transformation and result production using the same database
  semantics as other operations.

Engine composes [Ikea](../ikea/README.md) parts and binds their work through
[Loom](../loom/README.md). Orbital's embedded runtime supplies local versioning;
its wider transaction and recovery protocol remains proposed. Engine defines
their database meaning. The [earlier Orbital brief](../workbench/notebook/orbital/reference/BRIEF.md)
records that broader protocol investigation.
[Shore](../shore/README.md) owns source dialects, parsing and initial abstract
syntax trees (ASTs), and lowers supported sources into Engine's accepted IRs.
It also supplies external protocols and formats. Several source dialects can
share an Engine IR; Engine need not accept each source AST directly.
The IR family, source-to-IR binding and validation seams, representation schemas
and concrete integration interfaces remain open.

[Orbital Stage 2 outcomes](../workbench/spikes/orbital-mvcc/OUTCOMES.md)
retain the embedded MVCC and real-codec experiments, temporary caller assumptions,
and lessons about schema reuse, copy grain, retention and independent work.
They inform this design without supplying an Engine or multi-block container.

The [layout-analyser investigation](../workbench/spikes/layout-analyser/README.md)
explores workload objectives, plane organization and hardware fitting, with
reusable training separated from selection and local fitting.

The [trie-remapping investigation](../workbench/spikes/trie-remapping/README.md),
[aggregate maintenance](../workbench/spikes/aggregate-maintenance/README.md)
and [Ikea composition](../workbench/spikes/ikea-composition/README.md) inform
these choices without selecting a complete engine. No database implementation
is present here yet.
