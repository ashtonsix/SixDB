# CFT commit latency

How quickly can a crash-fault-tolerant (CFT) log make a record durable on a
majority of replicas, and how much throughput can it preserve at that latency?
This spike measures one leader and two followers in distinct AWS availability
zones (AZs). Commit requires durable copies on the leader and at least one
follower. Client RPC, elections and membership changes are outside the boundary.

**Start with [the results and operating choices](RESULTS.md).** Prepared NVMe
logs made sub-millisecond commits possible in the measured placement. Under load,
the useful rate depended on which latency percentile mattered: the ENA Express
cohort admitted about 231,000 commits/s below 1 ms at p99, but about 130,000/s
at p99.9. These are selected observations from short repeated runs.

The investigation builds from **individual writes and network links**, through
**one joint durable commit**, to **a stream of arriving records**. The component
studies explain where time goes; the joint experiments test how those costs
overlap. Their percentiles cannot simply be added. The
[commit path](commit/README.md) introduces quorum completion, batches, windows
and the different measurement clocks before the detailed comparisons.

| Follow the question | Read next |
| --- | --- |
| What changes the latency of one record? | [Placement, prepared files, EBS, transport and fallback](commit/latency.md) |
| What rate fits a latency budget? | [Throughput choices, queueing and variation between passes](commit/throughput.md) |
| Why does preparing a log help? | [Persistence concepts](persistence/README.md), then [measured paths and the D3 diagnostic](persistence/FINDINGS.md) |
| How should the leader and its alternatives be placed? | [All 15 AZ pairs and 20 triples](az-findings.md), with the [network method](az-measurements.md) |
| How can I inspect or reproduce a comparison? | [Evidence, field definitions and recovery](evidence.md) |

[Memory characterisation](../memory-characterisation/README.md) is the companion
hardware study within a host: cache and translation behaviour, useful concurrency,
SMT and NUMA placement. Together the spikes connect local work and ownership
transfer to durable replication between hosts. Both retain measurement conditions
and candidate ranges; a measured threshold is not automatically a hardware limit
or a portable tuning constant.
