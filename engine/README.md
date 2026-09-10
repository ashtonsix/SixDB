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
- Transactional reads and mutations, including coordinated visibility of data,
  structural changes and required summary/index effects across shards.
- Bulk ingestion, transformation and result production using the same database
  semantics as other operations.

Engine composes [Ikea](../ikea/README.md) parts and binds their work through
[Loom](../loom/README.md). [Shore](../shore/README.md) supplies external protocols
and formats. This division is a working proposal: representation schemas,
publication/recovery protocols and concrete integration interfaces remain open.

The [trie-remapping investigation](../workbench/spikes/trie-remapping/README.md),
[aggregate maintenance](../workbench/spikes/aggregate-maintenance/README.md)
and [Ikea composition](../workbench/spikes/ikea-composition/README.md) inform
these choices without selecting a complete engine. No database implementation
is present here yet.
