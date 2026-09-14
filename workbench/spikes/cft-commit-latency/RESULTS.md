# Durable commit latency and operating limits

**Preparing the log and choosing its replica placement made sub-millisecond
commits possible. Longer runs expose limits that short throughput screens miss.**
The original ENA Express choice near 130,000 commits/s had **0.923 ms p99.9**;
a later 60-second raw-log control had **2.053 ms**. The
[cliff follow-up](commit/cliff.md) identifies storage throughput limiting on
small instances and measures real background log preparation on larger hosts.
Preparation pauses improved tails in both paired comparisons, but the tested
controllers did not establish sub-millisecond service at that offered rate.

The [study overview](README.md) maps the supporting guides. This report follows
the decision from the low-load path through the original throughput choices
to the longer-run findings and their implications. Measurements are from
14 September 2026 in AWS us-east-1.

## Know which latency is being measured

A record commits after the leader and one follower have made it durable. Local
persistence and both replications overlap; the slower third replica can finish
after commit. The [commit path](commit/README.md) explains how durable prefixes
preserve order and how a lagging replica can still delay later admissions.

| Experiment | Timer and arrival boundary | What it establishes |
| --- | --- | --- |
| One outstanding record | Payload prepared before timing; both followers finish before the next record starts | The joint durable path with a comparable starting state |
| Arrival-driven pipeline | Scheduled arrival to commit, including preparation, batch formation and queueing | Latency at an offered rate, including waiting before admission |

The commit tables and plots use measured joint rounds, with latency in **milliseconds**.
P99 describes the observed 99th percentile, p99.9 the 99.9th. **Pooled** means
combining raw observations before computing the percentile. The original
throughput selections pool three passes; the follow-up reports individual
passes. Pass ranges show variation that pooling can conceal.
The 1 ms reference is a budget for this commit path, excluding client RPC and
failure detection or election time.

## Remove avoidable write and placement cost first

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
followers in az4 and az2. These labels are not permanent AZ properties.
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
  [network study](az-findings.md) identifies fast links and slower alternatives.
  Actual commit tests show what remains when the normally faster follower is
  already known unavailable. Healthy latency alone does not price that fallback.
- **Keep nearby alternatives in view.** Prepared raw NVMe did not improve the
  good one-record median over a prepared file. Initialized io2 was a promising
  EBS alternative in a shorter screen. UDP helped the larger 64 KiB record more
  than the 4 KiB record. Each conclusion has its own population and controls in
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

All measured paths request [power-safe completion](persistence/README.md#what-makes-completion-durable).
Readback checks content and ordering; it is not a physical power-cut test.
The [evidence guide](evidence.md) collects exact cases, populations, captured
sources, recovery commands and independent validation receipts.
