# Initial reference implementation

2026-09-27. The maintained library now has native actor ports, shared physical
resources, two executable model families, independent checks, an interactive
client, and captured batch experiments. These findings concern simulator
construction and bounded protocol experiments, not measured SixDB performance.
The later [composed experiments](experiments/README.md) retain the WAN convoy,
queue fairness, hosted recovery and competing retention-root comparisons.

## What closed the initial prototype exercise

The spike's [checked-old-cut composition](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-simulator/FINDINGS.md)
established actual position assignment, agreed context, private reads, matching
reports and checked publication across selected restarts. Its final
[retirement probe](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-simulator/RETIREMENT-PROBE.md) added 30 histories
and nine targeted tests: reconstruct old bytes after reclamation/restart, retain
both reader and replay dependencies, publish a durable checkpoint before retiring
its predecessor, and keep backend borrows alive after their owner's death.

Those probes exposed enough concrete boundaries to start the maintained library.
The native retained-view fixture expresses the same semantic obligations through
ordinary bounded read/list/delete and borrowed-buffer ports. It needs neither a
copied environment dispatcher nor a coordinator subclass intercepting phase names.
Its suite includes 18 successful root/incident/seed cases, seven deliberately
incorrect protocol cases, memory refusal, an independently corrupted observation,
and exact replay. This does not establish a distributed reclamation policy.

## What building and testing the library changed

Independent checks and Scaffolding's review exposed consequential mistakes in
the initial implementation, now corrected and retained as regression cases:

- A host reset must cancel CPU and NIC service as well as disk work. A process
  crash must stop its CPU work while allowing submitted disk/NIC work to survive.
  Healthy colocated work advances when the dead process's remaining CPU work ends.
- A packet still in flight and a message already resident in a host buffer have
  different lifetimes. Queued delivery cannot resurrect old bytes in a new process.
- Timer payloads need a byte charge. Cancelled events must release actual queued
  closures too, rather than remain until distant deadlines consuming laboratory
  memory and dispatch budgets.
- An old write completing after restart must keep its original incarnation in
  telemetry. Recovery ancestry must join the actual surviving write, independently
  of whether a fault happened to be triggered by observing that write.
- Observers may pause or enqueue an incident; direct lifecycle mutation and nested
  execution during observation are rejected before they can invalidate a handler.
- Evidence must survive runner interruption and report output errors. Per-case
  arguments, stdout/stderr and choices are retained as they are produced; completed
  trial receipts are flushed incrementally. Path aliases cannot overwrite replay
  input accidentally.

The core has 20 native checks. The Orbital model has 15 checks, including actual
leader-plus-follower persistence, consumer reconstruction, local reservation
release, two-shard transfer, pending predecessor reads, mismatch abort and
write-before-callback recovery. Twelve independent client checks cover replay,
trace independence, censored work, negative controls and evidence handling. The
retained-view suite and interactive example complete five CTest groups. All five
pass both normally and with AddressSanitizer/UndefinedBehaviorSanitizer.

## Captured campaign

The first campaign ran **132 histories**, seeds 1/7/19, with no evaluator errors
or reported safety violations. It includes all six incident settings, three link
delays, two offered rates, shared-worker memory limits and a separate longer WAN
retry comparison. Declared offers, arrivals, completions, unfinished obligations,
missing incidents and event-budget exhaustion remain separate fields.

| Family | Histories | Completed / declared offers | Interpretation |
| --- | ---: | ---: | --- |
| 1 µs links | 36 | 684 / 684 | All authored work and requested incidents complete within the window |
| 100 µs links | 36 | 684 / 684 | Same semantic work completes with greater physical delay |
| 20 ms links, 10 ms window | 36 | 0 / 684 | Deliberately censored; 18 milestone-triggered incidents never fire |
| Shared workers, 32 KiB per host | 6 | 0 / 114 | Physical capacity prevents completion; only 15 offers reach durable coordinator admission |
| Shared workers, 128 KiB per host | 6 | 7 / 114 | All 114 are admitted; many cannot finish within the retained-history budget |
| Shared workers, 16 MiB per host | 6 | 114 / 114 | Same offered work completes |
| 20 ms links, longer window, two retry periods | 6 | 21 / 42 | Three runs complete; three stop at their event budget |

In the last family, a 100 µs retry period exhausts 200,000 dispatches after about
76.6 ms of virtual time, with seven admitted offers and no completions in each
seed. An 80 ms period completes all seven offers in each seed and runs to the
3 s observation deadline in 15,267 dispatches. These are **different observed
prefixes**, not a throughput ratio or a calibrated recommendation for retry time.
The result motivates a controllable retry policy and explicit censoring. The
finite-memory outcomes likewise expose admitted-work obligations; they do not
establish necessary production memory sizes or a completion-reserve design.

The [selected summary](evidence/reference-initial/campaign/summary.json) retains
all 132 histories. Its [archive reference](evidence/reference-initial/artifact.json)
recovers the complete run, including captured sources, receipts and per-case choices.
The original local copy is `build/experiments/simulator/reference-initial/`. The captured source digest is
`33acb8a6f591e31f588707664fe0cb7d7e05266e00b5c0c481fd6fe0c67bfe32`
(1,429 files, unchanged during execution). The receipt selects the campaign
summary for retention; the source archive, exact per-case choices and build/test
logs remain available for replay. Later documentation edits do not alter that
captured implementation. The [README](README.md) gives the capture command.

## Boundaries still open

The native composition makes disagreement a durable abort under its existing
single decision-owner assumption; it resolves declared outputs before responding.
It does not make a permanently lost local outcome journal recoverable. Prepared
authority replacement, payload/frontier admission, network adaptation, general
extension transcripts and complete-replacement supersession remain explicit model
gaps. The retained-view case does not implement UFFD, COW or distributed GC.

Both the Orbital protocol model and its full-state observer retain growing
history. The native event engine's streamed telemetry does not make that model a
steady-state capacity experiment. Future questions may require a focused spike,
a richer physical service or a different protocol component. The maintained
library provides a common execution and observation boundary for that work.
