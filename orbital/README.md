<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../workbench/design/identity/orbital-reversed.svg">
  <img src="../workbench/design/identity/orbital.svg" width="96" height="96" alt="">
</picture>

# Orbital

Durable objects, deterministic execution and transactional recovery, backed by
memory, transport and other OS-like services.

Successor to Calico's `xmem` and `omachine`; see the
[Calico reference map](../workbench/notebook/calico.md).

The [design brief](BRIEF.md) explains durable objects, deterministic execution,
transactions, dissemination and recovery. The [physical brief](PHYSICAL.md)
develops memory views, protection and OS services: Orbital provides the machinery,
[Loom](../loom/README.md) directs its use, and [Engine](../engine/README.md)
supplies application meaning and legal plans. These are evolving designs;
no runtime services are implemented here yet.

Supporting research and evidence:

| Area | Study |
| --- | --- |
| Executable models, fault experiments and reference simulation | [Maintained simulator](../workbench/simulator/README.md) |
| Formal concepts, composition and executable evidence | [Models and evidence](spec/README.md) |
| Transaction and ELT workload repertoire | [Worked situations](../workbench/notebook/transactions/README.md) |
| Relaying, work placement and delivery | [Research survey](../workbench/notebook/dissemination/README.md) |
| Real Linux mapping and protection probes | [Object mappings](../workbench/spikes/orbital-objects/README.md) |
| Distributed applications, exchanges and reusable state | [Dataflow workloads](../workbench/notebook/dataflow-workloads.md) |

The [mining list](MINING.md) collects ideas worth revisiting. The
[retirement record](../workbench/notebook/retired-spikes.md#earlier-orbital-drafts)
identifies earlier drafts and prototype recovery. These are reference material,
not additional requirements.
