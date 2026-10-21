# Engine

The database core: a small set of high-level intermediate representation (IR)
dialects, their conversion into structured plans, execution, and core data
structures. Multi-shard transactions are in scope.

Successor to Calico's `engine` and `foyer`, with database-structural concerns
from `arbor`; see the [Calico reference map](../workbench/notebook/calico.md).

## Native material support

The [`material` adapter](include/engine/material.h) binds native material to
Orbital using a 24-byte Control and retained immutable geometry and
interpretation. Captures and exported Parts keep their selected layout alive;
retained sources can be recaptured without retaining a chain of predecessor
representations. The caller supplies the Store and metadata resource and follows
the adapter's Scope, resource and owner-Lane lifetime requirements.

Build `engine_material_check` with the [pinned toolchain](../BUILDING.md), then
run its same-named CTest entry for material lifetime and recapture checks.

## Directory and Segment experiments

The native [`directory` increment](include/engine/directory.h) implements
physical-prefix lookup and scalar/batched existing-member replacement, using
direct child ownership and one root capture. The caller joins finished Segment
and directory changes in one Orbital packet. Logical Boundlist routing,
membership/balance, ordinary/hash recipes, extras and schema remain unfinished.

The opt-in `SIXDB_ENGINE_DIRECTORY_DRIVER` builds a moving-Part relocation
driver and a multi-row Segment caller with real five-layer material, snapshot
correct afterimages and exact aggregate checks. Native LMDB is required only
for those drivers. The [implementation and measurements](../workbench/spikes/index-directories/IMPLEMENTATION.md)
describe their boundaries, byte ownership and unresolved performance costs.
These experiments do not establish a complete Engine or persistent format.
Source frontend work remains planning-only.

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

Engine owns the contract of its accepted IRs: their semantic model and supported
capabilities. Shore takes on source adaptation and interaction so Engine can stay
focused. Frontend
compatibility choices do not by themselves impose new core semantics, planning
constraints or execution requirements. Frontend use cases can motivate changes;
those changes remain Engine design decisions.

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
the wider Engine design; the broader database and frontend interfaces remain open.
