# Local message handoff measurements

How much do colocated actors pay for payload transfer, queue pressure and their
choice of waiting policy? This native fixture supplies measurement evidence to
[Orbital's simulator](../orbital-simulator/README.md); it defines neither an
Orbital transport API nor a second simulator. Orbital LEAD owns model integration.

Existing [memory characterisation](../memory-characterisation/METHOD.md) times
a payload load after a completed peer handshake and measures cache sharing. It
explicitly leaves batched ownership and ring distance open. Loom and Orbital
currently provide no native queue implementation to measure. This fixture is an
explicit implementation choice, not a production claim or Calico format carryover.
The [findings](FINDINGS.md) retain the two Zen 5 cohorts and their model implications.

## Measurement

One pinned producer and one pinned consumer use a finite SPSC ring. Release/acquire
tail publication protects initialized slots; the consumer releases head only
after checking every payload word. Each slot occupies a multiple of 256 bytes,
and control counters are separately aligned. Payload sizes are 64 B, 1 KiB and
16 KiB; capacity is usually 256 slots, with 16-slot pressure controls. These
geometry choices and the full-payload checker affect costs.

Offered IDs and timestamps come from a fixed schedule independent of completion.
The producer catches up when late; the offered-to-consumed distribution includes
that lateness. A full ring refuses the currently due group of offers; there is no
hidden infinite sender queue, retry or selective deletion from the denominator.
Each run checks offered = completed + refused + unfinished and preserves all IDs.
Unattempted offers at the bounded deadline are unfinished. This is an admission
policy under pressure, not a claim that production should discard messages.
One full-head snapshot refuses an overdue group even if slots become free during
the bookkeeping loop. Refusal time and group size make that authored policy
visible. The final arrival span and half-open offering window are distinct fields.

Batch caps 1, 8 and 32 apply to publication and consumer head release. The producer
publishes already-due work immediately; it never waits to fill a batch. Therefore
the achieved publication size, rather than the cap, defines actual batching.
Only one producer/consumer pair is measured: no fanout, multi-producer contention,
crash recovery, durability, network stack or transaction latency is implied.

The consumer either busy-polls (`spin`), uses C++ atomic wait/notify (`wait`), or
executes 128 dependent integer operations each time the queue is empty (`work`).
The latter counts completed independent work; it is not a real extension workload.
The wait variant signals every publication and includes that cost. It is one
concrete wait implementation, not an optimized event-count design. The producer
uses the same pacing in every policy: sleep when more than 100 us early, then spin
for the final 50 us. Endpoint thread CPU includes polling, pacing, checking and
instrumentation; it must not be substituted for per-message active service time.

Extra cases pause the consumer for 5 ms, reverse endpoint affinity, or stream a
64 MiB array on a third pinned core. Separate-core status, LLC sharing, NUMA and
actual allowed CPUs come from the existing hardware discovery helper. The ring
is first-touched on the producer before timing. Pages are warm; caches are not
explicitly flushed, and cold-start costs are not established. Timings exclude the
initial 10% of scheduled offers, capped at 20 ms; counts and CPU include them.
That is 20 ms in the main panel and 10 ms in the confirmation.

`CLOCK_MONOTONIC_RAW` provides one host-wide timeline, and thread CPU clocks count
both endpoints separately. Every completion is recorded, with no console/file I/O
inside the timed loops. Instrumentation is part of the measured path. Adjacent
clock-call differences are retained as a diagnostic and are **not** subtracted.
The tail store is bracketed by timestamps: publication-to-consumption begins with
a lower/upper interval, not an invented exact publication instant. Producer
preparation, consumer validation and scheduled offer latency are separate fields.
Three reduced-timestamp controls keep the same schedule, payload checker, queue,
ID bookkeeping and accounting while omitting per-item/publication clock reads.
They provide no message latency samples; differences in completion/refusal show
instrumentation sensitivity, not the performance of an uninstrumented queue.

Each case has three consecutive 200 ms offered windows. p99.9 is omitted below
10,000 completed steady samples; even above that threshold it describes a short
instrumented run, not a production tail guarantee. Refusal fractions, CPU costs,
maximum observed queue depth and repeat ranges belong alongside latency. Observed
depth samples are not a continuous occupancy distribution. Repetitions on one
host do not establish fleet repeatability or arbitrary LLC/NUMA placement.
Every percentile is conditional on completion. Whole-run and steady-window
offered/admitted/refused/unfinished cohorts are retained separately, so warmup
losses cannot disappear behind completed steady samples. The main panel has 47
cases. The 18-case confirmation uses 100 ms windows, rates of 8M and 15.625M/s,
matched healthy/paused queue capacities, and large-payload batching controls on
a second host. Neither panel alone is a maximum-throughput search.

## Run and recover

From Linux (prefix `orb -m ubuntu` in the macOS workspace):

```sh
python3 workbench/spikes/orbital-local-handoff/run.py --quick --reps 1 \
  --output build/experiments/local-handoff-check
python3 workbench/tools/worker.py run workbench/spikes/orbital-local-handoff/cloud.sh \
  --machine zen5 --instance-type c8a.xlarge --deadline 1200 --idle-seconds 0 \
  --max-age 1800 --detach
python3 workbench/tools/worker.py run workbench/spikes/orbital-local-handoff/cloud.sh \
  --machine zen5 --instance-type c8a.xlarge --deadline 900 --idle-seconds 0 \
  --max-age 1200 --detach -- --panel confirmation
python3 workbench/spikes/orbital-local-handoff/analyze.py PATH_TO_COLLECTED_STUDY
```

The quick panel checks mechanics on the local machine; it is not Zen 5 evidence.
The shared worker and experiment helpers preserve sources, compiler, hardware,
raw timestamps and execution order. Measurement traffic remains in one process:
**zero cross-AZ workload bytes**. Setup and source/result storage use ordinary
worker network access. The worker has bounded lifetime and shuts down after
collection. No network resources or disks are prepared by this study.

## Further network calibration

LEAD's next priorities are within-AZ evidence-sized messages under concurrent
bulk/repair traffic, then a controlled cluster placement-group comparison. Keep
explicit byte/time ceilings and fixed payload/request-return roles. A paired
idle ping alone cannot calibrate fixed packet cost and load-sensitive queueing.

The existing CFT timestamp probe and shared worker groups are reusable. Same-AZ
placement requires selecting a subnet in that AZ; membership in a worker group
alone does not constrain placement. As of this study, worker launch and pool reuse
do **not** implement EC2 placement groups. A future extension must validate a
caller-owned group, include it in launch **and reuse compatibility**, and retain
actual placement. Passing an ignored config key would invalidate the experiment.
Grouped/control hosts should share instance type and subnet, use repeated host
pairs and flows, and retain losing candidates. No such comparison is claimed here.
