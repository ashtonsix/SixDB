# One-record commit latency

The one-record experiments isolate the durable path before queueing and batch
formation enter the comparison. A fixed leader starts its write and both
replications together; commit requires local durability and one durable follower
acknowledgment. Both followers finish before the next record starts. Payload
preparation happens before timing. The [commit method](README.md) explains these
boundaries; [throughput findings](throughput.md) measure continuously arriving work.

The result is a sequence of choices: prepare the destination, choose the storage
and placement, then compare transport, record size and the surviving follower.
All commit latencies below are **milliseconds**. Each row's percentiles come from
the same joint observations, rather than adding separate device and link costs.

**Storage names:** NVMe instance store is local to the EC2 host; gp3 and io2 are
EBS block volumes. “Initialized” means the destination was written and synchronized
before the experiment. [Persistence concepts](../persistence/README.md) distinguish
that preparation from preallocation, direct I/O and durable completion.

## Placement and log preparation

The two measured placements use i8g.large instances:

- **Good:** leader `use1-az4`, followers `use1-az2` and `use1-az1`.
- **Bad:** leader `use1-az6`, followers `use1-az4` and `use1-az2`.

The names identify this comparison, rather than permanent properties of the AZs.
These **4 KiB** rows each pool 120,000 commits from three passes.

| Placement / log path | TCP MTU | p50 | p90 | p99 | p99.9 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Good / initialized NVMe | 9001 | 0.474 | 0.480 | 0.493 | 0.520 |
| Bad / initialized NVMe | 9001 | 0.801 | 0.837 | 0.870 | 1.045 |
| Good / growing gp3 | 1500 | 3.197 | 3.264 | 3.324 | 4.119 |
| Bad / growing gp3 | 1500 | 3.533 | 3.622 | 3.736 | 4.978 |

Moving from the bad-placement growing-gp3 policy to the good-placement
initialized-NVMe policy reduced p99 by **86.8%**, from **3.736 to 0.493 ms**
(a **7.58×** latency ratio).

Initialized NVMe uses direct `O_DSYNC` writes on tuned ordered ext4. Growing gp3
uses buffered writes followed by `fdatasync` on ordinary ordered ext4. The latter
is a complete baseline log policy: the difference includes storage, preparation,
I/O path and MTU. It cannot all be assigned to the medium. Both paths request
[power-safe completion](../persistence/README.md#what-makes-completion-durable).

The good initialized-NVMe TCP row placed **99.99917% of observed commits below
1 ms**; its per-pass p99.9 ranged **0.479–0.525 ms**. The bad placement's p99
remained below 1 ms while its pooled p99.9 exceeded it. A deployment preference
therefore needs to name the percentile, as well as its latency budget.

## Prepared files and raw regions

Raw I/O did not improve the good one-record median. At 4 KiB, TCP and MTU
9001, raw NVMe gave p50/p90/p99/p99.9 **0.489/0.497/0.505/0.517 ms** versus
**0.474/0.480/0.493/0.520 ms** for the initialized file. Raw was slightly lower at
the pooled p99.9, with overlapping pass ranges. This comparison offers no broad
latency reason to take on raw-region management. The throughput campaign tests
raw NVMe only, so it does not settle the file-versus-raw choice under load.

## Prepared EBS offers another candidate

Growing gp3 is only one EBS policy. The short joint-commit screen also tested
initialized extents with direct `O_DSYNC`. These rows use the **good placement,
tuned ordered ext4 and MTU 9001**, selecting the lowest p99 within each
device/record-size screen. The transport is part of that selection.

| Device / record / transport | p50 | p90 | p99 | p99.9 |
| --- | ---: | ---: | ---: | ---: |
| gp3 / 4 KiB / UDP | 1.303 | 1.343 | 1.372 | 1.404 |
| io2 / 4 KiB / UDP | 0.720 | 0.768 | 0.823 | 0.896 |
| gp3 / 64 KiB / TCP | 1.864 | 2.004 | 2.060 | 2.170 |
| io2 / 64 KiB / UDP | 0.970 | 1.036 | 1.112 | 1.199 |

**These are screening results: 6,000 commits in three passes per row.** They
support investigating initialized io2 for a sub-millisecond 4 KiB p99, while
initialized gp3 remained above that budget here. Selection used these same
observations, and only about six pooled observations lie beyond p99.9. The
displayed short-screen tails carry less evidence than the longer NVMe runs;
they do not establish an equally well-tested tail preference.
The [complete commit table](../evidence/20260914-durable/commits.csv) retains all
screened paths and their separate per-pass ranges.

## Transport and message size change the comparison

The following rows hold the **good placement, initialized NVMe and MTU 9001**
constant. There are three passes per row: 120,000 commits at 4 KiB and 36,000 at
64 KiB. These are record sizes, not the later pipeline's batch sizes.

| Record / transport | p50 | p90 | p99 | p99.9 |
| --- | ---: | ---: | ---: | ---: |
| 4 KiB / TCP | 0.474 | 0.480 | 0.493 | 0.520 |
| 4 KiB / UDP | 0.477 | 0.485 | 0.511 | 0.523 |
| 64 KiB / TCP | 0.639 | 0.738 | 0.805 | 0.874 |
| 64 KiB / UDP | 0.623 | 0.665 | 0.688 | 0.713 |

The 4 KiB distributions are close; the larger record gives UDP a more useful
tail advantage in this implementation. All 36,000 good 64 KiB UDP commits were
below 1 ms. This observed fraction is not a guarantee for unseen traffic.

MTU also needs a percentile-specific reading. In the **bad placement**, initialized
NVMe TCP at MTU 1500 gave **0.844/0.917/0.941/0.964 ms**, compared with
**0.801/0.837/0.870/1.045 ms** at MTU 9001. Jumbo frames improved the pooled
median and p99 while p99.9 worsened in these passes. The complete data and pass
variation matter more than assigning a universal winner to TCP, UDP or jumbo frames.

## A healthy quorum can conceal a costly fallback

These **4 KiB raw-NVMe, MTU 9001** comparisons remove the normally faster follower
from an otherwise matching case. That follower is already known unavailable;
the measurements exclude failure detection and election time. Each row contains
120,000 commits in three passes.

| Placement / transport / followers | p50 | p90 | p99 | p99.9 |
| --- | ---: | ---: | ---: | ---: |
| Good / TCP / both available | 0.489 | 0.497 | 0.505 | 0.517 |
| Good / TCP / fast follower absent | 0.479 | 0.491 | 0.508 | 0.549 |
| Bad / UDP / both available | 0.807 | 0.830 | 0.843 | 0.932 |
| Bad / UDP / fast follower absent | 1.017 | 1.037 | 1.048 | 1.080 |

The good placement retains a fast alternative in these observations. The bad
placement's tail crosses 1 ms when the fast follower is unavailable. Compare
healthy and absent rows within each transport; this table does not isolate a
TCP-versus-UDP effect. It also does not establish the bounded pipeline's behavior
after a follower disappears: that pipeline retains slots until both acknowledge.

## Evidence and next comparison

The completed durable commit campaign contains **114 configurations, 336 passes
and 2,160,300 commits**, including 19 longer tail configurations. All 336 passes
readback-verified participating nodes in a new process. These checks establish
record content and ordering under the test; durability relies on the recorded
OS/device/AWS completion contracts. They do not physically test a power cut.

[Exact configurations and passes](../evidence.md) retain every screen and longer
repetition. Continue with the [arrival-driven throughput findings](throughput.md)
to see how much of the low-load latency opportunity survives queueing, batching
and sustained arrivals within the measured windows.
