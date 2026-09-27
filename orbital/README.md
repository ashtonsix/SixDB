<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../workbench/design/identity/orbital-reversed.svg">
  <img src="../workbench/design/identity/orbital.svg" width="96" height="96" alt="">
</picture>

# Orbital

The OS-like layer underneath SixDB. Owns data durability, network transport,
thread spawning, and related system services.

Successor to Calico's `xmem` and `omachine`; see the
[Calico reference map](../workbench/notebook/calico.md).

The [design brief](BRIEF.md) explains durable objects, deterministic execution,
transactions, dissemination and recovery. The [physical brief](PHYSICAL.md)
develops memory views, protection and OS services: Orbital provides the machinery,
[Loom](../loom/README.md) directs its use, and [Engine](../engine/README.md)
supplies application meaning and legal plans. These are evolving designs;
no runtime services are implemented here yet.

The supporting studies retain alternatives, worked cases and experimental
evidence at greater depth:

| Area | Study |
| --- | --- |
| Executable models, fault experiments and reference simulation | [Maintained simulator](../workbench/simulator/README.md) |
| Contention, transactions and recovery | [Scenario workbench](../workbench/spikes/orbital-scenarios/README.md) |
| Relaying, work placement and delivery | [Dissemination](../workbench/spikes/orbital-dissemination/README.md) |
| Object views, OS bindings and mapping prototypes | [Objects and physical services](../workbench/spikes/orbital-objects/README.md) |
| Distributed extension applications, exchanges and reusable state | [Dataflow](../workbench/spikes/orbital-dataflow/README.md) |

The [mining list](MINING.md) collects ideas worth revisiting, with a
[source map](stale-drafts/README.md) for older drafts and a
[networking catalog](../workbench/spikes/orbital-dissemination/CATALOG.md).
These are reference material, not additional requirements.
