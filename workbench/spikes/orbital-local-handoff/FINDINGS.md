# Colocated messages consume CPU, queue space and readiness time

The first native measurements support a distinct same-host transport model,
with polling occupancy separated from active message work. They do not support
fitting a scalar “local NIC latency.” Large batches can postpone the availability
of useful payloads enough to reverse a small-message win.

## Main cohort

[Retained per-case results](evidence/20260927-main/summary.csv) contain 141 trials:
47 cases, three consecutive 200 ms offered windows each, **32,523,600 offers**.
All offered IDs were accounted for: **644,227 refused, zero unfinished**, with no
payload/order violations. Most refusals belong to the explicitly undersized or
paused queues; a completed-only result would hide them.

The fresh `c8a.xlarge` reported AMD EPYC 9R45 / Zen 5, four separate cores sharing
one LLC and one exposed NUMA node. CPUs 0 and 1 were endpoints; CPU 2 ran the
optional 64 MiB streaming neighbour. The compiler was pinned Clang 21.1.8, Release
with generic tuning. This is a pre-touched, instrumented SPSC fixture with full
payload validation, not a production transport implementation. [Method](README.md)
owns timestamp boundaries, admission policy, affinity and remaining limitations.

Values below are medians of three per-run statistics, not pooled quantiles.
Latency is conditional on completion; retained whole and steady cohorts preserve
refused and unfinished offers. Sparse trials have only 180 steady completions,
so no p99.9 is reported.

| 64-byte workload / consumer policy | Offer → consumed p50 | Preparation → consumed p50 | Receiver CPU per window |
| --- | ---: | ---: | ---: |
| 1k/s, spin | 5.327 us | 0.180 us | 198.999 ms |
| 1k/s, atomic wait | 10.670 us | 5.840 us | 0.633 ms |
| 100k/s, spin | 0.243 us | 0.170 us | 199.980 ms |
| 100k/s, useful empty-queue work | 0.283 us | 0.200 us | 199.976 ms |
| Bursts of 32, average 1M/s, batch 32, spin | 1.705 us | 1.260 us | 199.970 ms |
| Same bursts/batch, atomic wait | 2.928 us | 2.410 us | 18.840 ms |
| Same bursts/batch, useful empty-queue work | 2.420 us | 1.650 us | 199.962 ms |

Sparse producer scheduling contributes roughly 5 us to the offer latency; it is
not transport time. Producer CPU for those 200 offered messages was about 0.437 ms
with spin and 0.734 ms with atomic wait. At 100k/s and higher, the chosen pacing
loop occupies approximately a second full core. Consequently endpoint CPU divided
by completions includes pacing and idle polling, and cannot become a simulator's
per-message service coefficient.

Useful polling completed about **188.7 million dependent integer operations** in
the 200 ms burst/batch-32 window while consuming the same receiver CPU budget.
Its latency increase is a tradeoff for actual counted work. That synthetic
arithmetic loop does not establish extension throughput or fairness. A separate
streaming neighbour did not materially move the small-message median in these
cases; it does not establish immunity to cache/memory interference.

## Batching has a payload-readiness cost

The same 32-at-once / 1M/s schedule gives the following offered-to-consumed times.
All steady offers completed in these cases. One 64-byte batch-1 repetition refused
32 offers during warmup; that remains visible in the whole-run denominator.

| Payload | Publication cap | Achieved mean batch | p50 | p99 |
| --- | ---: | ---: | ---: | ---: |
| 64 B | 1 | 1 | 2.762 us | 5.304 us |
| 64 B | 8 | 8 | 1.636 us | 2.416 us |
| 64 B | 32 | 32 | 1.705 us | 2.401 us |
| 1 KiB | 1 | 1 | 3.136 us | 6.070 us |
| 1 KiB | 32 | 32 | 2.754 us | 4.032 us |
| 16 KiB | 1 | 1 | 11.100 us | 20.653 us |
| 16 KiB | 32 | 32 | 23.098 us | 30.225 us |

The implementation initializes all payloads before publishing the batch tail.
At 16 KiB, cap 32 makes the receiver wait for a 512 KiB preparation group before
any member is visible. Cap 1 overlaps preparation with consumption. This causal
difference belongs in the model; a cheap per-batch event with immediately ready
children would miss it. These results do not choose an optimal production batch
size or require copying when a safe reference handoff could suffice.

## Tail behaviour and queue admission remain visible

Pausing the 256-slot spinning consumer for 5 ms refused **4,800 of 200,000 offers**
in each main repetition. Its completed-message p99 remained roughly 2.4 us, while
p99.9 rose to **4.89–4.91 ms**. The accepted backlog preserves the stall in the tail;
refused offers have no completion latency and must remain a separate outcome.

Even healthy short-run tails varied. At 100k/s with spin, offered p99.9 ranged
**1.123–9.058 us** across three runs. At 4M/s with atomic wait and publication cap 1,
it ranged **14.388 us–5.911 ms**; one run refused 384 offers and another accumulated
producer delay without refusing any. Cap 32 achieved a mean batch of only
1.02–1.08 in that evenly spaced workload. A configured cap is not the batch size.
These short runs cannot establish a production p99.9 SLA or the 170/250 us durable
effect target.

The adjacent-clock diagnostic was 20 ns at both p50 and p99 in every main trial.
Multiple clock reads and dense per-ID bookkeeping are in the hot path. Reduced
timestamp controls at 4M/s still completed every offer, so that rate alone could
not quantify the observer's capacity cost. The targeted confirmation raises local
rates and adds matched healthy/paused 16-slot controls, as described below.

## Targeted confirmation: observer cost and matched queues

A second fresh host exposed the same Zen 5 / four-core / shared-LLC arrangement.
[Its 54 trials](evidence/20260927-confirmation/summary.csv) used the identical
native probe bytes, with three 100 ms windows per case. They accounted for
**31,350,000 offers: 1,448,767 refused and zero unfinished**. The reduced controls
retain full payload validation and ID bookkeeping, but omit per-item/publication
timestamps. They have no latency distribution, rather than fabricated zeros.
Completion counts include the bounded drain after the offering window.

| Local offers/s | Cap / timestamps | Completion fraction across repeats | Receiver finish, from window start |
| --- | --- | ---: | ---: |
| 8M | 1 / full | 97.067–100% | 130.032–133.059 ms |
| 8M | 1 / reduced | 100% | 100.001–100.002 ms |
| 8M | 32 / full | 99.944–100% | about 100.001 ms |
| 8M | 32 / reduced | 100% | about 100.000 ms |
| 15.625M | 1 / full | 100% | 254.170–257.672 ms |
| 15.625M | 1 / reduced | 87.050–100% | 122.444–151.441 ms |
| 15.625M | 32 / full | 89.060–90.452% | about 100.010 ms |
| 15.625M | 32 / reduced | 99.909–100% | about 100.000 ms |

The full-timestamp cap-1 15.625M/s case completed everything but accumulated
approximately **85 ms median scheduled latency**. Its preparation-to-consumption
path stayed short: most delay was the producer catching up to scheduled arrivals.
Cap 32 bounded that delay partly by refusing work. Counting all offers and the
post-window drain distinguishes both outcomes from sustainable service.

Instrumentation changes the balance between endpoints. Removing clock reads can
make the producer fast enough to expose queue refusals that a slower instrumented
producer hid. Therefore neither subtracting the adjacent-clock diagnostic nor
scaling all times by one factor repairs the model. These are **fixture capacity
observations**, not bare SPSC or production limits.

Matched 16-slot controls clarified the initially alarming approximately 50%
refusal result. With 32 offers arriving together, the authored due-batch rejection
policy completed **50.000–50.016% while healthy**, for both spin and wait. A 5 ms
pause reduced completion to approximately **47.46–47.49%**; the queue geometry
already caused most refusals before the fault. The 256-slot healthy controls
completed everything, and their paused controls completed **95.168%** in the
100 ms window, with 4,832 refusals and a completed p99.9 near 5 ms. This is why
capacity, burst shape, admission policy and fault effect need matched controls.

The large-payload reversal also repeated: 16 KiB bursts had a median offered p50
of **11.011 us with cap 1 versus 22.677 us with cap 32**, with all offers completed.
That agrees with the main host's 11.100 versus 23.098 us without assuming that
one host establishes arbitrary-placement or fleet-wide tail behaviour.

## Integration interpretation

These are proposals for LEAD's calibration comparison, not changes to the
simulator or its contracts:

- Give same-host transfer a CPU/memory/finite-queue path instead of charging the
  NIC byte servers. Preserve actor ports, message identities and the same physical
  resource accounting; this does not need another execution model.
- Charge idle polling occupancy separately from preparation, publication,
  payload access and application work. Useful polling may spend that occupancy on
  independent work. Sleeping releases CPU but introduces wakeup delay and cost.
- Keep the publication dependency on actual batch preparation, alongside bytes,
  message count and effective batch size. Enhancement that must finish before
  forwarding has the same dependency shape, with its own measured work.
- Compare schedules and policies using offer latency, refused/unfinished cohorts,
  available queue space and both endpoint CPU budgets. Separate active service
  calibration from host scheduling and offered-load generation.

No simulator constants were changed here. Cross-host IPC, socket loopback,
cross-LLC/NUMA placement, multi-producer/fanout work and real application kernels
are outside this measurement. Within-AZ evidence messages under bounded competing
bulk traffic, followed by matched placement-group controls, remain the next
network questions; neither is answered by these shared-memory numbers.

## Evidence and resource accounting

Across both cohorts: **195 trials, 63,873,600 offers, 61,780,606 completions,
2,092,994 refusals, zero unfinished and zero reported payload/order violations**.
The six native drain/full-queue cases and twelve observer negative controls passed
on both workers. The [evidence guide](evidence/README.md) gives exact per-trial,
raw-case and captured-source locations and recovery commands.

Both temporary Spot workers are [verified terminated](evidence/resources.json).
The workload sent **zero cross-AZ bytes**; the two existing worker archives total
about 875 MB compressed and also include sources, binaries and raw timestamps.
Ordinary setup/source/result traffic is separate from workload traffic. No claim
of zero cloud cost or observed billing is made, and no further workers are running
for this study.
