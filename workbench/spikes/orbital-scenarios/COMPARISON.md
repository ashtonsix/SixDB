# Contention comparison

2026-09-26. **Large observations should not retain exclusive influence over
source writers. BRIEF2 achieves that, but its retry rule does not solve hot
updates.** The strongest alternative tested fixes a transaction's serial position
*before* its final execution. It removes source-renewal failures for stable
output coverage, at a substantial cost in coordination, queued writers and fresh
reads of those outputs. Ordinary indexed updates already expose the missing
guarantee of complete output coverage.

This study compares mechanisms, including alternatives prompted by failures in
the first runs. It does not select BRIEF2 by default or establish a production
protocol. The arbitration baseline follows the
[archived brief](../../../orbital/stale-drafts/BRIEF-arbitration.md). “BRIEF2” below
names the former compute/promote/renew proposal, not today's
[brief](../../../orbital/BRIEF.md).

## Mechanisms tested

Every timed comparison uses actual values, shared service queues, participant
links and private computation delays. Committed observations and effects are
replayed in the claimed serial order. Eligible short local transactions execute
atomically within one service tick under every policy; they do not all read a
frozen epoch-start image. **Ticks and work units are synthetic, not measured
milliseconds or throughput.**

| Policy | What it changes; what it pays |
| --- | --- |
| Arbitration | The old retained read/write protection, independent components, union reservations and dependent-suffix invalidation. Complete collection and atomic invalidation favor this baseline. |
| Wound-wait / wait-die | Replace components with stable transaction age and whole-attempt restarts. Retain broad read exclusion and older-waiter handoff. |
| Conservative validation (`occ`) | Always advance the position and validate all sources. A conservative control using the shared engine, not a tuned conventional OCC implementation. |
| BRIEF2 (`certification`) | Compute privately at a snapshot; promise outputs; renew sources if the final position advances. Bounded whole-attempt retries. |
| Snapshot wait | Keep BRIEF2's chosen snapshot and wait at blocked initial reads. Output and renewal conflicts still restart. |
| Known-write admission (`ordered-writes`) | Admit predeclared outputs before choosing the snapshot. Read-through write admission, followed by BRIEF2 certification. |
| Output wait | Compute first, then queue for outputs, retaining the original computation. |
| Output refresh | Discover outputs, queue for them, then choose a fresh snapshot and recompute. Still certify afterward. |
| Fixed known / fixed discovered | Admit all output coverage, fix and announce the position, then execute at it. The discovered variant pays an initial discovery execution. No subsequent promotion or renewal. |

A separate `fixed-position-control` ablation merely rejects BRIEF2's promotion;
it is **not** the new fixed-before-execution protocol. Fixed arrival-window folds
are also tested for completion-only additive programs, with original request
latencies and outcomes retained.

The 19 timed workloads cover ordinary local/distributed work, cold/hot keys,
disjoint/conflicting WAN work, broad reports, maximum-derived writes, predicate
insertions, dependency backedges, unchanged aggregate answers, blind replacement,
bulk updates, sparse pending outputs and partial claims. Separate executable
histories cover moving maximum-row targets, conditional invariants and maintained
secondary indexes. The wider [read](reconsideration/READS.md) and
[write](reconsideration/WRITES.md) catalogs identify further real SQL/HTAP/ETL cases;
this is not a SQL executor.

## What the comparison establishes

Unless stated otherwise, tables use seed 7, width 64 and the fixture's original
horizon. **p99 includes successful requests only.** Where outcomes differ, counts
are complete / failed / pending; unfinished requests have not disappeared.

**Broad observations are the clearest reason to remove retained read protection.**
Move 128 point writers from outside a report's collection to inside it:
arbitration's writer p99 rises from 1 to 96 ticks; wound-wait and wait-die reach
92 and 91. The multiversion variants keep it at 1, with all requests complete.
Across seeds 0/7/19 and widths 16/64/256, arbitration reaches 93–105 versus 1
under the original multiversion comparators. This benefit does not require all
of BRIEF2's certification machinery.

**A coherent old maximum can be useful without freezing the collection.**
A transaction can read the maximum and write a separate marker while source rows
keep changing, provided the resulting history can place it before those changes.
If source writers first read that marker, that backedge constrains the order.
BRIEF2 needs seven attempts for three such transactions; arbitration needs three.
Even updates to losing rows cause conservative renewal failures: eight attempts
in the unchanged-maximum case. More semantic validation could distinguish them,
but is additional machinery rather than a free property of snapshots.

**Waiting saves reports from unnecessary retry failure.** Narrow WAN writers
leave unresolved outputs scattered across a broad report's scope:

| Policy | Reports complete / failed / pending | Report p99 |
| --- | ---: | ---: |
| Arbitration | 128 / 0 / 0 | 361 |
| Wound-wait | 17 / 0 / 111 | 624 |
| Wait-die | 128 / 0 / 0 | 337 |
| BRIEF2 | 54 / 74 / 0 | 238 |
| Snapshot wait | 128 / 0 / 0 | 155 |
| Known-write admission | 128 / 0 / 0 | 143 |
| Output wait / refresh | 128 / 0 / 0 each | 157 / 148 |
| Fixed known / discovered | 128 / 0 / 0 each | 217 / 220 |

Snapshot wait uses one attempt per report. An explicitly older-cut contract also
finishes all 128, at p99 100; that is a different freshness service. This comparison
does not establish external consistency. The wound-wait result is a continuing
convoy through broad readers and queued writers, not a demonstrated deadlock.

**Actual bulk mutation has a different cost from broad observation.** With three
bulk increments, 64 overlapping point updates and reports:

| Policy | Bulk complete / failed / pending | Bulk p99 | Point-update p99 |
| --- | ---: | ---: | ---: |
| Arbitration | 3 / 0 / 0 | 312 | 264 |
| BRIEF2 | 2 / 1 / 0 | 433 | 13 |
| Snapshot wait | 2 / 1 / 0 | 430 | 12 |
| Known-write admission | 3 / 0 / 0 | 179 | 165 |
| Output wait | 2 / 0 / 1 | 610 | 27 |
| Output refresh | 3 / 0 / 0 | 293 | 259 |
| Fixed known | 3 / 0 / 0 | 308 | 294 |
| Fixed discovered | 2 / 0 / 1 | 505 | 146 |

All 64 point updates complete in these rows. BRIEF2 discards 2,056 compute units.
Admission avoids much of that waste by making conflicting writers wait through
computation. Early fixed positions additionally make fresh output readers wait:
in the bulk-plus-reports fixture, report p99 is 67 with snapshot wait, 63 with
known-write admission, 259 with fixed known, and 296 with arbitration. Blind
replacement, partition publication and chunked ETL can choose different
contracts; they do not automatically preserve an arbitrary atomic SQL update.

**Queuing is progress, not extra capacity.** For 128 cross-shard read-modify-write
requests targeting one pair, the original horizon is 1,200 ticks:

| Policy | Complete / failed / pending at 1,200 | Complete / failed / pending at 4,800 |
| --- | ---: | ---: |
| Arbitration | 26 / 0 / 102 | 104 / 0 / 24 |
| Wound-wait | 32 / 0 / 96 | 128 / 0 / 0 |
| Wait-die | 35 / 0 / 93 | 128 / 0 / 0 |
| BRIEF2 | 6 / 122 / 0 | 6 / 122 / 0 |
| Snapshot wait | 9 / 119 / 0 | 9 / 119 / 0 |
| Known-write admission | 23 / 0 / 105 | 93 / 0 / 35 |
| Fixed known | 16 / 0 / 112 | 64 / 0 / 64 |
| Fixed discovered | 15 / 0 / 113 | 64 / 0 / 64 |

Output wait reaches 19 / 3 / 106 at 1,200; refreshing before certification reaches
22 / 0 / 106. At the separately labeled 20,000-tick drain, output wait has failed
32 requests; output refresh and both fixed variants complete all 128. Fixed
variants have p99 about 9,380 ticks. This is completion of a finite workload under
failure-free delivery through a costly serial bottleneck, not an acceptable
hot-key throughput result by itself.
Increasing BRIEF2's retry budget from four to sixteen only raises completions to
13; snapshot wait reaches 20. Every policy completes the unrelated regional cohort.

**Compatible work can avoid paying coordination once per contribution.** For
completion-only increments, fixed arrival windows combine identical cross-shard
footprints while leaving local operations ungrouped. At window 128 / cap 32:

| Backend | Hot requests complete / failed / pending | Hot p99 | Regional p99 |
| --- | ---: | ---: | ---: |
| Arbitration | 128 / 0 / 0 | 202 | 1 |
| Wait-die | 128 / 0 / 0 | 191 | 1 |
| Fixed known | 128 / 0 / 0 | 306 | 1 |

The fixed run finishes at tick 513 through five distributed program executions.
This improvement is shared by the old policies; it is evidence for the additive
contract, not a unique advantage of fixed positions. At window 25 / cap 8, old
arbitration and wait-die finish all 128, while fixed known reaches 94 with 34
pending. Blanket windows applied to local work would add avoidable latency;
the cross-shard-only controls preserve local-hot and disjoint-WAN traces exactly.
Earlier snapshot-wait grouping experiments still suffered conflicting-group
failures. Combining deltas does not repair every underlying contention policy.

The oracle expands every original contribution, checks complete serial blocks,
and includes window delay in latency. Per-request intermediate values and
conditional effects are ineligible. Costs are modeled, and total cost of a fully
drained grouped run cannot be treated as equal-work throughput against a partly
finished ungrouped run. These fixed windows demonstrate potential; an actual
epoch fold must preserve the same complete observable semantics.

**Ordinary costs and locality must remain visible.** All original policies finish
200 hot local increments at p99 2. Uncontended cross-shard p99 is 26 for the old
three policies, 46 for BRIEF2/snapshot wait, 58 for known-write admission, 48 for
output wait, 81 for output refresh, and 80/102 for fixed known/discovered. All 128
complete. The new harness explicitly charges remote bound negotiation and its
protocol exchanges. Moving the old policies' known-output reservation later raises
their p99 to 38. These are model-specific costs, not inherent performance ratios.

Two disjoint WAN transactions with 200-tick round trips coexist with 2,000
regional writes near nominal service capacity: all eleven policies preserve
regional p99 1. At lower capacity, the original comparison gives p99 17 versus
16 with WAN requests removed. However, in the *overlapping* mixed-WAN fixture,
fixed known finishes just 16/96 local hot writes by tick 600; the remainder queue.
Even after draining, their p99 is 3,255. Shared output gates localize interference
to conflicting work, but a wide queued group can delay a small writer on one of
its currently free keys. There is no claim that all small transactions remain
fast, or that simulated installation proves locality of actual epoch publication.

## The candidate that removes renewal

The fixed-position experiment is a different design, not another BRIEF2 retry
knob:

1. Acquire write-only gates for complete output coverage, in canonical participant
   order. Partial ownership creates **no read-blocking position promises**.
2. After all gates are held, reserve position bounds at outputs, choose one
   position, and publish that exact position everywhere. Await publication replies.
3. Read and compute at that position. Register each read's position before waiting;
   later writers must go after it. Reads may await earlier unfinished outputs.
4. Deliver and acknowledge the actual final values, decide once, and resolve all
   promised coverage, including conditional outputs that produce no write.

There is no post-computation source renewal. An earlier fixed writer cannot wait
for a later fixed writer's value. A temporary reservation can still block a read,
so completing position metadata must never require executing the blocked program.
The authored conditional histories cover write skew, quota checks and no-write
business outcomes; disjoint output gates alone would not establish those results.
[Model boundaries](MODEL.md#fixed-position-execution) state the assumptions and
remaining composition obligations.

This bargain is closer to **BOHM's preallocated multiversion execution** than
Spanner's read/write transactions' retained read locks. BOHM knows write sets, including index entries,
while allowing reads to be discovered during execution. Its shared ordering and
batch barrier differ materially from the per-output gates, participant bounds
and read stamps tested here. Its results do not prove this distributed variant.
Source: [Faleiro and Abadi, §§3.1–3.3](https://www.cs.umd.edu/~abadi/papers/rethink-mvcc.pdf).

**Stable output coverage is a substantive restriction.** The actual maximum-row
probe moves the winning row between discovery and admission: narrow rediscovery
can fail four times in four attempts, while admitting the whole collection blocks
otherwise harmless writes. Even `UPDATE row SET v = v + 1` has changing coverage
when maintaining a secondary index: the primary row stays the same, but the old
and new index entries change. Packing them into a stable physical page does not
make affected logical predicates stable.

A separate repair probe resolves the failed attempt's promises, retains its
write-only gates, and *tries without waiting* to acquire newly discovered outputs.
For the indexed increment, retaining the primary gate stabilizes its value: one
extra execution succeeds, holding four keys for three actual writes. A report and
a disjoint writer still finish between attempts. If expansion is denied, every
gate is released and the transaction restarts. This is a real possible repair,
but adds ownership across attempts and abort/recovery obligations; it does not
establish progress for a moving maximum or arbitrarily expanding extension output.
It is not silently included in the timed candidate.

## Sensitivities and remaining limits

The original sweep spans three seeds and three widths; follow-on admission and
fixed-position runs use three seeds, with targeted wide, capacity, link and drain
contrasts. Width 256 increases actual service demand, not just protection scope.
At the original horizon, snapshot wait finishes only 11–13/128 fresh reports;
with a fourfold horizon it drains all 128, p99 963 for seed 7. Fourfold capacity
instead gives p99 122. Waiting cannot manufacture service capacity.

The capture deadline is an **attempt-age cutoff checked only when a read blocks**.
It includes admission and previous exchanges; an unblocked read can succeed after
that age. At 200-tick round trips, fixed execution with the default cutoff can
ultimately fail 112/128 reports. A 2,000-tick cutoff and longer drain completes
all 128. Zero renewal failures therefore does not mean unconditional progress.
Real deterministic execution must derive admission, position assignment and
timeout/retry decisions from agreed inputs, not unrecorded arrival or wall-clock
races. Different serializable outcomes are not sufficient for replica agreement.

The model does not price old arbitration graph CPU or wound/die decision traffic.
Old protection policies have uncapped restarts; the new optimistic policies
default to four whole attempts, while output admission has no waiting deadline.
Longer horizons and retry-budget contrasts expose those different policies;
failure counts alone are not an equal-patience comparison.
It charges row work at the coordinator, not distributed scans or payload bytes.
Source-only late cancellation is simplified; output promises persist through
participant abort delivery. History has no garbage collection; held-key time
omits nonexclusive read metadata and queued-waiter priority. Exact event replay
is not a proof of epoch confluence, recovery or distributed liveness.

For composition, pending transactions must remain continuations while unrelated
enabled work reaches the epoch's logical fixpoint. Position metadata and abort
resolution must progress independently of the data programs they unblock. Exact
extension bytes, Firecracker output verification, PLP dissemination and witness
frontiers remain constraints: if their visibility dependency stalls an entire
shard or holds back the necessary position metadata, the locality/progress result
here does not transfer. This comparison neither redesigns those mechanisms nor
assumes that serial replay proves their compatibility.

## Evidence

[Cohort outcomes](evidence/contention-comparison/cohorts.csv),
[run counters](evidence/contention-comparison/runs.csv) and
[provenance](evidence/contention-comparison/study.json) retain all compared
outcomes, including failures and pending work. The selection contains 1,783 model
runs across the original and follow-on series; repeated controls are included,
not treated as independent statistical samples. Separate
[moving-target](evidence/contention-comparison/discovery.json),
[invariant](evidence/contention-comparison/invariants.json) and
[index](evidence/contention-comparison/index-discovery.json) probes retain 27
authored semantic histories. All replay checks pass, as do 87 scheduler/core
tests and the fold adapters' embedded checks. Full run payloads and source
snapshots are recoverable through the evidence bundle.

The evidence supports removing broad source exclusion, retaining a waiting
snapshot for reports, and seriously considering execution at an already fixed
position for work with complete output coverage. It rejects finite retries as
the general hot-write solution. Compatible folding can remove a large amount of
hot coordination under several policies. The study does not yet justify making the fixed-output
bargain the default for unrestricted SQL and extensions, nor importing every
repair above into one increasingly complicated mechanism.
