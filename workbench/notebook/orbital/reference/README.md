<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../../../design/identity/orbital-reversed.svg">
  <img src="../../../design/identity/orbital.svg" width="96" height="96" alt="">
</picture>

# Historical Orbital reference material

For implemented allocation and worker services, start with the
[Orbital runtime](../../../../orbital/README.md). The
[combined design](../../../design/orbital/DESIGN.md) keeps the wider candidate in view.

**[Design workspace](../ASHTON-SCRATCH.md)** — Ashton's outline and open space for reasoning
about the relationships. Agents contribute there only at his explicit request.

This page organises earlier accounts and research under his five areas.
The briefs cover durable objects, deterministic execution and recovery. They mix
user direction with proposals; the walkthrough's divisions remain exploratory.
Prototype layouts, names and stronger lifetime/growth rules do not establish
current runtime contracts. Formal models establish only their stated assumptions.
[Calico's `xmem` and `omachine`](../../calico.md) are prior art, not inherited designs.

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
  — measured hardware behaviour;
  [reuse selection](../../../spikes/allocation-reuse-temperature/FINDINGS.md)
  — selection now omits temperature/CPU ranking; temperature still guides cold-first
  reclamation. Earlier cascades and candidate caches remain rejected.

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
- **Experiments:** [local thread handoff](../../../spikes/orbital-local-handoff/README.md),
  [call boundaries](../../../spikes/call-boundaries/README.md) and
  [hardware hint policy](../../../spikes/loom-hardware-policy/README.md).
  [Loom's guide](../../../../loom/README.md) owns the implemented Task and hint interfaces.
- **Formal checks:** [execution and extensions](../../../../orbital/spec/EXECUTION.md).
  These are protocol models, not implemented WASM or Firecracker runtimes.

## Across the areas

The [logical brief](BRIEF.md), [physical brief](PHYSICAL.md) and
[earlier architecture walkthrough](ARCHITECTURE.md) retain their full accounts.
Their useful distinctions carry into the current design: input storage,
publication and physical completion are separate; native use needs both stable
bytes and valid interpretation; physical retention supplies no immutability,
version or access permission. The [buffers and allocation notebook](../../buffers-and-allocation.md)
records the earlier cross-module discussion.

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
