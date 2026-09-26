# Failure and recovery

2026-09-26. Research for [Orbital brief](../../../../orbital/BRIEF.md), coordinated
with Orbital LEAD. The priority is preventing data loss, maintaining writes, then
avoiding waste from overreaction. These are concrete proposals and bounded
counterexamples, not an implemented recovery system or a completed safety proof.

**Ordinary operation stays 2-of-3.** Preserve the first durable follower's early
propagation opportunity after the outgoing network leg and persistence, without
a return hop. The candidate gives that follower the leader's durable acceptance
of the exact prefix, plus its own acceptance. The
[network spike](../../cft-commit-latency/network/README.md) treats RTT/2 only as a
symmetric model; this is not a measured total latency. Routine nonvoting pool
copies stay off that path. Changed predicates or mandatory remote receipts below
are emergency options with explicit activation, exit and preserved obligations.

Normal optimization and recovery share the [same protocol boundaries](HANDOFF.md#keep-submissions-moving-across-route-and-leader-changes):
reuse a prepared ballot across epochs, accept eligible frontier submissions
through any witness, and replay idempotently across routes. Flow changes need no
election. Measured leadership changes within the same configuration require
prefix recovery; membership changes use the terminal handoff. A lagging third
must not become an implicit all-follower acknowledgement gate on send slots.
These ingress, transport and flow-control requirements remain unimplemented.

## The recommended response

An anomalously slow outstanding operation, perhaps the illustrative 100 ms,
starts bounded observer/path/process/persistence probes. Begin useful protective
copying while a quorum still exists; escalate on exposure and corroborating
progress failures. Historical latency alone neither proves destruction nor
changes authority. Health recovery does not immediately trigger migration back.

For replacement or relocation, **pre-copy, choose one terminal handoff entry in
the existing journal, then initialize the named successor from that certified
final prefix**. Its old-quorum certificate binds the inherited history, closes
further old-config admission and fixes the successor. Prefix consensus prevents
splicing incompatible dependent entries. New voters need complete durable state
before their ordinary election/quorum can serve. Logical shard/object identities
remain unchanged. An explicit
handoff pause cannot escape missing history or authority through a timeout.

When health becomes very bad, **SOS broadcast** can be useful even when handoff
is impossible. Pause admission altogether at reachable processes and send any
useful state/evidence through surviving authorized flows. Export does not wait
for a quorum, complete snapshot/manifest, newly admitted epoch or successor grant.
Small identities, accepted records and fragments can help an operator discover
missing or uncertain work even when no bootable recovery image escapes. A local
stop is not proof that hidden authorities stopped; missing evidence is not proof
of data destruction.

| Question | Owning note |
| --- | --- |
| What should an anomaly trigger, and how do we avoid recovery storms? | [Detection and proportionate response](DETECTION.md) |
| How exactly does a replacement or relocated quorum acquire authority? | [Terminal handoff and successor initialization](HANDOFF.md) |
| What do stronger quorums/predicates protect, and what do they fail to protect? | [Quorum comparisons and recovery obligations](QUORUM.md) |
| What can escape when admission and handoff cannot continue? | [SOS broadcast](SOS.md) |
| What cut can PITR restore, and can consumers help during blob outages? | [Replay, cuts and peer recovery](PITR.md) |
| Which concrete histories challenge this response? | [Twenty-six scenarios and primary outage reports](SCENARIOS.md) |

## Consequences that must survive integration

- Payload persistence, witness admission, transaction decisions and visible
  results are distinct guarantees. Preserve accepted-but-unadmitted submissions
  and chosen/possibly chosen history even when the caller's reply was lost.
- A replacement witness alone cannot repair a missing payload, dictionary, key,
  code artifact or staged result. Report protection for new work separately from
  repaired historical coverage. A remote certified prefix does not prove there
  was no later chosen suffix.
- Completed readers can leave observation bounds needed by future transactions.
  Retain them or sufficient monotone position floors. Restore the constraint
  frontier before reopening an affected scope, while allowing old computations
  to remain pending with their blockers and decision owners represented.
- Reject obsolete authority for new actions; validate delayed historical chosen
  evidence under its original configuration. PITR creates a separate lineage and
  needs enforceable promotion fencing and explicit external-effect handling.
- Consumer peers can supply authenticated immutable bytes during blob outages.
  Complete replay/checkpoint recipes include their dependencies; counted durable
  copies need retention obligations. High cache hit rate does not prove recovery.
- Reserve an independently authorized, executable resolution/export path with
  its inputs, keys, code and service capacity. Spare bytes or threads alone do
  not break a metadata/application dependency cycle.

With ordinary 2-of-3 fixed, normal placement and background replication still
choose the unannounced-failure exposure. No detector can retroactively supply
zero-loss regional protection when the region disappears before independent
history and payload copies exist. SOS improves evidence survival without
claiming that protection in advance.

## Costs and evidence

Losing the usually faster follower can worsen tails even while the remaining
pair continues 2-of-3. An emergency remote result gate affects all work within
its policy scope, including disjoint effect envelopes. Every propagating
follower/consumer/independent origin must enforce the applicable gate. Returning
to the ordinary path cannot discard obligations made under the stronger policy.

The [existing pipeline analysis](../PIPELINING.md) concerns a different cost:
remote transaction effect-authority grant, bound and exact-position rounds at
an **assumed** 80 ms RTT give about 240 ms before additional work. Those exchanges
do not require a WAN member in ordinary witness replication. Handoff pause,
repair service, quorum tail, result gating and transaction contention need
separate accounting; none is measured by these recovery notes.

Run the static coverage probe from the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/recovery/probe.py --output build/orbital-recovery/probe.json
```

Its **23 checks** cover three placements, disjoint old/new quorum examples and
complete declared recovery recipes, including distributed cache fragments,
missing dependencies and alternative checkpoints. It assumes exact verified
copies and reachable/authorized surviving holders; partitions can defeat
retrieval even when placement passes. It implements neither consensus nor a
failure detector. Output includes the source hash and stays in ignored storage.

The [handoff probe](handoff_probe.py) adds **30 authored checks**:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/recovery/handoff_probe.py --output build/orbital-recovery/handoff-probe.json
```

They cover whole-prefix selection versus an incompatible per-slot splice,
partial/late terminal choice, exact early-certificate coverage,
terminal-certificate/initialization boundaries and the lost completed-reader-bound
cycle. Reports, votes and chosen certificates are assumed facts; the probe checks
their use, not the network/storage protocol that establishes them. SOS interruption histories are
documented scenarios, not an implemented exporter experiment.

The handoff note selects the narrow sequence-consensus/learning primitives it
needs and spells out interrupted replacement and relocation. Completing their
implementation, prefix synchronization over the actual transport, checkpoint
certification and durable initialization remain protocol work. Detector
thresholds and pool sizing require actual tail/progress and recovery-load data.
No TLA+ model or production timing guarantee is claimed.
