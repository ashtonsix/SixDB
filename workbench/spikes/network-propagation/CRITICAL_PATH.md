# What the 5.322 ms means

**5.322 ms is a synthetic p99 for full 1 MiB delivery to every one of eight
recipients: nine hosts across three modeled AZs.** The clock starts with the
whole object available at the source and stops after the last recipient's
receive CPU work. It excludes persistence, consensus and returning receipts.
This workload was not matched to the user's eight-VM, five-AZ xmem experiment;
the headline needed this context next to it.

The [tail diagnostic](evidence/20260919-critical-path/index.html) shows the
actual objects behind the percentile, their overlapping transmissions and
completion dependencies. [Selected data](evidence/20260919-critical-path/diagnostic.json)
contains the timestamps and one-input sensitivities.

## Exact workload and route

- 1,048,576 payload bytes, 16 forwarding chunks of 64 KiB, outer MTU 1500.
- 100 objects/s Poisson offers; 32 objects each for seeds 17, 29 and 43.
- Assumed separate 10 Gbit/s TX and RX per host, plus shared route pools.
  Every outgoing stream consumes the sender's shared TX capacity.
- One CPU queue per host, with assumed 32 Gbit/s byte processing, 1 µs per
  chunk job and 0.08 µs per packet. Send and receive work both consume it.
- Same-zone one-way delay 25 µs; A→B 125 µs and A→C 175 µs, plus synthetic
  jitter. No losses, repair, failure, background traffic or rewriting here.

```text
a0 ─┬─ a1 ─┬─ a2
    │      └─ b2 ─┬─ b0
    │             └─ b1
    └─ c2 ─┬─ c0
           └─ c1
```

The completion distribution is p50 **2.429 ms**, p90 **3.669 ms**, p99
**5.322 ms**, p99.9 **5.517 ms**. All 96 objects completed. The corresponding
all-receipts-returned p99 is **5.456 ms**. These are finite synthetic sample
quantiles, with no empirical SLA or production-tail confidence claim.

There is no literal 5.322 ms object. Linear interpolation across 96 samples
gives `p99 = 0.95 × 5.310826 + 0.05 × 5.538416`: seed 43, objects 29 and 28.
Both finish at b1, with zero-based chunk 15 completing last. The original
workbench's first-object schedule illustrates pipelining but cannot explain
the tail percentile. The new diagnostic replays the complete seed, including
all contenders; passive auditing exactly preserves every original output field.

## The actual completion dependency

For object 29, start the clock at its release. Objects 27 and 28 were released
1.133 ms and 0.632 ms earlier. Their remaining work competes with object 29.
Each object requires two complete source copies. A 64 KiB chunk occupies
73,536 modeled wire bytes in 50 packets; the two copies consume 2,353,152 bytes
and **1.883 ms of source service** at 10 Gbit/s. Three objects arriving within
1.133 ms demand **5.648 ms of source service**, before forwarding completes.
The nominal 100/s rate hides this burst; average source data demand is only
about 18.8% of capacity.

Following the latest prerequisite at every CPU and stream gate produces this
non-overlapping chain. It crosses into an earlier object's work because of
shared CPU queues and per-object/edge FIFO streams:

| Elapsed ms | Completion-determining work |
| --- | --- |
| 0–2.747 | Earlier object 28's a0→a1 stream advances through chunks 1–9 under shared capacity. |
| 2.747–2.849 | That chunk propagates to a1; receive processing and the two forwarding CPU jobs run. |
| 2.849–4.493 | Object 28's a1→b2 stream drains chunks 9–15 under shared capacity. |
| 4.493–4.761 | Propagation to b2; its CPU handles object 28's final chunk/receipt/fanout, then object 29's chunk 12 and its fanout. |
| 4.761–5.261 | Object 29's b2→b1 stream drains chunks 12–15. |
| 5.261–5.311 | Final local propagation, 25.945 µs, then b1 receive CPU, 23.384 µs. |

The full chain is in the selected data. Its intervals sum to 5.310826 ms.
Network service durations already include reduced rates under contention;
they must not have an extra queueing estimate added to them. This is a
dependency explanation of one realized simulation, not an additive prediction
of the gain from removing work: changing a contender changes the service rates.

The last chunk's own route is `a0 → a1 → b2 → b1`. Its source CPU preparation
finishes at 1.117 ms, but it waits for earlier chunks until 4.430 ms, reaches a1
after receive processing at 4.597 ms, reaches b2 at 5.026 ms and b1 at 5.311 ms.
Calling the whole 1.093 ms before its CPU starts an avoidable CPU penalty would
be wrong: earlier chunks are already transmitting during that interval. The
diagnostic's separate last-chunk table preserves these waits without conflating
them with the latest-prerequisite chain above.

## Overlap already present, and opportunities

Object 29 starts a0→a1 transmission at **0.415 ms**. Forwarding a1→b2 starts at
**0.860 ms** and b2→b1 at **1.271 ms**. The source continues sending until
**4.553 ms**. The c-zone branch runs concurrently and completes at **5.117 ms**;
it is not another serial stage to add. Receive and transmit NIC resources are
separate, so forwarding overlaps receiving subsequent chunks. Per-host CPU
work and outgoing copies still contend.

Controlled replays hold this route and the seeds fixed; none reoptimizes it:

| Changed input | p50 ms | p90 ms | p99 ms | p99.9 ms |
| --- | ---: | ---: | ---: | ---: |
| Original workload | 2.429 | 3.669 | 5.322 | 5.517 |
| Each object alone, preserving its jitter draws | 2.427 | 2.434 | 2.438 | 2.438 |
| 4 KiB payload instead of 1 MiB | 0.239 | 0.244 | 0.251 | 0.252 |
| 1 MiB payload, 4 KiB forwarding chunks | 2.175 | 2.944 | 4.933 | 4.937 |
| Outer MTU 9000 | 2.210 | 3.414 | 4.741 | 5.064 |
| Every network capacity pool raised to 25 Gbit/s | 1.574 | 1.628 | 2.609 | 2.832 |
| Zero CPU service cost, limiting counterfactual | 2.303 | 3.818 | 5.280 | 5.574 |

Object 29 itself takes **2.426 ms alone versus 5.311 ms in the burst**. The
2.885 ms difference is a paired contention counterfactual, not a sum of its
queue waits. Erasing CPU cost scarcely changes p99 and worsens p99.9: releasing
traffic earlier changes contention. Smaller chunks improve pipeline overlap;
larger MTU reduces wire and packet work; extra capacity reduces backlog. Their
individual gains cannot be added, nor do these rows establish a jointly tuned
optimum.

The model gives active streams equal shares. It does not evaluate
completion-aware inter-object scheduling, packet-level cut-through or striping
different chunks over different trees to distribute fanout. These are genuine
algorithm opportunities, especially for a burst of large objects. The bounded
fixed-tree search does not establish that 5.322 ms is near the best attainable.
Isolating objects removes competing offered work; it is not evidence of a
scheduler achieving that latency at unchanged throughput.

## What can be compared with xmem

The local [xmem report](../../../../calico/xmem/REPORT.md) documents durable
**4 KiB** commits with **one outstanding operation**. Nearby five-AZ cases
report p50 2.421 ms for seven witnesses/q5 and 2.208 ms for ten witnesses/q7,
using medians of trial p50s. Their reported p99s are 2.994 and 8.003 ms.
These identify related evidence, not the user's exact eight-VM run.

The 5.322 ms headline used a payload 256 times larger, waited for every required
copy instead of a durable quorum, allowed concurrent offers, and quoted a
different percentile. It cannot establish a speed ratio against xmem. Those
differences also do not prove the modeled delay is justified or optimized.
The 4 KiB sensitivity gives a more relevant **0.251 ms synthetic delivery p99**,
but its topology and hardware assumptions still differ and it omits durable
writes and consensus. It is not an estimate of xmem's or Orbital's commit time.

## Replay

Recover the [original full result](evidence/20260918/artifact.json) into the
input directory if it is not already local. From the repository root:

```sh
orb -m ubuntu env PYTHONPATH=build/network-propagation/deps \
  python3 workbench/spikes/network-propagation/critical_path.py \
  --input build/network-propagation/final \
  --output build/network-propagation/critical-path-20260919
```

The diagnostic asserts complete seed replay equality, derives the last recipient
and completing chunk from timestamps, checks each CPU/stream gate against its
latest prerequisite, and checks both accounting paths sum to completion time.
The retained archive includes the complete audit, selected diagnostic, exact
source snapshot and input hashes. The selected JSON can regenerate the report
without simulation using `critical_path_report.py OUTPUT_DIRECTORY`.
