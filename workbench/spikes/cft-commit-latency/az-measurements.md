# Measuring AZ links

Measure all 15 AZ pairs, in both initiating directions, and explore how message
size and MTU affect round-trip latency. Compare the 20 possible three-AZ sets
using their measured pair links. These are observations from one six-instance
cohort, not a latency guarantee for an AZ or an application replication benchmark.

The [September 14 findings](az-findings.md) contain pair tables, MTU effects and the
good-versus-bad three-voter CFT comparison. [Retained evidence](evidence/20260914/README.md)
provides all measured cases and recovery of the six raw worker archives.

## Measurement

Six fresh, on-demand `i4i.xlarge` instances use the same pinned Ubuntu image and
Clang 21.1.8, with SMT disabled (two physical cores visible). This instance type
is offered in all six AZs, including the restricted instance selection in
`use1-az3`. Instances communicate over private IPv4 addresses in the default VPC,
without a placement group. Stable AZ IDs are recorded alongside this account's
letter names. See [AWS's AZ-ID explanation](https://docs.aws.amazon.com/global-infrastructure/latest/regions/az-ids.html).

The study changes the interface MTU on all hosts to **1500** or **9001** before
each measurement epoch. It leaves NIC offloads and ordinary Linux TCP settings
at their recorded defaults. [AWS's MTU guide](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/network_mtu.html)
describes standard and jumbo frames. New TCP connections negotiate their limits
after each change; `TCP_INFO` records PMTU and send/receive MSS.

- **TCP echo:** 64, 512, 1400, 4096, 8192 and 65536 application bytes in each
  direction. A native C++ probe uses a persistent connection with `TCP_NODELAY`
  at both ends, one outstanding request per client, and a full-size echo reply.
  Connection establishment, configuration and 200 warmup exchanges precede
  3000 retained samples. Each sample measures monotonic time from immediately
  before sending the request through receipt of the complete reply; payload
  validation happens after the timer. Connect time is recorded separately once
  per case. Client and echo server use different pinned physical cores.
- **ICMP echo:** 56, 1472, 1473, 8973 and 8974 payload bytes, IPv4, DF set.
  Payload plus 28 bytes of IPv4/ICMP headers establishes the MTU boundary.
  Fitting sizes run 1000 probes at a requested 2 ms interval. Oversize probes
  run three attempts to verify local rejection. ICMP timings preserve iputils'
  printed precision. Missing replies remain losses rather than being excluded
  silently from success rates.
- **Repetition:** three passes. MTU order is 1500/9001, 9001/1500, 1500/9001;
  matching and TCP-size order are deterministically shuffled per pass. Each
  host communicates with one peer at a time, with both initiating directions
  active concurrently. Five disjoint-pair matchings cover all 30 directions.
  Barriers precede cases; RTT timers exclude control traffic. There is no
  throughput/saturation background workload or periodic S3 upload during cases.
  This reciprocal traffic is part of the measured condition.

Each TCP direction/MTU/size has 9000 samples across three connections; pooled
unordered pairs have 18000 samples. Fitting ICMP cases have up to 3000 per
direction. Percentiles use linear interpolation (Hyndman–Fan type 7). Reports
retain min, mean, standard deviation, p1/p5/p25/p50/p75/p90/p95/p99/p99.9/max,
threshold exceedances, retransmissions, ICMP loss and per-repetition p50/p99.
P99.9 and maxima have limited tail support; temporal correlation means sample
count alone is not an independent-trials confidence bound.

NIC/driver/offload configuration, ENA counters, TCP counters, CPU time and
interrupts are captured before and after epochs. TCP retransmissions come from
per-connection `TCP_INFO`; complete TCP request counts do not establish packet
loss rates. These round trips include both hosts' OS and application processing;
RTT/2 is not a measured one-way latency. No fsync, WAL, quorum or simultaneous
three-node replication is measured. Three-AZ rankings use the **largest of the
six directional p99s**, with mean directional p50 as a deterministic tie-break.
That score is a placement comparison, not a percentile of a composed transaction.
One host per AZ cannot describe within-AZ host/rack/path variation, and repeated
passes over minutes cannot describe diurnal or longer-term network tails.

## Three-node network model

`consensus.py` evaluates every leader in every triple using the measured link
distributions. A healthy leader needs the first of its two follower replies;
with one follower unavailable, it needs the remaining reply. The underlying
measurement never fans out to both followers simultaneously, so their joint
delay distribution is unknown.

An empirical cumulative distribution F(t) is the fraction of observed RTTs no
greater than t. For follower distributions F1 and F2, the cumulative probability
of receiving the first reply is bounded by `max(F1, F2)` and `min(1, F1 + F2)`.
Inverting these bounds gives latency ranges across possible relationships between
the observed marginals. The independent-links scenario uses
`1 - (1 - F1) * (1 - F2)` and is labeled separately.

These bounds apply to the empirical marginals, not an unknown production
population; they are not confidence intervals. The CDF inverse uses nearest-rank
quantiles, unlike the interpolated descriptive tables. The single-follower-loss
scenario uses the remaining link's measured RTTs without injecting a failure or
election. The [findings](az-findings.md) show the resulting placement comparisons.

## Run and recover

From the repository root on Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/spikes/cft-commit-latency/launch.py
python3 workbench/tools/worker_group.py wait build/az-latency/RUN/group.json
python3 workbench/spikes/cft-commit-latency/summarize.py \
  build/workers/JOB1/results build/workers/JOB2/results \
  build/workers/JOB3/results build/workers/JOB4/results \
  build/workers/JOB5/results build/workers/JOB6/results \
  --output build/az-latency/RUN/summary
```

`launch.py` chooses the AZ cohort and supplies a single subnet per AZ to
[worker groups](../../tools/worker-groups.md), which resolve availability,
capture source once and submit the named participants.
A unique study-owned security group permits only self-referencing ICMP and TCP
ports 43000–43001; no public ingress is added. The existing worker IAM profile
provides S3 access. Initial S3 rendezvous uses a unique prefix under `sixdb/`;
a study-local HTTP barrier coordinates measurement phases thereafter.

Every worker is fresh, with an hour deadline/lifetime and no idle retention.
Scripts stay alive through the final barrier so their servers cannot disappear
before peers finish. Group `wait` verifies each archive, closes the dedicated
workers and removes their temporary security group. For a partial launch, use
`worker_group.py cancel GROUP.json`. `collect.py GROUP.json [--abort]` forwards
to those shared operations; the original completed `campaign.json` remains a
historical receipt. Probe, barrier and retained measurement bytes are unchanged
by the lifecycle extraction.

The worker archives hold raw per-sample RTTs, ping output, host diagnostics and
the exact probe sources. Small summaries and recovery references belong beside
the findings; do not put the complete raw sweep or binaries in Git.
