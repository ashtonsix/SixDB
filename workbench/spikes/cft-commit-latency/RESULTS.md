# Findings: network selection, durable latency and operating limits

**A fast network edge is a property to discover on actual hosts and flows.**
Changing just one UDP port selected repeatable latency classes without replacing
machines. This revises the original interpretation of AZ placement and supports
cheap screening for an adaptive witness assignment. It does not establish a
permanent AZ ranking, IID rerolls or a certified 110 µs one-way edge.

**Prepared logs made sub-millisecond leader-observed commits possible; longer
runs exposed limits missed by short screens.** The original ENA Express choice
near 130,000 commits/s had **0.923 ms p99.9**; a later 60-second raw-log control
had **2.053 ms**. The [cliff follow-up](commit/cliff.md) identifies storage
throughput limiting on small instances and measures real background log
preparation on larger hosts. The tested controllers did not establish
sub-millisecond service at that offered rate.

The [study overview](README.md) maps the supporting guides. The original TCP,
storage and durable-pipeline measurements are from **14 September 2026**;
the clock-calibrated UDP and fixed-tuple comparisons are from **24–25 September**.
Both use AWS us-east-1, with different hosts, protocols and timestamp boundaries.

## Select the hosts and flow, then validate the outgoing edge

On unchanged az2b/az6a hosts, changing one endpoint's UDP port produced four RTT
classes spanning **266.2 µs**; each class recurred over five shuffled rounds.
These offset-independent comparisons identify a tuple-sensitive mechanism,
without identifying its physical cause. Reopening the same tuple generally
preserved its class during the test; changing a port was the effective reroll.

The [larger port-policy comparison](network/studies/port-sampling.md) separates
candidate count from spacing. At 16 candidates, scattered ports gained a median
0.41 µs over consecutive ports; increasing 4→32 candidates improved 31/60 pairs
by >5 µs under either policy. Four consecutive ports already spanned 355 µs.
Most tuples remained stable, but a few changed by up to 103 µs during this
18-minute capture. Revalidation remains part of the supported hypothesis.

Host choice still matters. The [selection findings](network/selection.md)
compare ports on fixed machines, then select host/port candidates for all thirty
AZ directions using held-out outgoing requests. Promising pairs occurred on
both az2–az4 and az1–az5. Their clock bounds cannot certify a ≤110 µs outgoing
edge or resolve which endpoint has a few-microsecond advantage. Opposite
selected directions may use different tuples, so subtracting their winners
would not measure asymmetry.

The supported operational hypothesis is to **screen host pairs, sample ports,
validate separately, retain the tuple, and recheck**. The probe budget is small,
but the eventual UDP/QUIC transport and real PLP/fanout path need validation.
[Evidence and accounting](evidence.md#network-cohorts) records each cohort,
traffic, scoped cost models and verified cleanup, separately from actual billing.

## Know which latency is being measured

The current [witness design's target](network/README.md#optimize-the-first-durable-followers-branch-start)
is the first follower's branch start after a leader write, outgoing message and
follower write. There is no return leg in that model. In the older durable
experiments, local persistence and both replications overlap, and the leader
waits for a durable acknowledgment. The [commit path](commit/README.md) explains
that measured boundary, durable prefixes and how a lagging replica can still
delay later admissions.

| Experiment | Timer and arrival boundary | What it establishes |
| --- | --- | --- |
| Calibrated UDP probes | Kernel software TX→RX for each leg; peer turnaround removed from RTT | Host/tuple effects and conditional directional estimates, without durable writes or joint fanout |
| First durable follower model | Leader write, then first outgoing-message-plus-follower-write completion | Design boundary only; ~140 µs with assumed 15/110/15 µs costs is not a measured p99 |
| One outstanding record | Payload prepared before timing; both followers finish before the next record starts | The joint durable path with a comparable starting state |
| Arrival-driven pipeline | Scheduled arrival to commit, including preparation, batch formation and queueing | Latency at an offered rate, including waiting before admission |

The commit tables and plots use measured joint rounds, with latency in **milliseconds**.
P99 describes the observed 99th percentile, p99.9 the 99.9th. **Pooled** means
combining raw observations before computing the percentile. The original
throughput selections pool three passes; the follow-up reports individual
passes. Pass ranges show variation that pooling can conceal.
The 1 ms reference is a budget for this commit path, excluding client RPC and
failure detection or election time.

## Prepare the log and interpret the measured placements

The storage comparison uses host-local NVMe instance store and gp3, an AWS
Elastic Block Store (EBS) volume type. An initialized log region has already been
written and synchronized before records arrive.

For 4 KiB records, moving from a growing-gp3 log in the worse measured placement
to initialized NVMe in the better placement reduced p99 from **3.736 to
0.493 ms**, an **86.8% reduction**. The latter's p99.9 was **0.520 ms**, with
pass values of **0.479–0.525 ms**. Each policy below pools 120,000 commits across
three passes on i8g.large.

![Four measured TCP log policies show how storage preparation and placement change the durable commit distribution. Whiskers are observed pass ranges; the dashed line is 1 ms.](images/commits.png)

“Good” and “bad” identify the two measured placements. The good leader is in
`use1-az4`, with followers in az2 and az1; the bad leader is in az6, with
followers in az4 and az2. These labels identify the tested host/connection
configurations. The later tuple controls show why neither the labels nor the
entire latency difference
can be attributed to AZ identity alone.
Initialized NVMe uses direct `O_DSYNC` writes on tuned ordered ext4; growing gp3
uses buffered writes followed by `fdatasync`, with a different MTU. The complete
policy comparison changes several things, so its entire gain cannot be assigned
to the storage medium. The [one-record findings](commit/latency.md) give the exact
rows and the narrower comparisons:

- **Prepare before demand.** Holding buffered-fdatasync and ordered ext4 fixed,
  initializing the destination reduced i8g median write completion from 62.5 to
  17.4 µs; preallocation alone gave 54.6 µs. A real log must pay that preparation
  ahead of use. [Persistence findings](persistence/FINDINGS.md) explain the paths.
- **Choose the leader and its fallback together.** The
  [network study](network/README.md) supports screening actual outgoing edges.
  The older joint commit tests show what remains when the normally faster
  follower is already known unavailable. Healthy latency alone does not price that fallback.
- **Keep nearby alternatives in view.** Prepared raw NVMe did not improve the
  good one-record median over a prepared file. Initialized io2 was a promising
  EBS alternative in a shorter screen. The UDP policy had a larger advantage
  at 64 KiB than at 4 KiB; flow choice was not controlled across transports.
  Each conclusion has its own population and controls in
  the [one-record comparison](commit/latency.md).

## Read the original short-run choices

The original pipeline uses **4 KiB records, raw NVMe and direct `O_DSYNC`**,
with the good placement and one pinned application CPU per voter. **Goodput** counts unique
durable commits/s without a deadline filter. Candidates qualified by keeping
up and draining their work in **all three passes** under the
[finite-run stability rule](commit/README.md#count-waiting-from-the-arrival-schedule).
Among those candidates, the following are the highest-goodput choices for the
named **pooled** 1 ms budget. ENA (Elastic Network Adapter) is the AWS network
interface; Express is its alternative network path.

**The later 60-second probe did not confirm the near-130,000/s sub-millisecond
choice:** raw logs with a four-record batch cap reached 2.053 ms p99.9, and initialized files reached
1.648–2.544 ms across two passes. These fresh-cohort observations also change
instrumentation and preparation conditions. The table below preserves the
original short-run selections; it is not a sustained-rate recommendation.

![Original short-run choices on common axes: a p99 budget selects more throughput than a p99.9 budget. The later longer-run follow-up did not preserve the near-130,000/s sub-millisecond tail.](images/operating-choices.png)

| Cohort / budget | Commits/s | p50 | p90 | p99 | p99.9 |
| --- | ---: | ---: | ---: | ---: | ---: |
| i8g.large, standard ENA / p99.9 | 52,693 | 0.602 | 0.708 | 0.790 | 0.834 |
| i8g.8xlarge, standard ENA / p99.9 | 128,881 | 0.755 | 0.842 | 0.919 | 0.987 |
| i8g.8xlarge, ENA Express / p99.9 | 129,537 | 0.687 | 0.806 | 0.882 | 0.923 |
| i8g.8xlarge, ENA Express / p99 | 231,309 | 0.686 | 0.812 | 0.945 | 1.055 |

The larger standard-ENA point has little p99.9 margin: its pass range is
**0.894–1.019 ms**, so one pass exceeds the pooled budget. Express's p99 choice
stayed below 1 ms at p99 in all passes (**0.932–0.955 ms**), while every pass's
p99.9 exceeded it. Its p99.9 choice stayed at **0.816–0.930 ms** across passes.
The [throughput findings](commit/throughput.md) retain the policies, offered rates,
all four percentile preferences and their alternatives.

The original Express cohort's fastest stable point, **322,317/s**, completed only
**74.814%** of its arrival cohort below 1 ms, with **48.861 ms p99.9**. Choosing its 1 ms p99 or p99.9 point
therefore gives up **28.2% or 59.8%** of the largest observed stable goodput.
Stable admission and acceptable latency are separate requirements.

These are short observations. The table's rows contain about **45, 44, 43 and
23 post-warmup seconds total** across three passes. A two-million-record cap
shortens high-rate passes; the 322,317/s candidate has only about **5.2 seconds
per pass**. These measurements do not establish long-term capacity.

## What the longer-run probe changes

The [targeted follow-up](commit/cliff.md) separates three engineering decisions:

- **Keep demand within the resource budget.** At 104,400 offered/s, the small
  instance's 60-second reproduction reached **1,235 ms p99**. A local-only
  control reproduced the slowdown and reported NVMe throughput limiting while
  its operation-rate limit counter stayed zero. Larger batches reduced operation count but could
  not remove the byte-throughput deficit. Two 60-second passes at 90,000/s had
  **0.809–0.911 ms p99.9**; that rate still exceeds the small instance's network
  baseline in replicated bytes, so it is measured headroom rather than an SLA.
- **Pay for preparation while serving.** On Express at 129,600/s, live file
  preparation gave **3.788–5.267 ms p99.9**. Pausing preparation under pressure
  improved it in both paired repeats, to **2.027–2.415 ms**, while preparing
  the full consumed range. Adding batch adaptation gave **1.292–10.621 ms**
  across two passes: its better median did not establish a reliable tail gain.
- **Judge the actual deadline outcome.** Under the same 45,000/135,000/s burst
  trace, combined adaptation improved p99.9 from **12.652 to 1.753 ms**, yet
  reduced the post-warmup arrival fraction committed within 1 ms from **90.847% to
  85.714%**. Both policies committed all offered work. That is a real trade-off,
  not an improvement at every latency objective.

The larger-host residual tails did not carry the same NVMe limit signal. The
follow-up identifies the small-instance cause; it has not explained or removed
every tail mechanism. Its [proposed operating policy](commit/cliff.md#what-this-supports-for-a-service-objective)
combines resource budgets, retained-byte limits, preparation reserves and explicit
admission costs.

The original [throughput guide](commit/throughput.md) still supplies the sampled
landscape: sparse rate coverage, variation near numerical cutoffs, and the cost
of choosing different percentiles. Its [network ceiling comparison](commit/throughput.md#what-the-network-ceiling-explains)
also distinguishes standard ENA's per-flow limit from aggregate instance
bandwidth. These are separate constraints from the storage ceiling above.

## Carry the result into a system

A continuing deployment also has to retire or reuse log space, control admission
and recover replicas. This bounded pipeline retains slots until both followers
acknowledge, so it does not sustain admission after a follower disappears. The
known-absent-follower tests belong to the separate one-record experiment.

[Memory characterisation](../memory-characterisation/README.md) supplies the other
half of this hardware picture: within-host concurrency, cache sharing and
placement. Its findings show why CPU identity or NUMA-node identity alone can
miss a relevant cost. Neither spike turns a local optimum into a universal
tuning constant; the consuming workload and its actual environment matter.

The durable-write experiments request [power-safe completion](persistence/README.md#what-makes-completion-durable).
Readback checks content and ordering; it is not a physical power-cut test.
The [evidence guide](evidence.md) collects exact cases, populations, captured
sources, recovery commands and independent validation receipts.
