<picture>
  <source media="(prefers-color-scheme: dark)" srcset="workbench/design/identity/sixdb-reversed.svg">
  <img src="workbench/design/identity/sixdb.svg" width="112" height="84" alt="">
</picture>

# SixDB

SixDB is Calico's successor: a SQL database aiming for HTAP dominance with
strong ELT. Its value should come from collapsing the infrastructure needed
for mixed transactional and analytical workloads. The endpoint is a production
database.

C++ is the primary implementation language. SixDB is Linux-first,
distributed-first, AMD/Zen5-first, and durable-first. **First matters:** a wide
array of operating modes must be supported. Multi-shard transactions are in
scope. This is a starting description, not an exhaustive list of constraints.

| Module | Scope |
| --- | --- |
| [ikea](ikea/README.md) | Composition-ready containers and kernel stages |
| [orbital](orbital/README.md) | Durability, network transport, thread spawning, and other OS-like services |
| [loom](loom/README.md) | Binding to Orbital, task scheduling/routing, latency hiding, and the buffer pool |
| [engine](engine/README.md) | Database core: SQL interface, planning, execution, and core data structures |
| [shore](shore/README.md) | UI, shell, bindings, connectors, formats, and external utilities |
| [workbench](workbench/README.md) | Science, design development, spikes, benchmarks, datasets, and tooling |

Design develops through experiments; priorities and architecture follow the signal.
The [Workbench guide](workbench/README.md) is the starting point for research,
datasets, and experiment tools. [Calico](workbench/notebook/calico.md) is prior
work to recalibrate, not an inherited specification.

See [Building](BUILDING.md) for the shared CMake/Ninja build and independent,
incremental TU compilation, and [AGENTS.md](AGENTS.md) for agent working guidance.
