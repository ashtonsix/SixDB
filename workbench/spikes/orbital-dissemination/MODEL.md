# What this simulator can establish

The experiment is a causal, finite-resource comparison of authored scenarios.
It can reject a topology because of a dependency, queue or delivery obligation;
it cannot certify a production latency objective. The Python event loop's wall
time has no performance meaning. All simulated times use microseconds, rates
use bytes/µs, and prices are illustrative coefficients per decimal GB.

## Completion and the corrected placement

One authored latency fixture uses the corrected example: the **fast witness follower is also a
producer follower; the leader is not**. Client-producer P and witness leader L
are in AZ A. F has both follower roles in AZ B; the target consumer C is near F.
The fixture also gives slow witness S a payload copy in AZ C so its own acceptance
can satisfy the same durability condition. In this fixture L has no payload-holder duty. These
are experimental choices, not requirements. The underlying question is whether
payload transfer, eligibility evidence, journal persistence, analysis and
dissemination can overlap usefully in a correct composition. Different placements,
combined records and release dependencies are legitimate competitors.

The working target remains **p99 170 µs / p99.9 250 µs**, from client-producer
initiation to a post-persistent point-write extension effect. The precise effect
boundary remains open. Results report these distinct events rather than choosing
whichever one meets the target:

- `payload_ready`: P's PLP write plus at least one remote follower's PLP write.
  This is readiness at a qualifying holder, not consumer effect completion.
- Primary write result: the consumer near F possesses payload and an admitted
  journal prefix and finishes its modeled analysis/fold. It represents an
  internal replayable effect; it excludes external I/O and checked-native
  verification. No additional fold-state PLP write is assumed on that endpoint.
- `response`: that target consumer returns a result to P, using a different
  response origin from the request's witness leader.
- `all_consumers`: every designated consumer finishes, a background replication
  obligation rather than the universal write-latency objective.

The literal current-brief baseline is:

```text
P PLP → remote payload PLP → durable receipt back to P
      → admission request to L → L journal PLP
      → F/S journal PLP → contiguous admitted prefix → C input join → effect
```

The **unadopted conditional-proposal candidate** overlaps:

```text
          ┌→ P→F/S payload → follower payload PLP ───┐
P PLP ────┤                                         ├→ eligibility + prefix → C
          └→ P→L request → L journal PLP             │
                         → F/S journal PLP ──────────┘
```

Follower branch propagation never waits for a return to L. The return still
consumes resources and releases L's outstanding-journal credits. Each physical
witness has a separate per-shard contiguous journal gate. Ready entries cannot
skip an earlier missing one. Shared physical CPU/NIC/device queues are distinct
from that logical prefix. The baseline does not fix a journal slot before
remote eligibility; the candidate does.

The fixture treats operations as independent eligible items before journal
allocation; it does not implement admission-frontier extraction for every
producer's multi-record stream. The separate frontier fixtures check gaps and
contiguous progress. The tests deliberately distinguish an unrepaired lost
payload from a temporary delay with bounded retries. Both accepted and refused
operations remain in the offered denominator.

Conditional certificate checking in [delivery.py](delivery.py) is a fact guard,
not consensus. See [the candidate's unresolved obligations](DELIVERY.md#the-tentative-witness-proposal-candidate)
before treating its simulated completion as implementable. Leadership election,
authority transfer and conditional-entry recovery are absent from the timing
model; a crash does not silently run those protocols.

## Physical accounting

[simulator.py](simulator.py) supplies one event queue and resources indexed by
physical host. Several roles and shards using one host share those resources.

| Resource or mechanism | Modeled behavior | Limit |
| --- | --- | --- |
| CPU | Fixed per-packet plus per-byte service, finite slots/byte credits; relay and consumer compute use it too | Synthetic service demands; no instruction/cache/NUMA model |
| Network | Shared transmit and receive serializers per host; direct TX/RX overlap across propagation | Packet-stream approximation; RX occupies `max(upstream, receiver)` serialization, conservatively reserving a slow incoming stream |
| Fabric | Optional shared named store-and-forward bottlenecks, separate finite queues | Explicit additional hops, not a private copy of an uplink per graph edge |
| Local IPC | 0.1 µs propagation plus memory serialization and endpoint CPU work | User's 100 ns edge is an authored extreme, not a whole-operation measurement |
| Persistence | Shared device-byte service followed by bounded concurrent completion service | No real fsync/device controller, preparation reserve or media-tail model |
| Packetization | 1,400-byte default MTU, 80-byte packet overhead, 16-byte per-message framing; fragment/reassembly accounting | Encapsulation/authentication overhead is parameterized rather than an implemented protocol |
| Batching | Per-host/destination/traffic-class queues, byte threshold and oldest batch timer | Does not create savings on an upstream edge that was already individually packetized |
| Reliability | Real reverse ACK packets; bounded exponentially spaced retries; packet loss, duplicates, delay and incarnation checks | No congestion controller, selective ACK, FEC, route repair or automatic replay in generic transfer logic |
| Priority | Nonpreemptive control priority, one-in-eight noncontrol service when waiting; configurable bulk/repair credit exclusion | A mechanism to compare, not a selected QoS policy; ordinary data can consume reserved credits |
| Queue limits | CPU/NIC/device/fabric/batch/reassembly limits; write journal and consumer-input credits; explicit refused/unfinished counts | Each stage has its stated budget, not one global allocator; trace/output dictionaries are simulator instrumentation |

Source wire bytes and price are charged at TX completion even if a later fabric
queue refuses the packet. A departed packet can survive source failure. Active
partially transmitted bytes at the final observation instant are not included
until TX completes. Retained source buffers after generic transport ACK are not
a modeled durable delivery ledger; the membership experiment separately owns
that obligation and its application ACK.

Faults are programmatically scheduled with `Network.inject`: process crash and
recovery, CPU/device slowdown, directed partition, delay change and loss change.
Multiple simultaneous faults represent correlated incidents. Loss/partition is
sampled at packet arrival; this is not physical bit-level interruption. Existing
resource jobs retain their nonpreemptive duration through a crash and discard
their guarded result. New work sees the changed service rate. Overlapping changes
to the same fault property are not stacked; use nonoverlapping intervals or one
combined interval. A recovered transport incarnation does not possess an old
process's volatile send buffers or acquire new authority.

Reassembly has finite bytes and an expiry allowing unloaded message serialization
plus the configured timer. A single lost fragment can require resending a whole
logical transfer; that intentionally exposes amplification. ACK loss can yield
duplicate receives, and sender timeout exhaustion need not mean the receiver
failed. Useful follow-ups include selective repair, credit-based pacing,
packet-number acknowledgments, fragment/chunk coding and persistent sender lanes.
Their costs should be compared to this baseline, not assumed to be free.

## Reads and changing membership

[read_scenario.py](read_scenario.py) runs cross-shard scatter, within-shard replica
partitioning, result reduction and ring responses through the same network core.
Its application supplies a cut and exact chunk coverage. Duplicate attempts do
not satisfy missing chunks. Scan workers have explicitly separate reserved
service resources shared by colocated shards; scan bandwidth is synthetic and
does not claim a complete memory-system model. Hedges count admitted scan bytes
even when the winning answer makes them useless; active scans are not cancelled.
The `queue_ready` selector sees instantaneous cut readiness and maintains in-flight
work reservations. It is an optimistic scheduling comparison, not a demonstrated
distributed feedback protocol. Lost dispatch can leave a conservative reservation.

[membership_scenario.py](membership_scenario.py) adds a logical recipient while
the old route continues to omit it. An atomically captured snapshot barrier and
pinned tail define what is owed. Snapshot chunks, tail repair, durable acceptance
and application ACKs share network and device service with foreground delivery.
Repair decisions use missing ACKs and timeouts. Finite retention refuses new
offers rather than evicting an owed tail. Snapshot validity, atomic registration,
fencing and survival of completed durable state are supplied facts. This probe
models one stream, not a distributed snapshot protocol or witness membership
transfer. [Delivery](DELIVERY.md) owns the distinction.

## Input assumptions and interpretation

The standard write fixture uses 256-byte inputs, 64-byte journal bodies, 10 µs
persistence completion service, 2,000 bytes/µs device service, 1,250 bytes/µs
NICs, two packet/compute CPU slots, 8 µs same-AZ propagation and 100 µs cross-AZ
propagation. Slow-witness cross-AZ edges add 60 µs. These are **authored favorable
assumptions**. The campaign varies propagation, payload, rate, bursts, topology,
CPU/device faults and queue limits. Public links use a shared 125 bytes/µs pool,
1 ms propagation and an illustrative 0.09/GB coefficient; cross-AZ edges use
0.01/GB. These are not quoted provider prices.

The nearby [CFT persistence evidence](../cft-commit-latency/persistence/FINDINGS.md)
measured about 11 µs median and 110 µs p99.9 for one selected 4 KiB i8g path.
The [host/tuple selection evidence](../cft-commit-latency/network/selection.md)
retains useful directed estimates with clock bounds and held-out windows;
[port sampling](../cft-commit-latency/network/studies/port-sampling.md) records
occasional tuple changes. Those observations motivate sensitivity dimensions.
They do **not** calibrate the joint distribution of this pipeline. In particular,
8 µs same-AZ propagation is an optimistic sensitivity point, not a fitted value.
No reported percentile was assembled by adding independently ranked stage
percentiles or dividing RTT by two.

To calibrate, record matched per-operation producer persistence, payload receipt,
journal acceptance, consumer join/effect and return events under simultaneous
fanout, actual payload sizes and sustained load. Preserve correlated host/flow
time blocks and clock uncertainty. Fit CPU/packet, byte throughput, preparation
traffic and queue capacities separately; use held-out load/fault periods to test
the composed model. Replace assumed constants with those measurements before
using the modeled frontier for host placement or an SLO commitment.

`p99_us` and `p999_us` are nearest-rank percentiles of completions through the
explicit drain. The 170/250 µs fractions include **all offered operations**,
including refusal and unfinished work. Reports retain cutoff backlog, oldest
unfinished age, pending credits and service/traffic counters. A 1,000-operation
case has only one sample in its nominal 0.1% tail and supplies no tail confidence
claim. Deterministic service cases describe queue/dependency behavior, not a
sampled production distribution. Failed comparisons are retained with successes.
