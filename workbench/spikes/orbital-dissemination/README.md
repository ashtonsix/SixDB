# Orbital dissemination

Explore the work, traffic, latency and delivery consequences of many-to-many
messaging, including all one/few/many degeneracies. This spike includes writes,
read serving across shards and within shard replicas, extension rings, useful
relay computation and changing membership. Orbital LEAD owns integration into
the [brief](../../../orbital/BRIEF.md); these experiments do not silently select
new protocol semantics.

Start with the [source-linked idea catalog](CATALOG.md) and
[general design space](DESIGN-SPACE.md). The problem is joint work placement,
representation, execution count, release, routing, retention and adaptation.
Trees, follower placement and plan hints are examples within that problem.

| Question | Owning material |
| --- | --- |
| What composed direction is recommended now? | [Provisional recommendation](RECOMMENDATION.md) |
| Which recorded ideas were found, explored and supported by evidence? | [Catalog and survey boundaries](CATALOG.md) |
| What generalizes the individual examples? | [Design space](DESIGN-SPACE.md) |
| What did the comparisons establish? | [Findings](FINDINGS.md) |
| What is modeled, assumed, measured or absent? | [Model and endpoint definitions](MODEL.md) |
| Edmonds, multiple ready origins, tree families, hierarchy, sender choice and aggregation | [Algorithms and counterexamples](ALGORITHMS.md), [routing helpers](routing.py) |
| Read partitions, coherent coverage, reductions and backend extensions | [Reads and extensions](READS-AND-EXTENSIONS.md), [network-backed read cases](read_scenario.py) |
| Message enhancement: selections, reusable computation and representations | [Enhancement comparisons](ENHANCEMENT.md) |
| Edge origins, provider asymmetry, NAT, private links and peer retrieval | [Edge economics](EDGE.md) |
| Route families, sender choices and mixed foreground/background work | [Adaptation](ADAPTATION.md), [mixed traffic](MIXED.md) |
| Recipient failure, duplicate effects, joins and conditional admission | [Delivery](DELIVERY.md), [ledger probe](delivery.py), [network-backed joins](membership_scenario.py) |
| Add a topology, fault or offered-load scenario | [Event/resource core](simulator.py), [workloads](experiments.py) |

## Reproduce or extend

One low-latency write fixture follows Ashton's corrected example: client-producer
and witness leader in AZ A, with the **fast witness follower also serving as
producer follower** in AZ B and the target consumer nearby. The leader is not
a payload holder. The target is **170µs p99 / 250µs p99.9** to a post-persistent
extension effect; the exact effect boundary remains open. This placement prompts
an overlap comparison, not a deployment prescription. The model reports payload
readiness, admitted internal consumer effect and client return separately.

Python standard library only. Run in the repository's Linux environment;
no C++ target or clean build is needed. The checks include exact small-graph
oracles, causal/resource accounting and adversarial delivery histories.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/run_study.py \
  --count 1000 --output build/workbench/orbital-dissemination/selected.json
```

The study records source hashes and refuses to retain a comparison if its source
changes while it runs. It keeps every selected scenario, including failed,
refused and unfinished work. Increase `--count` to expose longer backlog growth;
small synthetic cases are not p99.9 confidence estimates. The selected evidence
and interpretation are linked from [Findings](FINDINGS.md).

A single write scenario accepts JSON via `experiments.py --config INPUT --output
OUTPUT`. For example:

```json
{
  "kind": "writes",
  "count": 2000,
  "rate": 100000,
  "size": 4096,
  "admission": "strict",
  "shards": 16,
  "shared_witnesses": true,
  "consumers": 8,
  "policy": "balanced",
  "queue_bytes": 65536,
  "batch_bytes": 1200,
  "batch_wait_us": 4,
  "faults": [
    {"kind": "cpu_slow", "node": "f0", "at_us": 5000, "until_us": 8000, "factor": 10}
  ]
}
```

Rates are total offered operations/second across the configured shards. `burst`
groups scheduled arrivals without resetting their original timestamps. `size`,
`rate`, `queue_bytes`, `nic_bytes_us`, `cpu_packet_us`, `persist_us`,
`persist_bytes_us`, `same_az_us`, `cross_az_us`, `public_bytes_us`, batching and
faults can vary independently. Call `Network` directly to supply an arbitrary
directed graph or compose application-specific completion callbacks; the
scenario constructors are fixtures rather than a graph-format contract.

`kind: "messages"` takes `senders`, `receivers`, `relay` and `aggregate` for
generic fan-out and frontier fan-in. `direct`, `chain`, `balanced`,
`hierarchical`, `edmonds`, `shortest` and `family` provide comparison routes.
Planner policies operate on generic messages; write routes additionally support
`redundant` independent follower branches. Noise-generated tree families are
deliberately unconstrained in one comparison, exposing how unfiltered alternatives
can worsen path depth and resource costs. They are not the selected load balancer.

The write fixture's `enhancement` is `none`, `blocking` or `optional`, with
`analysis_us`, `verify_us` and `hint_bytes` controlling work sharing. The hint is
assumed valid for the supplied application fixture. It grants no new authority
and models neither an arbitrary extension proof nor synchronous native checking.
This fixture covers plan-like side information. Message enhancement more generally
includes selective-filter masks/row IDs, computed values and partial aggregates;
those may shrink downstream traffic as well as save compute.

See [delivery run examples](DELIVERY.md#network-backed-membership-scenario)
for joins and their separate application ACKs. `read_scenario.py --output OUTPUT`
compares query partitioning and hedging; its `run_reads(config)` API supplies
fine-grained controls. Completed-only percentiles always travel with offered
counts, deadline fractions and outstanding work.

## Evidence that informed the scope

[MINING](../../../orbital/MINING.md) and the
[retired propagation note](../../notebook/consensus-networking.md) supplied
useful failure and accounting cases. This is a new investigation, not a revival
of that retired all-recipient optimizer. The
[CFT network work](../cft-commit-latency/network/README.md) and
[sustained-load cliff](../cft-commit-latency/commit/cliff.md) supply measured
questions and calibration constraints. Their data do not establish the joint
tail of this model. Historical Calico policies remain reference material.
