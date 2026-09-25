# CFT commit latency

How quickly can a crash-fault-tolerant (CFT) log make a record durable on a
majority of replicas, and how much throughput can it preserve at that latency?
This spike studies persistence, network selection and joint replication between
AWS availability zones (AZs). The durable experiments measure completion at a
leader after its own write and one follower acknowledgment. The newer network
work targets the outgoing edge that lets a durable follower begin propagating
admittance in Ashton's 2-of-3 witness design. These are different timing boundaries.

**Start with [the findings and their boundaries](RESULTS.md).** Network latency
depends strongly on the actual hosts and flow tuple: changing one UDP port
selected repeatable latency classes on unchanged machines. A permanent AZ
ranking, or half an RTT assigned to each direction, misses that result.
Prepared logs also made sub-millisecond leader-observed commits possible, while
longer runs exposed storage limits and tails that short screens missed.

The component studies explain individual costs; the joint experiments test
their composition. Separate network and device percentiles cannot simply be
added, and separately measured links do not establish a two-follower race.

| Follow the question | Read next |
| --- | --- |
| How can a shard find and retain a fast outgoing witness edge? | [Network findings and design boundary](network/README.md), then [host/port selection across all 15 AZ pairs](network/selection.md) |
| Which directional differences can the clocks resolve? | [Independent references, drift and uncertainty](network/clocks.md) |
| What separates port choice, host choice and time? | [Controlled comparisons and held-out selection](network/method.md) |
| What changes the latency of one record? | [Placement, prepared files, EBS, transport and fallback](commit/latency.md) |
| What did the short throughput screens identify? | [Original choices, queueing and variation between passes](commit/throughput.md) |
| What causes the cliff, and can a controller avoid it? | [Longer runs, live log preparation and service objectives](commit/cliff.md) |
| Why does preparing a log help? | [Persistence concepts](persistence/README.md), then [measured paths and the D3 diagnostic](persistence/FINDINGS.md) |
| How can I inspect or reproduce a comparison? | [Evidence, field definitions and recovery](evidence.md) |

[Memory characterisation](../memory-characterisation/README.md) is the companion
hardware study within a host: cache and translation behaviour, useful concurrency,
SMT and NUMA placement. Together the spikes connect local work and ownership
transfer to durable replication between hosts. Both retain measurement conditions
and candidate ranges; a measured threshold is not automatically a hardware limit
or a portable tuning constant.
