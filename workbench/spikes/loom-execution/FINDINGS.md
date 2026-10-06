# Loom execution findings

Loom needs three paths: ordinary native execution over qualified bytes, a small
local ring for independent memory dependencies, and ordinary tasks for resource
or Port waits. Current Ikea composition supports that split. Turning every
kernel stage into a microtask loses useful fusion and spends scheduling work
where the consumer already streams or stays hot.

This is a same-host prototype and design recommendation. Engine and Shore call
Loom; Engine supplies legal pipelines to Ikea, and Ikea drives their native
regions through Loom. Loom calls Orbital. Orbital supplies actual resources,
qualified bytes, stable lanes and completion lifetime; Loom chooses placement,
admission and service policy.

## Context and evidence

The [Loom objectives notebook](../../notebook/loom-objectives-and-architecture.md)
and [Ikea integration](../ikea-composition/semantics-and-integration.md#loom-resources-progress-and-retained-state)
already distinguish native work, short DRAM rotation and longer retained waits.
The earlier [physical brief](../../notebook/orbital/reference/PHYSICAL.md#loom-bindings-and-allocation)
keeps scheduling with Loom. The [current candidate](../../design/orbital/DESIGN.md)
replaces historical Controller/architecture sketches; it is declaration-only.
Calico's [AMAC findings](../../../../calico/loom/report/AMAC.md) are a comparison
input, not a SixDB ABI, ring width or resource policy. The current consumer
captures include October Ikea changes; September CPS timings do not describe
these binaries.

Source cuts are retained separately:

| Campaign | Purpose | Evidence |
| --- | --- | --- |
| Initial native | Broad ring/grain/stream, topology, lock, join and service comparisons | [Zen](evidence/20261006-native/zen), [V2](evidence/20261006-native/arm) |
| Wide ABI | Ten independent scalar arguments, two pointers and 0/2/8/12 live vectors; preserve_none compiler rejection | [V2](evidence/20261006-abi/arm), [Zen rejection](evidence/20261006-abi/zen-rejected) |
| Confirmation | Fixed hint classes, typed native control, hot-path audit, complete observers, matched receiver credits and measured join reuse | [Zen](evidence/20261006-confirmation/zen), [V2](evidence/20261006-confirmation/arm) |
| Final service | The mixed-service caller also specializes its no-hint/first-line body | [Zen](evidence/20261006-service/zen), [V2](evidence/20261006-service/arm) |
| Ownership/progress seams | Reusable two-parent inventories, short jobs, cancellation and physical output drain | [Zen](evidence/20261006-extensions/zen), [V2](evidence/20261006-extensions/arm) |

Initial native evidence has 1,395 Zen and 1,056 V2 verified timing rows;
the first V2 ABI cut has 96. Each case has three shuffled repetitions, using
Release Clang 21.1.8, `znver5/zen5` or `armv9-a+sve2+simd/neoverse-v2`.
`report.py` summarizes a metric without combining different machines or cases.
Tables below use medians; retained rows preserve every repetition and counter.
Confirmation has 642 Zen and 552 V2 rows, all verified. Its fixed-demand message
cuts additionally require every planned identity to be accepted and completed.
Final service has 36 Zen and 18 V2 rows, all verified.
The focused extensions add 138 Zen and 117 V2 verified rows. Each machine's
captured source remains unchanged through its run; native checks and selected
assembly accompany the timing rows.

The C8a.8xlarge exposes 32 separate physical cores, four 32 MiB LLC domains and
one guest NUMA node. C8g.4xlarge exposes 16 physical cores, one 36 MiB LLC and one
guest NUMA node. Both report 64-byte lines. Hardware receipts record allowed
CPUs, siblings, cache groups and pair placement. These measurements establish
same-LLC and cross-LLC behavior, not cross-NUMA or cross-host behavior. Spot
workers run the helper's five-second interruption observer. New instances do
not prove cold hardware caches; each fixture constructs inputs without cache
eviction and verifies them after timing.

## A few nanoseconds is the inner budget

The quoted generic `issue_prefetch` remains an explicitly measured legacy
control. It is unsuitable as the proposed per-yield API: policy/null/length
tests, variable page loops and per-hint counter writes spend an appreciable
fraction of the budget before useful work.

`prefetch.h` defines 16 approximate desired hint patterns. Their bodies are
fixed unrolled instruction sequences. Qualification proves the address windows
and page geometry once and can downgrade a requested class before native entry.
A known-class callback inlines those hints; a separately measured deferred-class
path uses a prebound runtime dispatch. Hint counts are derived after timing.
Counts describe instructions issued, not cache hits, occupied MSHRs or hardware
stream entries. Hardware/resource-pressure selection belongs at a composition
or owner-service boundary; this spike does not implement an adaptive controller.

The audit applies beyond prefetching:

- A preallocated vacant-home stack removes enqueue's search for a null slot.
  Ring and overflow cursors increment and reset without runtime division.
- Active yields save only continuation arguments. The initial dependency is
  retained for overflow admission; no dependency descriptor is copied each yield.
  Visit totals accumulate on spin return instead of adding a second hot store.
  Retirement and due clock samples tail-transfer to separate cold helpers, so
  their ordinary calls no longer create a frame on every fast yield.
- A typed one-node chain control removes the generic workload/grain/optional-hint
  tests, with matching direct, shared, manual and ring arms.
- Queue indexing uses a prebound mask or exact reciprocal with one correction;
  arbitrary logical capacity remains available for equal-credit comparisons.
  Group membership/coordinator maps are bound before service.
- Message counters occupy separate 64-byte slots on these measured machines;
  known non-coordinators avoid out-of-line no-op route calls.
  Send wrappers borrow the packet and copy it only into accepted queue storage;
  refusals leave all caller fields unchanged, with no eager payload copies.
- Full and sampled latency storage is reserved and touched before timing. Full
  observation retains all completed samples, with actual pauses and per-source
  completion counts. Owner service has a separate reduced gap observer.

The dispatcher still tests completion, live work and its quantum, stores/loads
live values, and scans vacant slots when underfilled. These are prototype costs
to compare against direct work; a `musttail` annotation does not make them free.
Ordinary message service has atomics, packet copies, queue/family dispatch and
validation. It has a different budget from a local native continuation.

The frozen typed warm chain measures whole native work plus switching:

| Scalar, 16 slots; ns/visit median [min,max] | Zen | V2 |
| --- | ---: | ---: |
| Direct, no hints | 1.177 [1.175,1.181] | 2.154 [2.152,2.155] |
| Manual AMAC, no hints | 1.267 [1.260,1.273] | 1.900 [1.885,1.941] |
| Ring, no hints | 1.781 [1.761,1.786] | 3.094 [3.094,3.144] |
| Direct, inline first-line hint | 1.154 [1.152,1.163] | 2.170 [2.160,2.191] |
| Ring, inline first-line hint | 1.895 [1.887,1.934] | 3.322 [3.304,3.373] |
| Ring, runtime first-line dispatch | 2.482 [2.481,2.493] | 4.022 [4.021,4.053] |
| Ring, legacy generic first-line helper | 2.773 [2.771,2.780] | 3.949 [3.946,3.959] |

This is 8 KiB of inputs, 16 jobs, no overflow, two heterogeneous callbacks,
4,194,304 visits, count128 quanta and zero clock samples. The ring-minus-direct
whole-arm median difference is 0.604/0.940 ns without hints and 0.741/1.152 ns
with the inline hint. These are not isolated instruction latencies. Adding two
live vectors gives 2.357/3.643 ns/visit with the inline first-line hint, including
their actual arithmetic and 32 additional bytes of saved state. The generic
workload body also costs another roughly 0.7–1.0 ns over the typed legacy control.
The few-ns result applies to this qualified native shape; it is not a promise
for every payload, ring occupancy, hint pattern or workload.

The retained `codegen.json` records actual fixed-class bodies and callback/yield
instruction counts from each native binary. All 16 fixed bodies have exactly
0/1/1/1/2/2/4/4/8/8/3/5/3/5/9/17 hints, zero conditional branches and zero calls.
Fast scalar/vector yield has no ordinary calls or runtime division. Independent
native inspection also verifies no stack adjustment on those fast paths.

The wider ABI probe retains ten scalar values plus two pointers and 0/2/8/12
vectors. It demonstrates architecture-specific benefits and limits:

| Live vectors; whole ring ns/visit | Zen ordinary / regcall | V2 ordinary / preserve_none |
| --- | ---: | ---: |
| 0 | 5.088 / 4.244 | 5.594 / 4.347 |
| 2 | 5.892 / 5.539 | 7.379 / 6.034 |
| 8 | 8.713 / 8.535 | 11.754 / 9.843 |
| 12 | 11.616 / 11.279 | 17.373 / 17.969 |

Direct/manual controls are practically unchanged between conventions. V2's
12-vector ranges overlap; preserve_none does not win there. These numbers include
all payload arithmetic and slot transfers. Full-payload x86 preserve_none fails
pinned Clang register allocation for the indirect musttail target; the diagnostic
is retained, with no timing point and no payload shrink to conceal the failure.
Private families need actual compiler/codegen validation before selecting a
calling convention; a universal public closure ABI does not follow.

## Choose useful dependency boundaries

The initial random-chain first-line ring32 is 7.485 ns/node on Zen and 10.268
on V2, versus serial 131.861 and 119.922. This is whole useful native work plus
dispatch over independent chains, not the cost of one branch. Forcing 4 or 16
dependent nodes into one region loses overlap: Zen ring grain4/16 is
39.544/68.952 ns/node. Fewer switches can be slower when they remove useful stops.

The actual SeriesPack local population directory plus current Bec256 pair
consumer has the opposite result:

| Initial first-line path | Zen ns/body | V2 ns/body |
| --- | ---: | ---: |
| Serial staged native | 79.118 | 118.008 |
| Ring16 | 85.040 | 126.528 |
| Ring16, both bodies and queries hinted | 86.459 | 130.508 |

The wider hint path issues 2.5 times as many hints without a gain here. This
does not condemn rings for every packed-codec consumer; it rejects forcing this
consumer through more scheduling by default. Its initial hinted population
window is a nine-byte tile; the qualified point resolver also reads length,
checkpoint and prefix metadata. Body allocation has the existing initialized
64-byte read suffix. Prefetching additional addresses cannot grant access to
padding. This is a staged directory point resolver plus compiled `consume_pair`,
not a full incumbent `bind_count`/CPS pipeline measurement.

Streams favor larger useful native regions. For 64 KiB ranges, ring16 with no
hints falls from 5.104 to 0.990 ns/line on Zen and 6.525 to 1.712 on V2 when the
grain changes from one to 1,024 lines. This changes 131,072 visits into 128.
Increasing independent 64 KiB streams from 16 to 64 instead slows Zen from
5.176 to 9.909 ns/line and V2 from 7.028 to 10.140; serial is 2.115/3.105.
Adding 64 bytes to the stride does not consistently cure the cliff. Neither
result identifies the exact capacity of a hardware prefetch tracking table.

Ring width, native grain, hint volume, outer quantum, admission inventory and
hardware outstanding misses are distinct. Manual AMAC refills a finished lane
immediately; this ring promotes overflow and admits more work between spins.
Underfill and overflow results include that choice, not only ABI overhead.

Confirmation keeps that distinction: the typed inline first-line chain ring is
6.485 [6.021,7.165] ns/node on Zen and 12.172 [11.666,12.314] on V2, versus
direct 129.021/130.385. V2's L2-one class reaches 8.506 [8.406,9.022]; Zen's
matched L2 case is noisy. Requested multi-line classes on 64-byte chains
downgrade to one line and establish no wider-hint benefit. The current staged
Bec direct/ring remains 82.045/87.578 ns/body on Zen and 117.464/126.602 on V2.
Fixed policy simplification does not reverse the useful-grain choice.

For qualified 16-line streaming regions, no hints / L1-one / L1-two / hybrid-three
are 1.428/1.345/1.558/1.371 ns/line on Zen and 2.640/2.504/2.844/2.588 on V2.
More hints do not monotonically improve throughput. Page8 at grain1 qualifies
down to L1-one; grain1024 retains its 17 hints. Legacy page volume differs,
so those timings cannot isolate dispatch overhead alone.

## Service the home between bounded regions

The mixed fixture performs actual random memory work while a peer sends paced
requests and receives causal acknowledgements. Request timestamp starts at the
first offer, including refusals. Live-ring and post-ring latency cohorts stay
separate; all accepted identities must complete.

| Initial same-LLC count quantum | Zen ring wall ms / live RTT p99 µs | V2 ring wall ms / live RTT p99 µs |
| --- | ---: | ---: |
| 16 visits | 40.618 / 0.950 | 36.837 / 1.105 |
| 128 visits | 34.734 / 4.470 | 31.155 / 4.254 |
| 1,024 visits | 33.789 / 34.970 | 30.552 / 29.442 |
| 8,192 visits | 33.820 / 265.480 | 30.382 / 235.670 |

A coarse 5 µs time check caps typical long-quantum delay near 5–6 µs here, while
adding work. Zen128 ring wall becomes 41.394 ms with the time cap, about 19%
above the count-only case. Its maximum service gap can still be tens of µs:
a callback, clock-check interval and OS preemption can overshoot. This is a soft
budget, not a hard deadline. Use a count quantum for proven short native regions;
time checking or smaller safe regions is needed when duration is uncertain.
The observer, sender pacing and occupied peer CPU are included fixture costs.

The final service cut uses the specialized first-line callback. Full observation
reads the owner clock at each service boundary; reduced observation samples the
failure deadline every 64 boundaries and leaves service-gap measurement disabled.
Both retain paced sender timestamps and every causal RTT sample.

| Final same-LLC quantum | Zen ring wall ms, full/reduced | Zen live RTT p99 µs, full/reduced | V2 ring wall ms, full/reduced | V2 live RTT p99 µs, full/reduced |
| --- | ---: | ---: | ---: | ---: |
| 16 visits | 27.409 / 24.165 | 0.750 / 0.630 | 27.613 / 25.113 | 0.981 / 0.968 |
| 128 visits | 21.034 / 20.397 | 2.880 / 2.710 | 23.247 / 22.733 | 3.267 / 3.324 |
| 1,024 visits | 20.571 / 20.041 | 19.560 / 19.890 | 22.373 / 22.186 | 21.755 / 21.789 |

Each completes 1,000 accepted ACKs and 1,048,576 native visits. Frequent clock
observation has a visible whole-fixture cost. Live cohorts are classified at
owner receipt and their identities/counts can differ between runs. Reduced
rows explicitly mark service gaps unmeasured; a zero there is not a zero gap.
The initial/final difference also changes source and cannot isolate one fix.

## Channels, worker groups and coordinators

Dense directed SPSC allocates O(n²) queues and scans all incoming peers. Sparse
active SPSC reduces storage/scans when traffic really has fewer edges. A bounded
per-worker MPSC inbox concentrates producer contention and receiver credit.
Group-direct retains local edges and routes inter-group packets through
coordinators; group-routed adds that route for all traffic. A scan stops at its
first successful receive; sparse all-to-all still has O(n²) active edges.
Coordinators retain
blocked forwarding state, preserve identity and cannot drop accepted packets.

Initial matched retry fan-in completes every offered identity:

| Variant | Zen32, 320k completed: wall ms | V2-16, 160k completed: wall ms |
| --- | ---: | ---: |
| Dense SPSC | 38.452 | 6.885 |
| Sparse SPSC | 40.792 | 6.356 |
| Inbox | 204.878 | 98.679 |
| Group-direct | 64.237 | 14.464 |
| Group-routed | 61.507 | 13.421 |

These initial cases give each queue 64 entries, so aggregate receiver credit is
unequal. They motivate confirmation controls, not a universal transport winner.
The receiver also produces in this initial cut; confirmation uses a dedicated
receiver. Cross-campaign changes include that workload and observer change.
In all-to-all retry, Zen dense/sparse finish 320k in 4.478/4.268 ms versus inbox
7.940 and groups 10.607/11.570. Additional hops and coordinator pressure remain
visible. A one-attempt group case can finish earlier by refusing most offers;
its offered-batch time is not an equal-work win.

Zen32 empty dense service retains 1,024 queues and about 3.37 MB; sparse active
uses 64 and 0.214 MB, inbox 32 and 0.121 MB. All consume about 3.19 CPU seconds
over 101 ms of spinning. Fewer probes do not themselves make workers sleep.
Padding changes several contended cases but is not universally beneficial;
initial one-attempt pressure comparisons also accept different work, so they
do not isolate a false-sharing speedup. Storage and cache footprint belong in
that decision. Initial full observers truncate latency storage to a prefix;
their tails and timing are excluded from complete-observer conclusions.

Groups describe placement and service domains, separate from Allocation Group
inventory and shard authority. A coordinator can aggregate wakeups, request
batches, credits or placement decisions without executing every native unit.
Bind a frequently used peer/route before a batch rather than paying routing
lookup for every item. Select direct edges for hot traffic and bounded group
routing when its footprint or aggregation repays the extra hop. Neither O(n²)
everywhere nor one global coordinator follows from this spike.

Confirmation matches receiver credits: eight slots per incoming SPSC edge versus
an inbox of `8 * (n - 1)` slots, with a dedicated receiver. Every planned identity
is accepted and completed. Total topology storage and admission policy still
differ; equal receiver credit does not make them equivalent.

| Dedicated retry fan-in | Zen32, 310k completed: wall ms | V2-16, 150k completed: wall ms |
| --- | ---: | ---: |
| Dense SPSC, eight slots/edge | 49.728 | 17.122 |
| Sparse SPSC, eight slots/edge | 49.976 | 15.060 |
| Inbox, matched receiver credit | 186.379 | 66.278 |
| Inbox, eight slots total | 123.305 | 67.793 |
| Group-direct, eight slots/queue | 67.258 | 21.674 |
| Group-routed, eight slots/queue | 62.080 | 26.142 |

The Zen matched inbox records 5.141 million CAS failures and 5.912 CPU seconds;
sparse uses 1.551 CPU seconds. Increasing the shared pool does not cure producer
contention here. Sparse fan-in retains 31 queues/22,832 bytes on Zen and
15 queues/11,184 bytes on V2; the matched inbox topology retains
32 queues/451,104 bytes and 16 queues/110,880 bytes. Group ingress, held
forwarding and egress inventory have different credit boundaries.

With an actual 5.058–5.062 ms receiver pause, all identities still drain.
Zen sparse p99 is 51.740 µs while its maximum is 5.114 ms; matched inbox p99 is
127.950 µs and maximum 5.176 ms. P99 hides this rare initial stall. Under 90%
all-to-all skew, Zen sparse/inbox complete in 38.001/151.936 ms with p99
279.4/119.35 µs. The inbox's lower delay coexists with lower work rate; this
is not a matched sustained-load fairness result. Final per-source completion
counts establish eventual drain, not temporal fairness.

Complete observation retains all 40,000 samples; every-64 sampling retains 626,
with zero dropped samples. Same-LLC inbox wall time for sample0/64/1 is
0.758/1.238/2.250 ms on Zen and 1.524/2.601/3.789 ms on V2. Full storage is
320,000 bytes versus 5,008 sampled bytes. Clock reads, sample stores and changed
producer pacing are whole-observer costs, not an isolated clock-read latency.

## Locks and large operations across affinity seams

TTAS/backoff, MCS, useful local polling and actual owner request/ACK transport
have different throughput, fairness and CPU costs. At 31 Zen writers, backoff
delivers 18.917 M acquisitions/s with sampled p99 181.31 µs; MCS delivers 7.765
with 5.5 µs and much more even progress. On V2-15, MCS leads backoff throughput
6.057 versus 4.070 M/s and sampled p99 2.832 versus 1,147.493 µs. These are
short protected counter regions, not arbitrary task critical sections.

Owner transport includes another active CPU and a real request/completion round
trip; it is not a lock instruction or a ready continuation. Useful polling
adds independent preallocated arithmetic but can still expose sampled maxima
of 72–100 ms under contention. Pooled p99 can hide a single stalled cohort;
per-writer progress, maxima, polls and actual holder pauses must accompany it.
Do not invoke arbitrary unqualified work while holding a lock or awaiting its
owner; admitted independent work needs bounded ownership and safe stops.

The join fixture hashes a real immutable duplicate-key build, probes it,
materializes every match through bounded output pages and checks against an
independent sorted-key oracle. It compares shared remote access, an actual
retained partition request/ACK to the data owner, one copy and per-worker
replicas. Qualification is supplied before the native region, and all physical
uses join before retention ends.

For a 72 MiB uniform build across Zen LLCs, initial shared probing takes 20.204
ms versus owner 17.624. Copy probing takes 19.641 plus 17.162 ms copy setup, so a
single use loses. Skewed hot keys favor owner locality more: shared 10.351 ms,
owner 6.978, copy 8.328 plus 16.890 ms setup. Early sums of setup/native components
are not full wall time; confirmation records real construction-through-join
wall time and actual successive uses of retained builds/copies.

The separate 18 MiB scale-out fixture is not a continuation of that 72 MiB curve.
Zen shared uniform probes at 8/16/32-CPU placements (7/15/31 probe workers)
take 1.069/1.048/1.051 ms: more cores
add CPU without improving this wall time. V2 improves from 1.518 ms at eight to
0.932 at sixteen. Replicas spend substantial setup/storage and lose here.
Requests carry stable partition indices, not a network transfer of all probe
and result bytes. Key skew changes cache locality; duplicate-count controls also
change build key cardinality and collision density. The synchronous output hash
sink is not a slow external sink or a Port-credit/backpressure experiment.

Measured reuse changes the copy conclusion. No copy wins actual
construction-through-final-join wall time at 1, 8 or 16 uses in any tested
placement/distribution. A simple extrapolation from the first cold probe misses
how the retained shared build warms. For Zen cross-LLC:

| Distribution / uses; fixture wall ms | Owner | Shared | Copy |
| --- | ---: | ---: | ---: |
| Uniform / 1 | 64.283 | 64.224 | 83.270 |
| Uniform / 16 | 284.663 | 296.703 | 300.292 |
| Skew / 1 | 53.080 | 56.884 | 72.731 |
| Skew / 16 | 91.413 | 92.370 | 117.693 |

The uniform sixteen-use owner wall advantage costs 467.507 ms execution/routing
CPU versus shared 247.377. Construction CPU is outside these CPU counters, though
construction is in fixture wall. After the first use, skewed owner/shared native
means are 2.473/2.499 ms per use; their initial locality gap mostly disappears.
Copies retain 144 MiB versus the original 72 MiB. Threads are joined and recreated
each use, while build/copy/probe/output storage remains allocated.

The output controls produce exactly 245,760 / 983,040 / 3,932,160 rows at
duplicate counts 1/4/16. At d16 and 31 Zen probe workers, shared native wall is
1.424 ms versus owner 13.482; actual fixture wall is 17.775 versus 30.055.
Replicas reach 119.580 ms fixture wall with 576 MiB retained builds. At 15 V2
probe workers the d16 shared/owner fixture wall is 19.916/92.496 ms. This supports
legal parallel native partitions for larger work; it does not isolate output
row cost or establish a slow-sink policy.

Confirmation's actual 1 ms holder pause measures about 1.06 ms. MCS remains much
more even than useful polling: useful has minimum writer progress of only 64
acquisitions and sampled maxima near 100 ms, despite completing substantial
independent arithmetic. Pooled p99 hides that stalled writer. Single-writer
TTAS/owner controls are 8.257/185.030 ns/acquisition on Zen and 17.814/392.490
on V2, including protected work; owner also consumes a second active CPU.

## Reusable worker inventory and operation lifetime

`loom_inventory` keeps one worker inventory across successive pairs of real
parent allocations. Parents own their qualified 512-byte input and native job
state. It compares one shared scalar bank, two scalar parent-reserved banks and
separate scalar/vector family banks, with worker-wide admission credits. The
family-bank arm does extra vector work and retains extra state; it cannot isolate
storage placement or ABI cost. No owner/refcount/cancellation check enters the
native callback or yield. Selected scalar/vector callback and fast yield
disassembly has no ordinary calls or divides on either architecture.

Short jobs expose costs that the warm continuation panel amortizes:

| Shared scalar bank; ns/accepted operation median [min,max] | Zen | V2 |
| --- | ---: | ---: |
| One native visit | 15.356 [15.355,16.093] | 21.522 [21.046,22.384] |
| Two visits | 17.245 [17.192,17.637] | 24.851 [23.829,25.164] |
| Eight visits | 29.311 [28.380,29.558] | 44.024 [44.010,44.180] |
| Sixty-four visits | 122.468 [121.127,122.693] | 222.916 [222.073,223.098] |

These complete all 10,240 operations over 256 waves. Timing includes enqueue,
outer bookkeeping, native work, retirement and one first-B clock sample/wave;
parent construction and independent oracle preparation are excluded. The final
source skips no-hint admission preparation and no-op dispatch. The unchanged warm
shape measures 1.847 [1.779,1.880]/3.111 [3.096,3.126] ns/visit in this cut.
A few-ns warm visit is therefore not a few-ns full admission/retirement promise.
Keep tiny ready regions direct, and amortize admission over useful native grain.
An underfilled one-visit outer quantum costs 43.896/55.069 ns per two-visit job;
outer granularity matters even with the same native family.

The long-A/short-B fixture alternates outer parent visits but allows each visited
parent to fill remaining admission. Its 64 A jobs need 1,024 native visits each;
eight B jobs need two. Slots/overflow are 8/8 per bank, aggregate credits 16:

| Inventory / admission | First actual B outer visit, both machines | Scheduler bytes |
| --- | ---: | ---: |
| Shared / FIFO | 4,097 | 2,064 |
| Shared / reserve one B credit | 513 | 2,064 |
| Parent banks / FIFO | 1,024 | 4,128 |
| Parent banks / reserve one B credit | 2 | 4,128 |
| Family banks / FIFO | 1,024 | 4,768 |
| Family banks / reserve one B credit | 2 | 4,768 |

These are observed policy outcomes, not a fairness guarantee. Reserving aggregate
credit in the shared ring admits B into overflow; it does not reserve an active
home. Empty B storage also cannot help when A consumes every aggregate credit.
Bind admission and active-service policy together at the worker boundary rather
than adding a per-yield owner check.

Cancellation after outer visit 4 distinguishes physical lifetime choices. Shared
selective removal abandons 480 A frames, preserves all 256 B outcomes and destroys
A while B remains resident in all 32 waves. Whole shared discard also abandons
32 B frames, so only 224 B outcomes survive; it is a valid exclusive-inventory
operation, not a valid way to cancel just A. Draining instead physically finishes
all 736 accepted frames and suppresses cancelled A publication while retaining A
until completion. Independent scalar/vector checks destroy A's actual allocations
before B continues, including active and wrapped overflow survivors.

Selective cleanup is an optional private typed owner-boundary scan, not a public
closure/ownership ABI. Removing 8 of 16 populated frames takes 35.684/65.781 ns per
boundary; removing 64 of 128 takes 159.805/332.556 ns. Both include timer overhead
and survivor promotion, excluding construction/admission/final drain. Parent or
family banks allow whole-bank cleanup without scanning other parents but retain
more physical storage. An exclusively borrowed typed bank followed by drain/reuse
remains the simplest cut when simultaneous same-family sharing is unnecessary.

## Output pressure and physical completion

`loom_pressure` reserves actual bounded writable output before native entry.
Its callback transforms a qualified input into that span, then returns at a
native grain boundary. It has no output-credit wait branch. An owner cursor
stops when the output pool is exhausted; a peer consumes each span through real
reads. Reuse requires both physical consumer completion and owner settlement of
the captured generation receipt.

With reserved receipts, every normal case accepts and physically retires all 64
outputs, including one/two/eight-buffer pools and zero/100 µs consumer holds.
For two buffers and a 100 µs hold, same-LLC wall time is 6.441/6.455 ms on Zen/V2;
the serial consumer hold dominates. There are 62 capacity stops; receipts settle
while work is full 33–39 times on Zen and 11–25 on V2. The pool is 4,112 bytes and
its two receipt cells are 128 bytes. Eight buffers use 16,448 output bytes and 512
receipt bytes without improving this serial-consumer bound. Zero hold takes
0.046/0.064 ms same-LLC; Zen cross-LLC takes0.147 ms. These are whole fixtures,
including busy waits, clocks, receipt scans and repeated volatile/oracle reads.

The finite negative puts workload and ACKs in one FIFO whose dequeue is gated by
free output credit. It accepts four jobs, starts/submits two, then cannot settle
the ACK needed to restore credit. After observing the cycle it cancels, disposes
the two accepted unstarted jobs and physically drains both outputs, with zero
unfinished work or forced retirement. The separate wide-FIFO check also traps an
already-published ACK behind work: 64 accepted, one started/submitted, one queued
ACK and 63 unstarted jobs disposed. This shows a specific credit-coupling failure,
not that every unified queue must fail.

Cancellation during physical use retains both submitted buffers through actual
post-cancel reads and receipt settlement; their physical drain is about 2.03 ms
for sequential 1 ms holds. The authored mixed check cancels one still-pending
native frame while retaining one physically used submitted buffer: accepted 4,
started 2, submitted/retired 1, native-abandoned 1, disposed-unstarted 2. Both native
architectures pass all eight pressure checks, with no unfinished work or forced
retirement. Logical cancellation is not permission to reuse a submitted span.

One reserved receipt per retained buffer suffices for this concrete protocol.
No universal resource coordinator or per-hop lifetime reference is needed.
The fixture supplies real qualified spans and generation receipts; it does not
implement the candidate Orbital Port/View admission/protection coupling.
Existing [recycling](../orbital-views/recycling.cpp) and
[binding](../orbital-views/systems/binding_probe.cpp) probes inform borrower and
home-settlement ordering, but their auto-growing pool/allocating inbox do not
prove bounded output admission or reserved-return progress. Keep that residual
explicit when applying the ownership cut to the declaration-only candidate.

## Minimum viable Loom recommendation

Start with a worker assigned to a stable Orbital lane, a bounded ordinary task
queue and home completion service. Run ready qualified native regions directly.
Keep Ikea's useful native grain and Engine's legal partitioning separate from
scheduling boundaries. The embedded application lane can use the same direct
path without a mandatory worker round trip.

Add a small preallocated per-worker memory ring with bounded overflow, uniform
private callback families and explicit acceptance/refusal. Enqueue supplies the
callback, live arguments, qualified dependency address and a previously selected
hint class. `yield` tail-transfers within that family. Worker service spins the
inventory for a bounded quantum, then services home work. Size the active ring
from measured useful overlap and interference, rather than treating a measured
width as a hardware limit. Retain only state crossing the chosen stop.

Keep physical inventory reusable independently of an operation's lifetime.
Ikea/Engine own the concrete native family, cursor and qualified-byte borrows;
worker storage owns slots, credits and service. An exclusive typed bank can
drain and be reused by the next operation. Same-family sharing may use a cold
selective release or drain; it need not add ownership tests to every yield.
Whole-inventory discard requires exclusive cancellation ownership. Different
native families can use separate banks without a mixed closure ABI.

Qualification, version/lifetime retention, class/pressure choice and allocation
stay outside each switch. Prefetch is advisory: no readiness event is implied.
A Port dependency or unavailable output capacity leaves the short ring for an
ordinary retained Task. Cancellation does not release bytes still used by the
backend or another worker; physical drain/ACK settlement precedes reuse.
Keep reserved physical completion progress independent of workload admission.
A concrete output protocol can reserve one receipt with each retained span;
it does not require a universal resource coordinator.

Use explicit bounded worker channels and per-domain placement information.
Hot direct paths can coexist with group coordinators. Reserve receiving/output
credit before production and retain it through the actual required settlement.
Split large work over admitted partitions; decide owner execution, remote shared
reads or copying from the operation's reuse, skew, transfer and output costs.
Worker placement cannot change shard authority or deterministic meaning.

Loom owns placement, residency, reservation policy and outer service. Orbital
owns actual backing geometry, protection, accounting and physical retirement.
Hot workload channels belong with their Loom placement/service policy; a
retained physical receipt belongs with the resource mechanism that guarantees
its lifetime and settlement. Reusing a completion inbox is not a reason to
funnel ordinary data traffic through it. Keep private native families below
ordinary Task admission and remove wrapper layers that only forward this state.

This supplies a concrete first implementation boundary without committing a
universal closure ABI, adaptive resource model, work-stealing policy, buffer-pool
victim policy or cross-host transport. Those choices need real Engine/Shore
operations and Orbital resource/retirement paths; this fixture cannot select them.
