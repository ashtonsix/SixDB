# CFT commit latency

How quickly can a crash-fault-tolerant (CFT) log commit records, and how much
throughput can it preserve at that latency? This spike studies placement, storage
and transport together, measuring a fixed leader and two followers in distinct
availability zones (AZs). Commit
requires a durable copy on the leader and at least one follower. Client RPC,
elections and membership changes are outside the measured boundary.

Start with [the results and operating choices](RESULTS.md): completed joint
commit measurements, their storage/placement explanation, and the repeated
latency versus throughput choices. P50, p90, p99 and p99.9
are separate outcomes; the report states the load and completion contract with
each comparison.

| To understand… | Read |
| --- | --- |
| What latency preferences cost, and where short screens fail | [Throughput findings](commit/throughput.md) |
| How records, batches and quorum completion fit together | [Commit path and measurement](commit/README.md) |
| Why file preparation and durability operations change write cost | [Persistence concepts and method](persistence/README.md), then [findings](persistence/FINDINGS.md) |
| Which AZ paths are fast, and what a slower third AZ changes | [Network findings: all 15 pairs and 20 triples](az-findings.md) |
| How the network observations were obtained and modeled | [AZ measurement method](az-measurements.md) |
| Exact cases, repetitions, captured sources and recovery | [Retained evidence](evidence.md) |

The component experiments explain candidate choices. Joint commit rounds test
their interaction; arrival-driven runs reveal queueing and throughput. Component
percentiles cannot simply be added to predict a commit percentile.

All admitted storage paths use a documented power-safe completion contract.
The [persistence guide](persistence/README.md#what-makes-completion-durable)
explains that qualification and what readback verifies. Source and cloud drivers
live with the experiments; [worker groups](../../tools/worker-groups.md) own
machine lifecycle and private networking.
