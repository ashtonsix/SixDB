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
| [workbench](workbench/README.md) | Science, design development, prototypes, benchmarks, datasets, and tooling |

Design develops in Workbench through experiments and spikes. Module documents
stay thin, brief, and easy to steer. Experiment priorities and architectural
choices will follow the signal; no implementation roadmap is fixed here.

One repository and a shared [CMake/Ninja build](BUILDING.md) support independent,
incremental compilation of TUs, including opt-in prototypes. There are no
database implementation targets yet.

[Calico references](workbench/prior-art/calico.md) preserve useful starting
points. Calico's architecture and specifications do not automatically become
SixDB's constraints.
