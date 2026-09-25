# Network latency and witness selection

**Select and validate actual host/port combinations.** On fixed machines,
changing one UDP endpoint port produced RTT classes separated by as much as
266 µs, while same-tuple revisits changed by a median of about 0.9 µs across
host pairs. New machines or long waits are not required to obtain a different
latency class. Host choice still matters, and the short observations do not
establish IID rerolls or long-term stability.

The [selection findings](selection.md) contain the controlled comparisons and
held-out candidates for both directions of all fifteen AZ pairs. They supersede
the original AZ-only placement interpretation. The original TCP measurements
remain useful observations of that cohort; they do not rank an AZ independently
of its hosts and connections.

## Optimize the first durable follower's branch start

Ashton's shard has many data holders and three adaptively assigned witnesses.
The leader writes to PLP-backed instance store, sends to both followers, and
each follower can propagate admittance down its arborescence branch as soon as
its own write completes. The first follower's branch-start time is modeled as

`leader_write + min(network(L,F1) + write(F1), network(L,F2) + write(F2))`

plus relevant software overhead. A 110 µs outgoing edge and the supplied ~15 µs
write assumption at each end give roughly **140 µs to that branch start**.
This is a composition example, without a measured percentile: the network
probes contain no PLP writes, concurrent fanout or arborescence propagation.
The [older durable experiments](../commit/README.md#follow-one-record) instead
overlap the leader write with replication and wait for an acknowledgment at
the leader. Their measured commit latency has a different boundary.

Finding a fast pair is better supported than choosing its faster initiating
side. Selected point medians near 100–110 µs occurred on both az2–az4 and
az1–az5, but clock bounds cannot certify a ≤110 µs edge or its few-microsecond
directional advantage. RTT/2 is only a symmetric model. Extra machines reveal
placement variation; they do not average away shared clock-reference error.
The third witness's alternate path and the joint follower race still matter.

## Follow the measurement question

| Question | Read |
| --- | --- |
| Can ports reroll latency cheaply, and what survives validation? | [Host/port selection](selection.md), including all 30 AZ directions and the UDP/QUIC implications |
| What controls distinguish hosts, tuples, roles and elapsed time? | [Experiment design](method.md), including training, holdout and sample budgets |
| What is a one-way estimate allowed to claim? | [Clocks and timestamp boundaries](clocks.md), including identifiability, uncertainty and sensitivity |
| What did the earlier measurements actually establish? | [TCP RTTs and modeled triples](studies/tcp.md), [initial one-way cohort](studies/oneway-initial.md), [fresh-host confirmation](studies/oneway-confirmation.md), [dense tuple control](studies/tuple-dense.md) |
| Where are sources, every candidate and raw recovery? | [Evidence and recovery](../evidence.md#network-cohorts); [probe tools](../oneway/README.md) and [tuple tools](../variance/README.md) |

Measurements use stable AWS IDs `use1-az1` through `use1-az6` in us-east-1.
Letters such as `az2a` name a host **within one cohort**, not the same machine
across cohorts. Network tables here use microseconds unless stated otherwise;
the historical TCP and durable-commit tables use milliseconds.
