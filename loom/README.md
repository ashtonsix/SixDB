# Loom

Loom connects database work to [Orbital](../orbital/README.md): ordinary calls
over ready bytes, typed Rings for independent memory dependencies, and Tasks for
work that suspends. [Engine](../engine/README.md) owns database semantics and
decides which work is legal, [Shore](../shore/README.md) owns external adapters,
and [Ikea](../ikea/README.md) owns the formats and native bodies. A kernel stage
need not become a Task.

Loom owns execution mechanisms and runtime policy: scheduling, bounded service,
memory-dependency rotation, hardware/pressure-aware hints and execution-cost
feedback. It receives caller-prepared work and qualified byte demands. Container
algorithms, storage-policy adapters, format interpretation and operation lifetime
remain with the consumer. Loom calls Orbital's physical services; its public
runtime does not depend on Ikea or export consumer containers. Integration
examples demonstrate callers of these services without becoming runtime APIs.

## Choose the execution path

| Operation | Start with | Maintained example or guide |
| --- | --- | --- |
| Ready values and a small filter or reduction | An ordinary inlined Ikea body; fuse cheap adjacent work | [Ordinary filters](../ikea/examples/filters/ordinary.cpp) |
| An existing synchronous pipeline with substantial reusable stages | Share a canonical native consumer where the complete operation justifies it | [Shared filter stages](../ikea/examples/filters/composition.cpp), [composition guidance](docs/ring.md#compose-ready-work-before-rotating) |
| Independent lookups whose keys are already known | Use the shared lookahead loop synchronously, or in bounded Ring visits when other work needs service | [Stream guide](docs/stream.md), [mixed example](examples/stream.cpp) |
| Independent lookups with a next address discovered from prior data | Prefetch the qualified dependency, then rotate a memory Ring | [Dependent lookups](examples/hints.cpp), [Ring guide](docs/ring.md) |
| Prepared hot/cold owner entries with periodic owner maintenance | Confirm useful direct batches in cold; hot runs without observation | [Cold promotion](docs/execution.md#cold-promotion-without-hot-path-observation) |
| A consumer whose direct/Ring crossover changes with recent work | An owner-held execution Policy at the operation boundary | [Execution guide](docs/execution.md), [complete example](examples/execution.cpp) |
| Allocation, I/O or producer work that must wait | A stable Task frame, with admitted resources and explicit completion | [Allocation](examples/allocation.cpp), [partition work](examples/partition.cpp) |

Read the [usage guide](docs/usage.md) for setup and steady-state work, binding
and lifetime, batching, readiness and performance comparisons. It links to the
mechanism guides rather than requiring contracts to be recovered from spikes.

## Allocation stage

An embedded [Worker](include/loom/worker.h) owns a stable Lane, allocation Pool
and bounded Task queue. Zero Task capacity still permits ordinary native work,
Rings and physical Message service. A synchronous owner borrow needs stable
Allocation, contents and bindings through every use; escaping work retains its
physical use separately. Holds preserve an address, not an immutable version.
The [binding and lifetime rules](docs/usage.md#binding-and-lifetime) cover both.

## Tasks and completion

Task admission, logical completion, detach and physical drain are separate.
Each step bounds its work; poll budgets count visits, not elapsed time.
The [completion guide](docs/usage.md#tasks-and-physical-completion) explains fresh
frames, Signal waits, Message credit, shutdown and retained backend records.

## Memory Rings

Configure a stable typed bank once and reuse its bounded active/overflow
inventory. [Native continuations](docs/ring.md) cover `musttail`, argument traffic,
common prefixes, mixed operations, cohorts and cleanup. Borrowed inputs and
outputs stay qualified until their last use; close the bank before Pool shutdown.

Callers supply abstract [memory demands](include/loom/hints.h); Loom maps admitted
classes through a hardware/pressure snapshot.
[Hardware usage](docs/hardware.md#bind-demand-and-adopt-policy) explains safe
windows, startup defaults/calibration and returned-boundary refresh. Neither a
hint nor a width candidate establishes readiness or cache residence.

## Experimental Object callers

The [selected ObjectStore caller](../workbench/spikes/orbital-mvcc/callers/object_module.md)
exercises coherent reads across suspension, publication and escaping completion
using an actual Worker. The [MVCC study](../workbench/spikes/orbital-mvcc/README.md)
also retains current production integration checks and historical findings.
The caller's policy remains experimental; the [Object module](../orbital/README.md#embedded-objects)
owns the current interpretation and physical-ownership seams.
Placement, the version-aware read cache and I/O orchestration remain in the
[joint design](../workbench/design/orbital/DESIGN.md). The historical
[minimum viable Loom study](../workbench/spikes/loom-execution/FINDINGS.md#minimum-viable-loom-recommendation)
informs the runtime without establishing its contracts.

## Build and examples

Use the pinned Linux toolchain in [Building](../BUILDING.md). `loom_validate`
builds and runs Worker, Ring, hardware, execution-policy and allocation checks.
The examples include admission, useful work and complete cleanup:

- [Allocation](examples/allocation.cpp): wait for a foreign reader, grow and rebind.
- [Partition work](examples/partition.cpp): reserve output and settle reads/ACK credit after detach.
- [Capacity](examples/capacity.cpp): grant finite equal-shape clients storage under contention.
- [Preparation](examples/preparation.cpp): bound placed pending/ready inventory and settle stockout/failure.
- [Hints](examples/hints.cpp): dependent chains during startup, with returned-boundary policy adoption.
- [Streams](examples/stream.cpp): bounded known-key lookahead and a dependent chain sharing one Ring.
- [Execution](examples/execution.cpp): charged serial probes and legal direct/Ring selection.
- [Hardware inspection](examples/hardware.cpp): reported facts and conditioned observations; experiments off by default.

The allocation/MVCC timing drivers are [retired](../workbench/benchmarks/retired-drivers.md).
[Retained Object checks](../workbench/benchmarks/retired-drivers.md#current-integration-checks)
exercise real formats and Loom lifetimes; the earlier complete index comparisons
are [archived](../workbench/notebook/retired-spikes.md#index-directory-spikes--october-2026).
The [execution research route](../workbench/notebook/execution-and-codegen.md)
connects those consumers to memory overlap, codegen and earlier experiments.
Optional `loom_ring_probe` and
`loom_hold_probe` are [dispatch](test/ring_probe.cpp) and
[Hold acquisition](test/hold_probe.cpp) diagnostics; their timings alone establish
neither database throughput nor request latency.
