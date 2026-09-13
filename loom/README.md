# Loom

Binds Orbital to SixDB proper. Schedules tasks, routes them to the right
threads, and interleaves work while requested bytes arrive from disk, network,
DRAM, or other sources. Owns the buffer pool.

Successor to Calico's `loom` and parts of `arbor`; see the
[Calico reference map](../workbench/notebook/calico.md).

## Provisional scope and seams

- Task placement, queues and progress: run ready work directly, and interleave
  useful work around dependent accesses and waits.
- Buffer-pool partitions, residency and byte-access leases, with admission and
  resource budgets for inputs, outputs and retained work.
- Drivers for prefetching, suspension, resumption and cancellation, using
  [Orbital](../orbital/README.md) for threads, transport and durable storage.

The working division gives [Engine](../engine/README.md) database routing,
identity and structural mutation, and Loom task routing and resource mechanics.
Arbor's ancestry does not settle those boundaries. Bindings must preserve both
the intended data version and the shorter lifetime of its resident addresses.
[Ikea](../ikea/README.md) supplies local access requirements and computational
work; a kernel stage need not be a scheduler task.

Queue policy, partition geometry, ownership across waits and the concrete
acquisition interface remain open. The
[integration proposal](../workbench/spikes/ikea-composition/semantics-and-integration.md#loom-resources-progress-and-retained-state)
develops some candidate seams. Initial
[objectives and architecture questions](../workbench/notebook/loom-objectives-and-architecture.md)
explore mixed-workload goals, locality, resource coupling and analyser placement
without selecting a design. No runtime is implemented here yet.
