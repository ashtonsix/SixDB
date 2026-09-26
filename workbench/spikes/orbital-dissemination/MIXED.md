# Mixed traffic and work shaping

2026-09-26. [mixed_study.py](mixed_study.py) adds twelve executable comparisons
of foreground work with background scans, extension computation and result
shuffle. It uses the shared finite CPU, memory, NIC, persistence and queue
resources in [simulator.py](simulator.py). All inputs are authored. These are
simulation findings, not hardware measurements or a protocol selection.

The foreground is a deliberately small **durable notification proxy**: persist
a payload locally, transmit and persist one remote copy, notify an effect host,
then execute a local effect. A separate metric ends when a different client
receives its response. There is no witness admission or transaction decision in
this workload. Its timings must not be presented as the brief's write latency
or compared with the 170us/250us write targets.

The background is an immutable-cut read whose chunks scan resident memory,
perform extension work and send outputs to a sink. Completion requires every
chunk to be reduced there. The cut and operator correctness are supplied facts;
this is not a distributed snapshot or checked-native execution implementation.
The 2x computation case repeats CPU work without changing logical coverage.

## What the comparison holds and changes

The retained run offers 600 foreground jobs at 20,000/s over 30ms, then drains
for 20ms. Background work arrives at 1,000 reads/s in pairs: 30 reads, each
scanning 128KiB. Default output is 4KiB per read. Extension computation costs an
authored .006us per input byte, plus .5us per chunk; a full-size chunk therefore
holds a CPU for about 787us. The normal host NIC rate is 1,250B/us. Resources
have finite queues; full queues can drop work, leading to bounded retries or
unfinished reads. Application credit refusal is separately counted.

The shared configuration gives foreground and background two CPU slots on one
effect host. Core separation assigns one slot to foreground/packet work and one
to background extension work, keeping their total at two while sharing the
host's NIC and memory resource. Host separation assigns one CPU slot to each
host and adds another NIC and memory resource. Other hosts remain fixed. This
is a resource-budget comparison with explicit extra network capacity in the
host case, not free isolation. Resident source data is assumed available on
the chosen host; cold replication and deployment costs are excluded.

The foreground runs in the simulator's control priority class. Background work
uses bulk priority. A control selection can run ahead of waiting bulk jobs,
but cannot preempt a job already executing. The reserved-credit case protects
64KiB of each resource queue from bulk use; it does not reserve execution time.

## Retained results

Percentiles below describe this finite completed cohort. With 600 foreground
jobs and 30 reads, they are not calibrated estimates of production tail
probabilities. The authored 150us foreground deadline fraction uses **all
offered jobs**. Read completion includes only exact whole-query coverage.

| Case | Foreground effect p99, us | Within 150us | Reads complete/offered | Read p99, us |
| --- | ---: | ---: | ---: | ---: |
| Foreground only | 84.56 | 100% | 0/0 | — |
| Shared, 128KiB chunks | 853.69 | 62.7% | 30/30 | 849.3 |
| Shared, 8KiB chunks | 152.12 | 97.7% | 30/30 | 1,326.1 |
| Separate core pools, 128KiB chunks | 84.56 | 100% | 30/30 | 1,614.9 |
| Shared large chunks, reserved control credits | 853.69 | 62.7% | 30/30 | 849.3 |
| Shared large chunks, 2x computation | 1,647.30 | 22.7% | 30/30 | 1,625.3 |
| Shared small chunks, output 16x input | 105.12 | 99.5% | 3/30 | 10,667.1 |
| Separate cores, output 16x input | 85.31 | 100% | 3/30 | 8,558.4 |
| Separate host, output 16x input | 84.56 | 100% | 2/30 | 8,892.6 |
| Small chunks, 2x computation, two-read credit limit | 182.86 | 84.3% | 16/30 | 2,188.7 |
| Cancel large chunks after 50us | 852.90 | 62.7% | 0/30, cancelled | — |
| Cancel small chunks after 50us | 152.12 | 98.8% | 0/30, cancelled | — |

**Chunk size trades foreground blocking against background overhead.** Reducing
chunks preserves scanned and computed input bytes, but creates more stages,
packets and ACK waits. Background wire traffic rises from 0.137MB to 0.227MB
in these two cases, while read p99 rises from 849us to 1,326us. Foreground p99
falls from 854us to 152us. Large chunks also delay transport processing enough
to cause 134 retries despite no injected packet loss; small chunks avoid them
in this run. Priority alone cannot make a 787us nonpreemptible job fit a much
smaller deadline.

**Core separation protects latency by allocating capacity.** Its foreground
p99 returns to the proxy baseline while background p99 approaches twice the
shared-large result. The total effect/background CPU slot count remains two.
It is therefore not sufficient to claim isolation improved the system from
foreground latency alone; background service and unused reserved capacity matter.
Reserved queue credits leave the shared-large outcome unchanged because its
dominant problem is active CPU occupancy, not lack of control queue space.

**Output expansion changes the bottleneck and can hide behind healthy
foreground service.** A 16x output ratio offers about 2.1GB/s of background
payload at the selected arrival rate, beyond one 1.25GB/s sender NIC before
framing. Only 3 of 30 reads complete on the shared host. Core and host separation
do not cure the overloaded background path; the sink and its receive resources
also remain common. The three variants transmit 39.88MB, 42.40MB and 42.74MB
of background wire bytes including retries and ACKs while completing 3, 3 and
2 reads respectively. Most reads lose required chunks after bounded transport
give-up. The p99 of those few completed reads is not a successful service result.

**Admission limits bound accepted demand by declining work.** The two-read
credit limit admits 16 and refuses 14 of 30 background reads in the doubled-
computation case. All sixteen admitted reads complete. Refusals remain in the
offered denominator and have a separate counter; this is capacity protection
with less accepted work, not a throughput improvement at equal acceptance.

**Cancellation needs a real stopping point.** Cancelled queries launch no new
chunks, but submitted nonpreemptive work still runs and reserves input/output
credits. The first large-chunk query is cancelled at 50us and retires its
135,168-byte reservation at 793.49us. Its small-chunk counterpart retires at
50.47us. Already emitted output retains its credits until ACK or the explicitly
bounded transport give-up time. Cancellation therefore does not immediately
rescue the foreground large-chunk tail, and zero completed reads must not be
reported as an improved read workload.

## Reproduce and inspect

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_mixed.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/mixed_study.py \
  --count 600 --output build/workbench/orbital-dissemination/mixed.json
```

Eight checks cover invariant scan/compute work under chunking, exact whole-read
completion, core-pool resource sharing, charged recomputation, offered-denominator
refusal, delayed cancellation retirement, distinct foreground/client endpoints,
wire attribution including ACKs, and the nonpreemption limit of reserved credits.
The compact JSON retains source hashes, full configurations, per-class
offered/admitted/completed/refused/unfinished counts and deadlines, application
compute demand, per-resource elapsed service, actual completed-TX bytes by class,
queue occupancy, held application credits and a few cancellation examples.

Wire attribution is specific to this fault-free, unbatched workload: sink ACKs
belong to the background, and retries retain their original class. It sums to
the simulator's completed-TX wire counter. A transmission still active at the
drain boundary is not yet charged there; elapsed resource service is reported
separately. Application CPU demand excludes packet processing, which remains
included in physical CPU resource service. Cancellation is cooperative between
stages; active jobs are not preempted. Credit release after exhausted retries
does not establish successful delivery.

The useful design signal is to optimize work grain, amplification, acceptance
and resource placement together. Next calibration should replace the authored
operator and packet costs with measurements, including the price of isolating
cores or moving resident state. These examples do not select a universal chunk
size, queue reserve, throughput limit or deployment layout.
