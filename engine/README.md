# Engine

The database core: a small set of high-level intermediate representation (IR)
dialects, their conversion into structured plans, execution, and core data
structures. Multi-shard transactions are in scope.

Successor to Calico's `engine` and `foyer`, with database-structural concerns
from `arbor`; see the [Calico reference map](../workbench/notebook/calico.md).

## Segment directory

[`engine::directory`](include/engine/directory.h) locates an index's Segments.
Each of the five layers is a tree of Orbital Objects ordered by physical
prefix:
- **Leaves** hold the prefix plane, 64-byte dirents in Parts of 16, and the
  min/max logical fences. Fences exist only above the shared prefix J.
- **Interior rows** hold a physical separator, a logical separator above J, and
  the child's name.
- **The root keeps a fixed name.** The tree deepens instead of growing one flat
  root.
- **L0 and L1 are single leaves.**

Bulk build leaves 25% slack, the rightmost node splits off build-occupancy
pieces so appends stay dense, separators are loose, and only shrinking nodes
merge. Each `apply` publishes one packet. Unchanged Parts are reused from the
basis version, and identical replacements publish nothing. Readers borrow
Parts under their own `Store::Scope`.

Ordered searches over separators, prefixes and fences use
[Intervals](../ikea/docs/intervals/usage.md) endpoint searches; K-byte logical
keys are a custom endpoint source. The directory owns its Orbital material: a
Control whose Part extents follow from row count and key width. The [segment-directory spike](../workbench/spikes/segment-directory/README.md)
holds the evidence, the rejected alternatives and the open questions. Those
alternatives are flat roots, natural partitions, inline-dirent hash pages and
a prefix-to-leaf hash.

Build and run `engine_directory_check` with the [pinned toolchain](../BUILDING.md).
It runs 40 layer shapes against an ordered oracle through real Objects. The
shapes cover single and batched edits, draining and regrowth, readers holding
older Scopes, no-op publication, and orphaned-Object detection. Additional
9/12/31/255-byte key cases differ only after the first eight bytes, with equal
fences crossing node boundaries and an old Scope retaining the first duplicate
after its deletion from the current version.

Not yet settled: durable allocation of node names (a Layer counts in memory),
direct-slot indexing for L0/L1, and timing the edit path with real callers.
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
