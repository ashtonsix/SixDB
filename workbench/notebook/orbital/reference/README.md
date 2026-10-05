<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../../../design/identity/orbital-reversed.svg">
  <img src="../../../design/identity/orbital.svg" width="96" height="96" alt="">
</picture>

# Historical Orbital reference material

Durable objects, deterministic execution and transactional recovery, backed by
memory, transport and other OS-like services.

Successor to Calico's `xmem` and `omachine`; see the
[Calico reference map](../../calico.md).

**[Design workspace](../ASHTON-SCRATCH.md)** — Ashton's outline and open space for reasoning
about the relationships. Agents contribute there only at his explicit request.

This page organises earlier accounts and research under his five areas.
The briefs mix user direction with researched proposals; the walkthrough's
implementation divisions remain exploratory. Orbital has research models and
native prototypes, but no production runtime.

## Object

- **Briefs:** [durable objects](BRIEF.md#durable-objects),
  [transactions and contention](BRIEF.md#transactions-and-contention),
  [memory views](PHYSICAL.md#memory-views).
- **Experiments:** [views for the embedded profile](../../../spikes/orbital-views/README.md),
  [earlier object/view composition](../../../spikes/orbital-objects/README.md),
  [lock/reservation policies](../../../spikes/orbital-reservation-policy/README.md),
  [prepared retention](../../../spikes/orbital-root-fusion/README.md).
- **Formal checks:** [retained material and recovery](../../../../orbital/spec/MATERIAL.md),
  [views and physical lifetimes](../../../../orbital/spec/RUNTIME.md).

## Allocation

- **Brief:** [allocation and Loom bindings](PHYSICAL.md#loom-bindings-and-allocation).
- **Experiments:** [memory descriptors, arenas and groups](../../../spikes/memory-descriptors/README.md)
  — a guide to geometry, allocation choices, tuning and virtual growth;
  [memory characterisation](../../../spikes/memory-characterisation/README.md)
  — measured hardware behaviour.

## Journal

- **Brief:** [execution](BRIEF.md#execution),
  [persistence and admission](BRIEF.md#persistence-and-admission),
  [failure and recovery](BRIEF.md#failure-and-recovery).
- **Experiments:** [durable commit](../../../spikes/cft-commit-latency/commit/README.md),
  [native transaction model](../../../simulator/models/README.md#transaction-assembly).
- **Formal checks:** [journal authority](../../../../orbital/spec/JOURNAL.md),
  [admission and recovery material](../../../../orbital/spec/MATERIAL.md).

## Port

- **Briefs:** [I/O and IPC](PHYSICAL.md#io-and-ipc),
  [dissemination and adaptation](BRIEF.md#dissemination-and-adaptation).
- **Experiments:** [network and persistence findings](../../../spikes/cft-commit-latency/RESULTS.md);
  its [study guide](../../../spikes/cft-commit-latency/README.md) leads to individual comparisons and evidence.
- **Reading and checks:** [networking notes](../../consensus-networking.md),
  [dissemination survey](../../dissemination/README.md),
  [formal delivery models](../../../../orbital/spec/DELIVERY.md).

## Worker

- **Briefs:** [extensions and publication](BRIEF.md#extensions-and-publication),
  [protection and execution](PHYSICAL.md#protection-and-execution).
- **Experiments:** [local thread handoff](../../../spikes/orbital-local-handoff/README.md).
- **Formal checks:** [execution and extensions](../../../../orbital/spec/EXECUTION.md).
  These are protocol models, not implemented WASM or Firecracker runtimes.

## Across the areas

The [logical brief](BRIEF.md) and [physical brief](PHYSICAL.md) retain their full
accounts. The [earlier architecture walkthrough](ARCHITECTURE.md) is a provisional
cross-cutting proposal. The [buffers and allocation notebook](../../buffers-and-allocation.md)
records the separate cross-module discussion.

Other module entry points: [Ikea](../../../../ikea/README.md), [Loom](../../../../loom/README.md),
[Engine](../../../../engine/README.md), [Shore](../../../../shore/README.md).
For concrete caller context, see [Ikea integration](../../../../ikea/docs/integration.md),
the [transaction repertoire](../../transactions/README.md) and
[dataflow workloads](../../dataflow-workloads.md).

## Evidence and earlier work

- [October 3 handover](handover-2026-10-03.md) — decision history, explicit restarts,
  evidence boundaries and the state left by the two preceding roles.
- [Verification assessment](../../../../orbital/spec/RESULTS.md) — what the retained formal checks establish and leave open.
  The [formal-model guide](../../../../orbital/spec/README.md) covers finding, running and recovering checks.
- [Simulator guide](../../../simulator/README.md) — executable models, authored costs and their narrower coverage.
- [Research and prior-art notes](../../orbital-ideas.md),
  [retired studies and drafts](../../retired-spikes.md) — historical material and recovery pointers.

Individual studies own their measured results, source links and archive references.
