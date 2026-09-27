<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../workbench/design/identity/orbital-reversed.svg">
  <img src="../workbench/design/identity/orbital.svg" width="96" height="96" alt="">
</picture>

# Orbital

Durable objects, deterministic execution and transactional recovery, backed by
memory, transport and other OS-like services.

Successor to Calico's `xmem` and `omachine`; see the
[Calico reference map](../workbench/notebook/calico.md).

Start with the [architecture walkthrough](ARCHITECTURE.md): follow a durable
object through a transaction, retained reads, memory access, failure and recovery.
It explains the concepts and provisional implementation boundaries. The
[design brief](BRIEF.md) owns the intended logical guarantees; the
[physical brief](PHYSICAL.md) owns memory, protection and OS-service direction.
[Engine](../engine/README.md) supplies application meaning and legal plans;
[Loom](../loom/README.md) binds resources and schedules work.

## What exists

Orbital has executable research models and native platform probes, but no
production runtime implementation yet. The models have different jobs and scope:

| Material | What it provides |
| --- | --- |
| [Formal models and evidence](spec/README.md) | Finite protocol checks spanning authority, transactions, retention, delivery and physical lifetimes. [Results](spec/RESULTS.md) owns current coverage and unfinished work. |
| [Native simulator](../workbench/simulator/README.md) | Actors, controlled faults and shared finite resources. Its [Orbital models](../workbench/simulator/models/README.md) use a prepared leader/follower path and a local coordinator journal; they do not execute all formally checked recovery mechanisms. |
| [Linux object probes](../workbench/spikes/orbital-objects/README.md) | Real mapping, protection, UFFD and COW experiments; individual mechanisms rather than a composed object service. |

Formal results do not extend simulator coverage implicitly. Simulator service
costs are authored inputs, not measured production performance. Their common
purpose is to expose obligations and counterexamples that an implementation must
address; laboratory state and message formats are not production interfaces.

## Explore further

The [research questions](../workbench/notebook/orbital-ideas.md) point to useful
earlier work without adding requirements. The [transaction repertoire](../workbench/notebook/transactions/README.md),
[dataflow workloads](../workbench/notebook/dataflow-workloads.md) and
[dissemination survey](../workbench/notebook/dissemination/README.md) supply
application situations and prior art. [Retired work](../workbench/notebook/retired-spikes.md)
keeps historical designs and evidence recoverable outside the current reading path.
