# CFT commit latency

How quickly can a crash-fault-tolerant (CFT) log make a record durable on a
majority of replicas, and how much throughput can it preserve at that latency?
This spike measures one leader and two followers in distinct AWS availability
zones (AZs). Commit requires durable copies on the leader and at least one
follower. Client RPC, elections and membership changes are outside the boundary.

**Start with [the results and operating limits](RESULTS.md).** Prepared NVMe
logs made sub-millisecond commits possible. The original short-run choice near
130,000 commits/s had 0.923 ms p99.9; a later 60-second raw-log control reached
2.053 ms. The [cliff follow-up](commit/cliff.md) identifies storage throughput
limiting on small instances and measures the costs of preparing real log space
and adapting batch size. None of its larger-host policies established a
sub-millisecond tail at that offered rate.

The investigation builds from **individual writes and network links**, through
**one joint durable commit**, to **a stream of arriving records**. The component
studies explain where time goes; the joint experiments test how those costs
overlap. Their percentiles cannot simply be added. The
[commit path](commit/README.md) introduces quorum completion, batches, windows
and the different measurement clocks before the detailed comparisons. Longer
runs then test what happens as queues grow and prepared space is replenished.

| Follow the question | Read next |
| --- | --- |
| What changes the latency of one record? | [Placement, prepared files, EBS, transport and fallback](commit/latency.md) |
| What did the short throughput screens identify? | [Original choices, queueing and variation between passes](commit/throughput.md) |
| What causes the cliff, and can a controller avoid it? | [Longer runs, live log preparation and service objectives](commit/cliff.md) |
| Why does preparing a log help? | [Persistence concepts](persistence/README.md), then [measured paths and the D3 diagnostic](persistence/FINDINGS.md) |
| How should the leader and its alternatives be placed? | [All 15 AZ pairs and 20 triples](az-findings.md), with the [network method](az-measurements.md) |
| How can I inspect or reproduce a comparison? | [Evidence, field definitions and recovery](evidence.md) |

[Memory characterisation](../memory-characterisation/README.md) is the companion
hardware study within a host: cache and translation behaviour, useful concurrency,
SMT and NUMA placement. Together the spikes connect local work and ownership
transfer to durable replication between hosts. Both retain measurement conditions
and candidate ranges; a measured threshold is not automatically a hardware limit
or a portable tuning constant.
