# Engine

The database core: the main SQL query interface, planning, execution, and core
data structures. Multi-shard transactions are in scope.

Successor to Calico's `engine` and `foyer`, with database-structural concerns
from `arbor`; see the [Calico reference map](../workbench/notebook/calico.md).

## Provisional scope and seams

- SQL semantics, catalog/schema, planning and execution. Retained plans and
  inspectable compositions can support improvement as evidence accumulates.
- Core data structures: logical keys and identity, routing, record membership,
  columns, indexes and the mapping to actual representations.
- Database access and effect scopes, including atomic visibility of data,
  structural changes and required summary/index effects across shards.
- Bulk ingestion, transformation and result production using the same database
  semantics as other operations.

Engine composes [Ikea](../ikea/README.md) parts and binds their work through
[Loom](../loom/README.md). The current [Orbital design](../orbital/BRIEF.md) supplies
shared transaction and recovery semantics; Engine defines their database meaning.
[Shore](../shore/README.md) supplies external protocols and formats. Representation
schemas and concrete integration interfaces remain open.

The [layout-analyser investigation](../workbench/spikes/layout-analyser/README.md)
explores workload objectives, plane organization and hardware fitting, with
reusable training separated from selection and local fitting.

The [trie-remapping investigation](../workbench/spikes/trie-remapping/README.md),
[aggregate maintenance](../workbench/spikes/aggregate-maintenance/README.md)
and [Ikea composition](../workbench/spikes/ikea-composition/README.md) inform
these choices without selecting a complete engine. No database implementation
is present here yet.
