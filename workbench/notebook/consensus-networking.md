# Networking around the consensus obligation

2026-09-19. Retired the synthetic network-propagation spike at Ashton's request.
Its all-recipient delivery objective was insufficiently connected to consensus
to justify maintaining the simulator, optimiser and UI. Revisit the question
when the protocol's completion and failure requirements are clearer; this note
does not choose a protocol or bind the evolving Orbital drafts.

The useful question is how to minimise commit latency and network cost while
satisfying the actual consensus and durability obligations. Which recipients
must receive which bytes, what must be durable before acknowledging, and what
evidence lets the caller return? Those dependencies determine the critical path
and which propagation can continue afterward.

Useful lessons from the discarded model:

- Equal-price forwarding chains can add avoidable delay. Reducing depth can
  also concentrate sending work on a shared NIC; evaluate both dependencies
  and resource demand.
- Bursty arrivals can produce transmission backlog at modest average load.
  Follow actual completion dependencies across objects, queues and chunks;
  adding per-hop percentiles or overlapping CPU/network work is misleading.
- Failure injection alone does not make route selection failure-aware.
  Repair and rewriting need observable evidence, possession information and
  resource accounting; eager repair can amplify congestion.
- Refused and unfinished obligations must remain visible when comparing
  latency and throughput. Completing a selected subset can hide exhaustion.

Promising mechanisms to investigate in the eventual protocol include overlap
between propagation and durable writes, choosing quorum paths, hosts and transport
flows across AZs,
distributing forwarding work across hosts or chunk routes, scheduling competing
commits, and adapting to uncertain capacity or missing progress. Establish
which work may legally overlap before optimising it. These are questions,
not a roadmap or an inherited architecture.

Compare matched payloads, placement, offered load and completion definitions.
The discarded model's 5.322 ms p99 meant synthetic 1 MiB delivery to eight
recipients across three modeled AZs, with assumed 10 Gbit/s resources and burst
contention. It was neither a durable commit result nor a comparison with xmem's
roughly 2 ms commits. Its finite synthetic tails establish no production SLA,
optimal policy or calibrated latency floor.

The measured [CFT commit latency work](../spikes/cft-commit-latency/README.md),
including PLP storage and network experiments, remains independently useful.
Use its observations with their workload and measurement boundaries intact.

## Recovering the old experiment

The active spike and its generated reports were removed. Historical authored
explanations and the latest source remain in Git commit `68b8733`, under
`workbench/spikes/network-propagation/`. Full results and source snapshots are
also in the verified [propagation archive](consensus-networking/propagation-artifact.json)
and [tail diagnostic archive](consensus-networking/tail-artifact.json).
Exact-member recovery of inputs, results and source was checked before removal.

Use the [artifact helper](../tools/artifacts.md) with either reference and a
fresh output directory, for example:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/notebook/consensus-networking/propagation-artifact.json \
  build/recovered/network-propagation
```

The propagation archive contains the complete campaign; the tail archive adds
passive auditing, completion-dependency traces and their renderer. Recovery is
for historical inspection, not a recommendation to resume the old optimiser.
