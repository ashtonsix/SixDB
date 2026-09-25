<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../workbench/design/identity/orbital-reversed.svg">
  <img src="../workbench/design/identity/orbital.svg" width="96" height="96" alt="">
</picture>

# Orbital

The OS-like layer underneath SixDB. Owns data durability, network transport,
thread spawning, and related system services.

Successor to Calico's `xmem` and `omachine`; see the
[Calico reference map](../workbench/notebook/calico.md).

The evolving [brief](BRIEF.md) sets the current direction. The
[scenario workbench](../workbench/spikes/orbital-scenarios/README.md) is an
interactive contention model for preparation, retained protection and yielding,
with explicit assumptions and replayable counterexamples.

## Provisional scope and seams

- Storage services for durable bytes, recovery and the lifetimes of backing
  objects. Versioning, replication and their guarantees need concrete contracts.
- Transport and I/O submission, completion, failure and cancellation mechanics.
- Thread lifecycle and other machine services used by the execution runtime.

[Loom](../loom/README.md) binds these services to database task scheduling and
the buffer pool. The working boundary gives [Engine](../engine/README.md)
authority over database meaning and the unit of coordinated publication, while
Orbital supplies the durability mechanisms. The placement of transaction and
recovery adapters remains open.

Distributed and durable operation come first; local and other operating modes
also need explicit guarantees. Backends, recovery/replication design and the
meaning of successful completion will develop through
[Workbench](../workbench/README.md). No services are implemented here yet.
