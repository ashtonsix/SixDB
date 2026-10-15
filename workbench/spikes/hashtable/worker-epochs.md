# Coordinating private hash-table owners

The existing two-writer measurements use fixed K40 word aggregation and queues
that accumulate until a barrier. They do not establish coordinated growth,
bounded intake or broad sets/maps/multimaps performance. This note records the
next experiment's ownership and operation choices; it is not a product contract.

Each worker keeps a compact list or bucket hierarchy on its own Loom Worker and
Orbital Pool. The coordinator holds occupancy deltas and a small bounded set of
accepted batch tickets. A batch contains opaque values of the configured width;
the protocol must not interpret every payload as a machine word. Sets implement
union, maps use an explicitly supplied associative combiner, and multimaps
concatenate unordered values while preserving duplicates. An arbitrary overwrite
or delete history needs an ordering/version rule and cannot be called a reduction.

The first implementation prerequisite is a fused opaque map combine. Both
`small_table` and `promoted_table` can now apply a synchronous nonthrowing caller
combiner after locating an existing record, then publish normal byte damage and
occupancy deltas. Absent records take the incoming value directly. This avoids
requiring every reduction caller to perform get followed by set. `combine_bytes.cpp`
checks complete bytes, refusal before the callback, erase/reuse, retained clones,
overflow and reencoding; its first timing compares the fused path, double search,
and ordinary/exact-key Abseil. The initial diagonal key/value-width sample is not
the full Cartesian product. These are update-path measurements, excluding initial
owner setup/reservation and retirement, not complete worker lifetime scores.

The first local checks pass all eight domains at W1/2/3/7/8/16/32/64, plus the
opposite extremes K8/W64 and K64/W1. ASan/UBSan with stack-use-after-return passes
K8/W1, K8/W64, K40/W16 and K64/W64, including the timed history's complete oracle,
and the all-domain list-growth check. AArch64 final-binary inspection finds the
combiner's explicit vector XOR in the complete update roots; lookup/combiner calls
are fused, while cold allocation and failure/unwind calls remain.

The width-aware local comparison uses a larger primary index as values widen;
the earlier fixed 16-bucket diagnostic badly congested wide records and is not a
format recommendation. The current capacities remain experimental controls and
are oversized for some tiny populations. Seven-repetition medians at 251 keys:

| Key/value bits/bytes | Fused hierarchy ns/update | Double search ns/update | Ordinary Abseil ns/update | Logical bytes ours / Abseil |
| --- | ---: | ---: | ---: | ---: |
| K8 / W1 | 3.41 | 4.65 | 3.04 | 3,752 / 1,555 |
| K40 / W16 | 3.65 | 5.52 | 2.70 | 14,120 / 12,688 |
| K64 / W64 | 10.29 | 15.15 | 3.97 | 56,914 / 36,976 |
| K8 / W64 | 7.84 | 8.51 | 3.92 | 27,928 / 33,433 |
| K64 / W1 | 6.14 | 8.66 | 2.76 | 3,752 / 8,640 |

These are OrbStack development measurements of one first-insert plus 31 XOR
updates per key, shuffled, with the complete output checked. They are neither
serial latency nor native platform results. Exact-key Abseil and the list owner
are retained alongside every row in [the local comparison](evidence/combine-local/artifact.json).
The extreme-width follow-up is separate. Callback fusion improves this operation
but does not resolve wide-layout costs or establish worker competitiveness.

Intake credit, Task admission and allocation capacity are separate resources.
Loom currently provides stable caller-owned Tasks, bounded accepted-task slots,
capacity-change signals and Orbital physical Messages. It has no generic batch
credit or occupancy-balancing service. Use its existing private Message/ACK pattern
with completion progress reserved independently of workload credit; do not make a
new allocator or infer that a waiting Task releases its slot. Queue storage and
pending bytes belong in memory accounting. A batch's work budget is separate from
the selected eight-request hash-table packet interface.

For disjoint key slices, route mismatches through bounded exception batches;
backpressure must stop acceptance before either queue can grow without bound.
Moving a slice boundary is a charged reencode/copy into the destination Pool,
unless whole source Workers remain live. For overlapping instances, route each
accepted batch using current occupancy plus outstanding credits, and record work
imbalance separately: duplicate-heavy input can be cheap in occupancy yet expensive
to consume. Do not feed exact future union cardinality into this decision.

Overlapping instances use shared seeds and geometry within an epoch. A growth
request stops new admission, drains accepted mutation batches, prepares private
replacement indexes on both owners, and migrates each with bounded scan work.
Only after all owners are ready may the coordinator advance the epoch. Admission
failure/cancellation leaves the old owners usable; no caller may observe some
instances under the new interpretation and others under the old one. The existing
`hierarchy_migration` transfers each source's bag owner only on that same Pool.
Caller entropy supplies the next shared seed pair. Thresholds and geometry remain
measured policy inputs, with refusal, heavily skewed occupancy and delayed owners
included from the first runnable comparison.

`hierarchy_migration` now accepts compact-list sources as well as hierarchies.
Its bounded scan preserves rank on refusal, and each source retains its bag owner
until `finish()`. A coordinator can prepare all owners before consuming any of
them. Cancellation destroys only the private replacement index. This avoids full
source/bag clones merely to compensate for the consuming synchronous operation.

The all-domain local comparison passes sets and W2/W64 maps/multimaps, empty
sources, every-row refusal/retry, cancellation after a copied prefix, retained
clones and complete-value checks after commit. Actual Pool exhaustion also refuses
a second preparation and a later migration step; releasing pressure permits
continuation without replaying accepted records. The old all-domain hierarchy
migration regression passes. K8/40/64 pass ASan/UBSan with always-on stack-use-after-
return, including complete timed-history oracles. These checks establish the
ownership boundary, not a coordinated-worker result.

The first complete-lifetime comparison includes build, promotion, full-value
reads and destruction, with ordinary/exact-key Abseil from the first prototype.
It exposed a redundant duplicate search in synchronous promotion; the source is
already unique and the destination empty. Synchronous promotion now uses the
unique insertion helper too; an explicit checked control preserves attribution.
Prepared-vs-synchronous timing must use that equalized control. The complete
local panel has 11,340 rows and substantial timing variation, so it does not pick
a native performance winner. Native V2/Zen5/Granite comparisons now complete:
each has 18 processes, 22,680 rows and nine passing key/control checks, with both
process orders and seven repetitions of 64 complete lifetimes per timer.
Prepared coexistence is sampled at the ready boundary, not a memory high-water
measurement. Synchronous coexistence and baseline accessible backing are unknown
(`-1`); physical Pool reuse can change backing for equal logical contents.

Across 360 domain/kind/size/grain cells per host, the median prepared/synchronous
complete-lifetime ratio is 1.010 on Zen5, 1.006 on V2 and 1.006 on Granite.
Regressions above 5% occur in 19/24/18 cells respectively; V2's smallest work
budget accounts for 22 of its 24 regressions. Bounded preparation therefore has
a modest typical cost, with exceptions; it is useful when coordination needs
that boundary, not a blanket replacement for synchronous private promotion.
The cost of constructing and promoting these small owners remains much larger
than ordinary Abseil. K40, 128 keys and a 64-record budget, in ns per complete
construct/build/promote/read/destroy history:

| Kind / value bytes | Zen5 synchronous / prepared / Abseil | V2 synchronous / prepared / Abseil | Granite synchronous / prepared / Abseil |
| --- | ---: | ---: | ---: |
| set | 1,745 / 1,726 / 871 | 2,823 / 2,886 / 1,141 | 2,111 / 2,118 / 1,015 |
| map / 2 | 2,170 / 2,107 / 939 | 3,163 / 3,238 / 1,208 | 2,585 / 2,470 / 1,084 |
| map / 64 | 5,543 / 5,558 / 2,807 | 11,039 / 11,172 / 5,501 | 6,368 / 6,556 / 3,384 |
| multimap / 2 | 15,310 / 15,515 / 5,706 | 27,871 / 27,688 / 8,107 | 18,579 / 18,592 / 6,868 |
| multimap / 64 | 31,817 / 30,826 / 19,430 | 54,798 / 54,546 / 30,431 | 37,921 / 37,940 / 24,073 |

These deliberately force promotion at the specified cardinality; they do not
choose a production threshold. [Zen5](evidence/prepared-list-zen5/artifact.json),
[V2](evidence/prepared-list-neon/artifact.json) and
[Granite](evidence/prepared-list-granite/artifact.json) keep ordinary/exact baselines,
all work budgets and the redundant-search control. `prepared-list-report.py`
rebuilds the complete comparison from their raw files.

The bounded-intake experiment uses two actual Loom Workers with one accepted
batch ticket per owner and independent completion Messages. A Task is rebuilt
only after retirement and the completion Message's final release. Epoch changes
stop admission, drain those tickets, then prepare/commit every private owner.
Occupancy plus outstanding accepted work chooses overlapping routes; disjoint
routes use key slices. The baseline follows the same admission protocol but may
use its native reserve operation, rather than being forced to clone for a shared
encoding it does not need. First comparisons include complete ordinary-Abseil
histories and retain growth/routing costs separately. Merge and retained-reader
consolidation remain distinct work; intake alone cannot establish those wins.

The first caller covers uniform and skewed update populations, two owners and
a separately pinned coordinator. Batches contain at most 64 records; Tasks apply
eight per visit, with up to two accepted batches plus two bounded producer staging
arrays. It does not model worker-affine producers or a network routing topology.
Adaptive overlapping routing can assign different subsets to each contender's
owners as their completion timing changes. That is a measured policy outcome,
not identical private-table contents; final union/reduction/multiplicity is equal.
The scalar byte oracle combines outputs outside timing, and is not a merge path.

CSV `ns_per_update` includes routing, mutation and any coordinated changes.
`owner_lifetime_ns` adds Worker/Pool/thread setup and their drain/join, while
excluding the independent oracle and inventory. It is a fixture lifetime, not
the cost of a table inside an already-running Worker. Final table logical/backing
and bounded ticket/staging bytes are separate. The prototype's reserved inactive
owner/migration alternatives and Worker/Arena metadata are not charged to the
active table column; it is not a total-memory comparison or per-owner RSS/high-water.
Unknown baseline backing remains -1.
Initial geometry and the 64/128-key growth triggers are controls. Neither this
policy nor its one-ticket credit count is selected for production.

All 40 key/kind/W2/W64 local checks pass, including paired cancellation and full
byte oracles. Three boundary callers also pass ASan/UBSan/always-stack-UAR; the
K40/W2 map caller passes ThreadSanitizer. Loom's read-only review found no current
ticket-lifetime defect and confirmed final-release credit reuse. Future failed/
detached Task paths need their own retirement handling. Four diagnostic atomic
reference-count operations and one acquire load per accepted batch are charged;
they are not free protocol work. The first disassembly also exposed per-record
outlined caller lambdas, now forced into the chunk body before native comparison.
The exact previously qualified Orbital repair is included in the current worker
source. All 40 checks pass again after caller fusion, as do the three sanitizer
boundaries, and final binaries no longer call those per-record caller lambdas.
Cold allocation calls remain.
[First complete local caller histories](evidence/worker-epochs-local/artifact.json)
retain the measured losses and full
source/binary recovery. Native V2/Zen5/Granite comparisons now complete: 80 paired
processes, 3,200 timing rows and 40 domain/kind/width checks per host, with five
repetitions in both process orders. These are the first fixed-geometry intake
controls, not the final worker format or growth policy. In particular, they
force growth at 64 and 128 keys and retain the ordinary eight-byte inline budget;
wide multimap values therefore start external. The existing specialized external/
spill patch controls are off in this caller. The source, flags and losing cases
remain recoverable rather than being presented as the best attainable design.

Zen5's uniform overlapping control costs 15.44 ns/update for K40/W2 maps versus
private Abseil8.12, with active logical bytes28,240 versus69,696. K40/W2 multimaps
cost25.58 versus8.44 ns, with54,456 versus135,200 logical bytes. K64/W64 multimaps
cost88.19 versus17.10 ns and use1,158,719 versus1,000,448 active logical bytes.
Growth contributes25,050/28,660/50,591 ns respectively over8,192 updates; both
growth and the steady mutation portion need attention. Queue/staging storage is
4,744 B for W2 or19,080 B for W64, additional to the active table column.

The narrow-domain set exposes the wrong policy particularly clearly: at 240
input keys, forced overlapping promotion costs30.39 versus9.55 ns/update and
28,240 versus2,140 active logical bytes. K8 can never exceed the compact list's
256-key domain. These controls establish coordinated transition semantics and
their cost; they do not justify that transition in the natural K8 lifetime.
Preserving the compact regime, using occupancy and pending admission bounds for
growth, and coupling inline bytes with primary/secondary capacity are the next
policy comparisons. Reuse the existing patch controls as measured candidates;
do not interpret this first unpatched caller as the settled mutation path.

[Zen5](evidence/worker-epochs-zen5/artifact.json),
[V2](evidence/worker-epochs-neon/artifact.json) and
[Granite](evidence/worker-epochs-granite/artifact.json) retain every completed
case, ordinary baseline and check. `worker-epochs-report.py` validates and
summarizes their raw inputs into a separate output directory. These results
neither replace the qualified cold-read advantages nor establish serial latency,
full memory use, or merge/retained-reader performance.

The next worker policy panel retains that original control and adds four
attribution variants in `worker_epochs.cpp`:

| Policy | Change from the preceding variant |
| --- | --- |
| 0 | Original forced promotion at 64/128 keys, 192/96-byte units, eight inline bytes, ordinary mutation path. |
| 1 | Keep the compact list through its admitted capacity; K8 always fits. Grow before accepting a batch whose population-plus-request bound exceeds capacity. Size the first hierarchy from at least 512 solver-admitted primary slots where the domain permits. |
| 2 | Enable the existing external-bag and first-spill mutation patches. Applies to multimaps only. |
| 3 | Reserve enough inline bytes for one wide value, retaining eight bytes for narrow values. Applies to multimaps only; W2 intentionally has the same inline budget as policy 2. |
| 4 | Couple that inline budget with 64/64-byte units for sets, 96/64 for narrow values, or 320/160 for wide values. Recompute bucket counts from actual admitted slots. |

The 512-slot reservation, 75% hierarchy trigger and quadrupling remain explicit
controls. A pending batch is an upper bound on new distinct keys, so duplicate
work near a boundary can cause conservative growth. No exact future cardinality
is provided. Coordinated growth drains accepted work and preserves pending rows
until the replacement epoch commits. The driver has two hierarchy levels and
rejects inputs exceeding the measured range; it is not a general growth policy.
All variants use the same bounded Worker protocol and independent full-byte
oracle, including the complete 256-key K8 domain in correctness checks.

New CSV fields expose final per-owner populations, inactive owner/migration
storage, fixed caller/Worker state, and Orbital-reported Pool metadata/mapping
and Arena charges. They supplement active logical bytes and ticket/staging
storage. These are overlapping views where stated: Pool mapping includes active
allocation backing, and Arena charge is shared rather than summed per owner.
They must not be added into a purported RSS figure. Baseline allocator overhead,
allocator headers and high-water memory remain unknown. Every timed history
still checks complete contents after timing.

The corrected local policy capture completes 24 checks and 960 timing rows.
All 40 natural-policy domain/kind/value cases pass independently; four boundary
cases pass ASan/UBSan with always-on stack-use-after-return, and the K40/W2 map
passes ThreadSanitizer, including full post-timing oracles. The initial capture
failed only at constant-expression class completion and supplied no timings.

Native qualification completes on all three hosts, each with 24 checks,
48 paired processes and 1,920 timing rows. The complete caller-fusion audit finds
no reintroduced per-record outlined caller lambdas; cold calls remain. Results
are in [Zen5](evidence/worker-policy-zen5/artifact.json),
[V2](evidence/worker-policy-neon/artifact.json) and
[Granite](evidence/worker-policy-granite/artifact.json). The policy column keeps
these controls distinct in `worker-epochs-report.py`.

Uniform overlapping intake, including growth, in **ns/update**:

| Case | Host | Original policy 0, ours / Abseil | Natural policy 1, ours / Abseil |
| --- | --- | ---: | ---: |
| K8 set | Zen5 | 35.76 / 9.59 | 32.56 / 8.60 |
| K8 set | V2 | 56.13 / 17.29 | 46.42 / 14.55 |
| K8 set | Granite | 51.77 / 21.46 | 45.10 / 17.21 |
| K40/W2 map | Zen5 | 18.76 / 8.73 | 19.80 / 8.98 |
| K40/W2 map | V2 | 27.65 / 15.18 | 30.88 / 15.24 |
| K40/W2 map | Granite | 25.84 / 15.07 | 28.40 / 15.42 |

K8 active logical bytes fall from 28,240 to 608, versus Abseil 2,140 B on x86
and 2,124 B on V2. This is not a complete memory win: the candidate fixture also
reserves 2,576 B of inactive owner/migration alternatives and reports 7,808 B of
Pool metadata on x86 (7,824 B on V2), with 40,960 mapped Pool bytes. Both sides
carry 4,872 B of ticket/staging storage. Pool mapping includes backing, and
baseline allocator overhead is unknown. The inactive alternatives are a driver
design debt, not a proposed representation cost. K8's partitioned Granite
intake also has exceptions: 39.09→39.40 ns uniform and 44.04→47.17 ns skewed.
The compact regime earns its footprint without establishing throughput parity.

K40/W2 maps show why a blanket capacity change is wrong. Overlapping mode
expands active logical storage from 28,240 to 55,888 B and gets slower on all
three hosts. Under disjoint uniform routing, policy 1 instead uses 14,416 B and
one transition; intake improves 17.17→16.48 ns on Zen5, 26.76→25.84 on V2 and
25.40→23.77 on Granite. The matching baseline and skewed exceptions remain in
the full rows. Overlapping routing creates almost a complete key population in
both owners during these repeated passes; balancing work does not eliminate
duplicated partial aggregates. The reservation, growth factor and routing policy
must therefore be evaluated together. The 512-slot/75%/quadrupling rule is not
selected as a universal policy.

One inline wide value is also not a general improvement. For uniform overlapping
K64/W64 multimaps, policy 2→3 changes only the inline budget (8→64 B). Intake is
94.98→104.24 ns on Zen5, 145.72→143.41 on V2 and 138.69→149.89 on Granite.
Active logical storage rises from roughly 1.04–1.05 MB to 1.37 MB. Policy 4's
larger units recover some time but still use about 1.28–1.29 MB; ordinary Abseil
uses about 1.00 MB and is substantially faster. These histories contain eight
updates per key, plus a skewed control, rather than a naturally singleton-heavy
population. They reject a blanket inline upgrade, not inline storage itself.
Keep fanout and its evolution in the coupled layout/ownership decision.

The useful selections are the compact K8 lifetime and complete accounting.
This panel does not close the worker-throughput gap or select a general growth,
inline or bucket preset. All code remains in the spike.

Phase-separated merge has two materially different lifetime choices. Whole-Worker
handoff can retain allocation identity, but the receiver must retain/service all
source Pools, Lanes, queues and budgets. Consolidation into one destination Pool
requires copying/reencoding, or retaining immutable sources until later cloning.
An Allocation move does not adopt storage into a different Pool; a Hold does not
authorize mutation. Representation-local multimap IDs cannot simply be copied
between unrelated bag owners. Charge consolidation, source retirement, old/new
coexistence and retained readers, alongside the independent read and intake rates.

The comparison should use private ordinary Abseil with the same bounded protocol
as the immediate control, plus the existing shared Boost/GTL controls where their
operation semantics match. Keep owner setup, admission/routing, mutation, coordinated
growth, merge, final reads and retirement distinguishable, and report the complete
lifetime too. This extends the existing experiment rather than selecting its old
barrier, geometry, exact-cardinality or word-map restrictions as defaults.

## Actual distinct-key admission and corrected inspection

Policy5 keeps a full compact list until an actual absent-key insertion is refused.
A retired ticket retains its untouched suffix across paired prepare/cancel/commit
and replays it before later input. The forced boundary accepts63duplicates,
refuses the64th absent key, cancels once, then retries exactly one update. No
extra payload queue is introduced. Snapshot storage is allocated before owners
exist and complete union/reduction validation runs after shutdown; its capacity
is reported separately as inspection_bytes. See BENCHMARK-VALIDATION.md.

Source b6ed0d20f000d8cffcc5a734804dd1257da68bde08d03192ba12de2745d72d92 passes
local12checks/44processes/1,056rows/352cases; ASan/UBSan/always-stack-UAR five
families and TSan K40/W2MM pass ordinary and256-key populations. Native Zen5
20261015T071315Z-dedd12e5, V2...-ff9e7e16, Granite...-a3736e54 each complete
88processes/3,520rows/352cases. Both process orders, five repeats,240/256/257/1024
input keys (K8 only240/256), partitioned/overlap and uniform/skew. The baseline
uses native growth and no artificial encoding epochs under policy5.

**Keep the exact boundary for memory semantics; it does not earn a throughput
win.** At256overlapping uniform K40/W2 map keys the table pair drops14,928→3,712
logical B. A repeated-update compact list can be slower than a prematurely
promoted index. Larger populations change little, and the complete Worker
consumer remains materially behind private Abseil. This is a throughput loss,
not a few-nanosecond serial-latency concession.

Selected uniform overlap cells, ns/update includes intake plus growth and
excludes separately reported setup/retirement. Logical B is the two active
owners, excluding separately charged queues, inactive owners, runtime, Pool
metadata and inspection fixture. These rows do not establish total RSS or
lookup latency. Each median combines two process orders × five repeats.

| Host | Keys | Case | Hierarchy ns/update | Abseil ns/update | Hierarchy logical B | Abseil logical B |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| Zen5 | 256 | K40 set | 36.13 | 8.01 | 2,688 | 9,296 |
| Zen5 | 256 | K40 W2 map | 38.42 | 8.19 | 3,712 | 17,472 |
| Zen5 | 256 | K40 W2 multimap | 56.40 | 8.27 | 12,472 | 33,824 |
| Zen5 | 1024 | K40 W2 map | 17.47 | 7.74 | 57,936 | 69,696 |
| Zen5 | 1024 | K64 W64 map | 25.79 | 12.09 | 369,200 | 298,960 |
| Zen5 | 1024 | K64 W64 multimap | 90.44 | 22.42 | 1,284,931 | 998,912 |
| V2 | 256 | K40 set | 58.22 | 15.23 | 2,688 | 9,280 |
| V2 | 256 | K40 W2 map | 65.39 | 16.23 | 3,712 | 17,456 |
| V2 | 256 | K40 W2 multimap | 87.54 | 18.16 | 12,518 | 33,808 |
| V2 | 1024 | K40 W2 map | 34.41 | 15.46 | 57,936 | 69,680 |
| V2 | 1024 | K64 W64 map | 43.47 | 19.93 | 369,200 | 298,944 |
| V2 | 1024 | K64 W64 multimap | 144.21 | 38.62 | 1,287,440 | 999,984 |
| Granite | 256 | K40 set | 55.75 | 15.70 | 2,688 | 9,296 |
| Granite | 256 | K40 W2 map | 56.96 | 16.31 | 3,712 | 17,472 |
| Granite | 256 | K40 W2 multimap | 78.05 | 17.64 | 12,386 | 33,824 |
| Granite | 1024 | K40 W2 map | 33.95 | 15.53 | 57,936 | 69,696 |
| Granite | 1024 | K64 W64 map | 54.23 | 28.59 | 369,200 | 298,960 |
| Granite | 1024 | K64 W64 multimap | 152.75 | 49.06 | 1,285,204 | 998,144 |

`evidence/worker-partial-{zen5,neon,granite}` retains full summaries and recoverable
sources/raw rows/assembly. Initial inspection of the Zen5 binary finds one
outlined geometry-dispatch call per eight-record grain; set/map ready work is
otherwise fused in that body, with cold allocation paths. Multimap small apply
can still outline. This static inspection does not attribute the measured gap.
The Worker intake currently loops scalar writes; the separately qualified N8
hashing/write path has not been composed into this caller, and map reductions
need packet combine semantics. Measure that complete caller, including input
packing and accepted-prefix retry, before assuming a packet win. Also profile
the real Worker consumer rather than blaming one call for the whole deficit.
Actual merge/handoff and retained-reader publication remain separate work.


## Packet mutation in the actual caller

The next paired control keeps policy5 geometry/admission and the same64-record
tickets and8-record task grain. It replaces the scalar calls inside a grain
with `setN_bytes<8>` or caller-supplied `combineN_bytes<8>`, sharing the existing
fused mutation bodies. Preparing key/value arrays from ticket entries is inside
the measured callback. A refusal returns the accepted prefix to the existing
cancel/grow/retry protocol; inputs and mutable lookup locations do not escape.

Three lowerings are compared: scalar operations, explicit SIMD primary hashing
with lazy scalar secondary hashing, and per-lane scalar packet hashing. Small
lists follow the same vector/scalar attribution. This is execution over one
format, not three owner types to graduate. Short8-pass traces retain the previous
complete-growth comparison; long128-pass traces separately test recurring update
cost. Neither is serial latency. Software-clock profiles over longer4096-pass
traces retain setup, oracle and coordinator costs and are not unprofiled timings
or cache-residency measurements.

The local frozen V1 panel completes18Worker checks,20reducer checks spanning all
eight key domains and1..64-byte values, and66timing processes/1,584rows. Ordered
noncommutative reduction catches duplicate reordering that XOR cannot. Sparse
mask/refusal tests verify exact reducer calls, occupancy deltas, damage, retained
copies and replay once. Both packet paths pass the actual256/257-key promotion
and cancellation history. The first native panel completes on all three hosts: Zen
`20261015T075011Z-8b7d84da`, V2 `...-c4a7b335`, Granite `...-243d4f1a`.
Each has 132 timing processes, 5,280 rows, 528 operation/storage cells and 56
correctness runs. Source `178b0b38183c9101034ee1f041dffeda8145593212deef40f993576307905c1e`.
ASan/UBSan/always-stack-UAR passes ten Worker variants and eight reducer variants;
TSan passes both narrow-MM packet variants, ordinary and compact histories.

Assembly exposed eight unrolled allocation-capable small-list mutation bodies.
The shared-loop refinement retains N8 hash preparation and ordered commits while
reducing local K40/W2 map dispatcher instructions from 3,172/2,820 (vector/scalar
packet) to 2,196/1,824; the scalar control stays 1,372. Wide-MM counts fall from
4,356/3,928 to 3,188/2,768. These counts include cold paths and are not dynamically
executed instruction counts. The complete callers are inventoried by
`check_epoch_codegen.py`, including their remaining calls and stack references.

The paired refinement completes on the same native hosts: Zen
`20261015T080147Z-d361e542`, V2 `...-c82fb5cb`, Granite `...-ed6c28f1`.
The same 132 processes/5,280 rows/528 cells/56 checks pass per host. Source
`7c8d952b429f46bf3c19b435a940ed6e96323f113ea9f2906bc830ac27b9c25e`.
Focused ASan/UBSan/stack-UAR and TSan reruns also pass. Keep the shared lane body:
the native comparison supports its smaller code without a general throughput
penalty, with exceptions preserved in the full panel. This is a maintenance/code
size choice, not a claim of a universal speedup from replacing unrolling.

Selected **reciprocal throughput, ns/update**, 128 passes, uniform input,
overlapping private owners. Each cell is **scalar / N8 scalar-prepared / ordinary
Abseil paired with N8**. These are complete intake plus amortized growth costs;
setup and retirement stay separate in the full scorecard. They are not serial
latency, DRAM measurements or a selected complete production configuration.

| Case / input keys | Zen 5 | Neoverse V2 | Granite Rapids |
| --- | ---: | ---: | ---: |
| K40 set / 256 | 16.67 / 15.65 / 6.75 | 26.26 / 26.51 / 12.81 | 23.45 / 24.89 / 13.06 |
| K40 W2 map / 256 | 16.92 / 15.56 / 6.22 | 26.91 / 26.15 / 13.18 | 24.52 / 24.10 / 12.94 |
| K40 W2 map / 1,024 | 7.45 / 7.00 / 6.78 | 16.37 / 14.39 / 13.55 | 20.65 / 13.91 / 13.34 |
| K64 W64 map / 1,024 | 9.37 / 9.84 / 8.19 | 17.47 / 19.39 / 14.99 | 26.99 / 25.57 / 24.28 |
| K40 W2 multimap / 1,024 | 12.19 / 12.73 / 7.45 | 23.94 / 23.42 / 15.97 | 23.10 / 21.95 / 14.94 |
| K64 W64 multimap / 1,024 | 40.55 / 41.18 / 16.61 | 61.12 / 62.70 / 28.68 | 62.17 / 63.65 / 32.41 |

On x86, the K40/W2 map at 256 input keys uses 3,712 versus 17,472 logical bytes;
at 1,024 it uses 57,936 versus 69,696. The same hierarchy's K40 set at 1,024 uses
41,552 versus 36,944, and its W64 map 369,200 versus 298,960: the still-provisional
quadrupling/75% Worker geometry spends too much empty capacity in these cases.
The separate corrected growth study chooses doubling/90%; integrating that choice
must preserve coordinated admission, rather than transplanting a threshold and
silently adding false growth on duplicate-heavy batches. Bag memory varies with
per-worker value distribution and remains charged. Runtime/queue/Pool/inspection
storage are separate columns, not hidden inside these active logical bytes.

**Decision:** keep point mutation and a shared-body N8 mutation interface. Use
scalar-per-lane hash preparation as the starting write lowering; explicit SIMD
preparation remains an attribution control, not an automatic write default.
The actual Worker should use batches where complete consumption benefits. Narrow
promoted maps improve on all three hosts here; wide maps and multimaps do not
justify universally routing every update through that path. Read preparation
remains a separate choice from the qualified read-packet study. No public option
for every benchmark arm is implied.

Software-clock sampling succeeds for all 18 profiles on Zen and Granite. The
V2 guest refuses every attempt with `PMU Hardware doesn't support sampling/overflow-interrupts`;
there is no ARM dynamic-hotspot claim. Profiles include both contenders, four
routing/skew histories, oracles and coordination. In the original Zen compact-map
profile, 737 samples include 15.33% in the candidate mutation dispatcher; about
69% of those map to the list selector/confirmation loop. In the 1,024-key profile,
its mutation body accounts for 6.23% of total samples. These are approximate
source-line CPU-work attributions, not fractions of timed wall latency. They
support addressing list search, not blaming the one dispatcher call. Multimap
oracles/sorting account for substantial whole-process work; do not mislabel them
as timed container costs. `worker-packet-profile-report.py` keeps the distinction.

The original [Zen](evidence/worker-packet-zen5/artifact.json),
[V2](evidence/worker-packet-neon/artifact.json) and
[Granite](evidence/worker-packet-granite/artifact.json) controls, and shared-loop
[Zen](evidence/worker-packet-loop-zen5/artifact.json),
[V2](evidence/worker-packet-loop-neon/artifact.json) and
[Granite](evidence/worker-packet-loop-granite/artifact.json) preserve summaries,
all repetitions, sources, binaries, assembly and profiles in verified archives.
The report recipes regenerate timings, static inventories and profile summaries.

The next architectural work is compact-list lookup/admission and actual integrated
growth, together with wide-MM ownership and retained-reader/merge lifetimes.
More hash-preparation variants alone will not close the observed gaps. The
cold-read advantages remain in READ-WINDOWS.md and STAGED.md; this update panel
does not supersede those workloads or their separate serial losses.


## Integrated growth comparison

The selected isolated-growth direction now has a complete Worker control:
double the bucket count, with a 90% primary-capacity trigger. Bucket slot count
can change with encoded suffix width, so this does not promise exactly twice
the admitted slots. The older quadruple/75% owner remains the paired control.
The new fixture admits a third hierarchy level when wide values need it and
charges its inactive C++ state separately from active packed storage. These
compile-time alternatives are bounded experiment machinery, not the proposed
production representation of runtime growth levels.

Two admission rules distinguish geometry from duplicate-induced reservation.
The conservative rule adds pending records to reported occupancy. The completed-
delta rule consults the previous accepted batch's actual distinct-key delta;
one 64-record batch may cross the trigger before coordinated growth. The caller
has no future distinct-key count. Compact lists still promote on an actual
absent-key refusal; K8 never promotes. Allocator refusal is not growth.

A forced actual-Worker history fills one owner to the first trigger while its
peer stays empty, applies a duplicate-only batch, then64new keys. It verifies
both admission decisions, performs prepare/cancel/commit at all three levels,
and checks every key, complete opaque value and duplicate count after transfer.
The ordinary paired histories retain overlap/disjoint routing and uniform/skewed
updates. This does not implement cross-Pool consolidation or retained publication.

V1 had a compilation failure in the disabled third-level control and produced
no timings. V2 corrects the compile-time guard; frozen source
`3c7dd6261b78d5a2fc2f8de615035428e79bce25bb852e9fe00bece81d1e6b72`.
The local Run completes 96 processes/2,304 rows/768 cells and 24 checks across all eight
domains. Uniform overlapping owners,1024 keys/128passes: K40sets use41,552→21,072 B
and K40/W2maps57,936→29,264 B under doubling; active logical bytes only. Inactive
fixture state increases2,112→3,408 B for the pair. Local reciprocal costs are
8.56→8.83 ns and 8.64→8.88 ns respectively for completed-delta admission; these
ARM-VM diagnostics are not platform qualification. Wide W64maps finish at the
same369,200active B after an extra growth; wide-MM structural losses remain.
Local static inventory counts18complete mutation dispatchers. Adding a third
specialized level increases K40/W2packet-body instructions1824→2596; this includes
all epoch branches and cold paths, not a dynamic instruction cost. The bounded
fixture must not dictate a production owner with every epoch body in one visit.
Native jobs Zen 20261015T085146Z-64133d41, V2...-0df237fb and Granite...-d7205463
complete from this source; the absolute results below qualify the trade-off. The shared
compact packet-lookup experiment remains disabled throughout this comparison.


### Native integrated growth results

All three broad panels complete:192 processes,7,680 rows,768 cells and 42 checks per host. Each cell has ten samples (five repeats in both process orders). Local ASan/UBSan with always-on stack-use-after-return checks passes 26 histories across 13 variants; TSan passes four narrow-map/wide-MM histories. All eight key domains are covered by behavior checks; this targeted timing panel does not replace the full domain/value cross-product.

Uniform overlapping owners,1024 keys,128 updates/key. Costs include intake and coordinated growth, in ns/update. Abseil is the matched completed-delta process. These are reciprocal update costs, not serial latency or DRAM-read timings.

| Host / case | Old quad/75% ns | Double/90% + completed deltas ns | Abseil ns | New logical B | Abseil logical B |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen5 / K40 set | 7.18 | 6.92 | 6.89 | 21,072 | 36,944 |
| Zen5 / K40/W2 map | 7.06 | 7.00 | 6.82 | 29,264 | 69,696 |
| Zen5 / K40/W2 multimap | 12.32 | 12.50 | 7.54 | 594,632 | 519,776 |
| Zen5 / K64/W64 map | 9.71 | 10.09 | 8.14 | 369,656 | 298,960 |
| Zen5 / K64/W64 multimap | 40.28 | 41.52 | 18.36 | 14,497,234 | 12,615,616 |
| V2 / K40 set | 14.34 | 14.57 | 13.08 | 21,072 | 36,928 |
| V2 / K40/W2 map | 15.07 | 15.26 | 13.32 | 29,264 | 69,680 |
| V2 / K40/W2 multimap | 23.96 | 24.26 | 16.29 | 595,036 | 518,992 |
| V2 / K64/W64 map | 19.10 | 20.01 | 15.09 | 369,803 | 298,944 |
| V2 / K64/W64 multimap | 60.14 | 61.18 | 27.28 | 14,504,979 | 12,654,512 |
| Granite / K40 set | 13.57 | 14.40 | 12.93 | 21,072 | 36,944 |
| Granite / K40/W2 map | 14.15 | 13.88 | 13.27 | 29,264 | 69,696 |
| Granite / K40/W2 multimap | 22.77 | 23.17 | 14.95 | 595,438 | 519,904 |
| Granite / K64/W64 map | 25.74 | 25.69 | 24.19 | 369,656 | 298,960 |
| Granite / K64/W64 multimap | 70.46 | 71.29 | 34.69 | 14,531,643 | 12,634,048 |

Narrow-set/map active storage falls41,552→21,072 B and 57,936→29,264 B. The fixture separately charges its increased inactive state (2,112→3,408 B per pair), queue/runtime/Pool storage and backing. These logical counts are not RSS. Wide maps can reach the old final allocation after an extra migration; wide multimaps retain large update and storage losses. Do not extrapolate the narrow result into a universal win.

The exact-trigger follow-up isolates duplicate-induced growth. Zen5 K40sets at 633 keys stay at 10,832 B instead of 21,072 B with completed deltas, taking7.14 ns/update versus7.39 with pending bounds and 6.74 for matched Abseil. K40/W2maps at 748 keys use15,124 instead of 29,264 B, taking7.18 versus7.13 ns (Abseil6.73). A wide-map460-key owner saves184,880→104,498 B but costs10.51→12.02 ns (Abseil8.11). V2 and Granite confirm the trade-off: at 748 keys,narrow-map costs are14.59→14.45 ns
and 14.66→14.54 ns; matched Abseil13.14/13.18 ns. Wide-map460-key costs are
21.69→23.16 ns on V2 and26.84→27.67 ns on Granite, with the same 184,880→104,498 B
active storage reduction. Matched Abseil16.28/23.77 ns. All three edge panels
complete 72 processes/2,880 rows/288 cells and 42 checks. SourceV3 changes only the
runner/report and preserves the V2 C++ implementation. Edge jobs:
Zen 20261015T085932Z-626ebf1c, V2 20261015T090209Z-bc6af76a,
Granite 20261015T090118Z-fc48d26b.

This reveals a format qualification limit: the actual Worker control has 11/13 primary slots for K40sets/narrow maps,6 for narrow multimaps and only4 for W64 records. The earlier growth study selected90% with 14/28-slot families. A threshold cannot transfer independently of bucket capacity: high occupancy in four-slot buckets has different downstream costs. Advance completed occupancy deltas as the admission signal; qualify the load threshold and solved format together, especially for wide records. Do not deliberately count duplicate updates as absent keys just to obtain spare capacity.

Evidence: [Zen5](evidence/worker-growth-zen5/qualified/summary.csv), [V2](evidence/worker-growth-neon/qualified/summary.csv), [Granite](evidence/worker-growth-granite/qualified/summary.csv) and [Zen5](evidence/worker-growth-edge-zen5/qualified/summary.csv), [V2](evidence/worker-growth-edge-neon/qualified/summary.csv) and [Granite threshold traces](evidence/worker-growth-edge-granite/qualified/summary.csv). Full sources, binaries, assembly, every timing repetition and process-resource records are recoverable from their archives.

## Solver-sized formats in the complete Worker caller

The format panel keeps completed-delta admission, doubling/90%, exact access, inline budget and linked bag ownership fixed. Requested primary capacities 14/28 and secondary capacities 5/8 use the existing field solver, with eight-byte rounding, recomputed after each address-width change. Padding may admit another slot. These are geometry controls, not additional public modes.

Each native host completes 192 processes, 7,680 rows, 768 cells and 42 checks; ten samples/cell. Source `85b1269f18e6d37a81cba0b4ae36c8999467bf4dea2a1397d92800a3edbbc664`. Local ASan/UBSan/stack-UAR passes 26 checks across 13 variants; TSan passes four. Timing covers six cases, populations 512/768/1024 (K8:256), 8/128 passes, partitioned/overlapping owners and uniform/skewed input. Behavior checks cover all eight domains.

Uniform overlapping owners, 1024 keys and 128 updates/key, **reciprocal ns/update including growth**. Abseil is the matched 14-slot process; the archive keeps the separate baseline in every process. These are neither serial nor cold-read results.

| Host / case | Old geometry ns | 14-family ns | 28-family ns | Matched Abseil ns | Logical B old / 14 / 28 | Abseil B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Zen5 / K40 set | 6.98 | 7.12 | 6.95 | 6.73 | 21,072 / 23,632 / 21,840 | 36,944 |
| Zen5 / K40/W2 map | 6.86 | 7.05 | 6.97 | 6.56 | 29,264 / 30,288 / 29,520 | 69,696 |
| Zen5 / K40/W2 multimap | 12.23 | 12.12 | 11.99 | 7.33 | 594,632 / 595,389 / 592,854 | 519,328 |
| Zen5 / K64/W64 map | 9.91 | 9.82 | 9.79 | 8.05 | 369,656 / 284,720 / 278,576 | 298,960 |
| Zen5 / K64/W64 multimap | 40.88 | 41.56 | 41.39 | 16.56 | 14,503,158 / 14,420,263 / 14,441,038 | 12,607,424 |
| V2 / K40 set | 13.83 | 14.05 | 14.33 | 12.49 | 21,072 / 23,632 / 21,840 | 36,928 |
| V2 / K40/W2 map | 14.56 | 14.65 | 14.91 | 13.60 | 29,264 / 30,288 / 29,520 | 69,680 |
| V2 / K40/W2 multimap | 24.03 | 23.75 | 23.62 | 16.23 | 595,170 / 596,730 / 594,730 | 519,440 |
| V2 / K64/W64 map | 20.27 | 19.92 | 19.80 | 15.02 | 369,656 / 284,720 / 278,576 | 298,944 |
| V2 / K64/W64 multimap | 63.39 | 62.10 | 65.28 | 29.93 | 14,416,784 / 14,414,110 / 14,416,425 | 12,658,608 |
| Granite / K40 set | 14.52 | 15.56 | 13.39 | 13.53 | 21,072 / 23,632 / 21,840 | 36,944 |
| Granite / K40/W2 map | 13.52 | 14.18 | 13.95 | 12.77 | 29,264 / 30,288 / 29,520 | 69,696 |
| Granite / K40/W2 multimap | 22.09 | 21.86 | 22.11 | 14.73 | 596,109 / 596,730 / 592,048 | 519,584 |
| Granite / K64/W64 map | 25.54 | 24.64 | 25.32 | 24.32 | 369,656 / 284,720 / 278,576 | 298,960 |
| Granite / K64/W64 multimap | 67.02 | 69.77 | 67.14 | 30.47 | 14,502,927 / 14,397,701 / 14,358,996 | 12,621,760 |

The wide-map geometry saves 369,656→284,720/278,576 active logical B, with similar or slightly lower update cost. Narrow set/map geometry gains little; changing it universally is unjustified. Wide-multimap append and storage losses survive both larger families: bucket capacity alone is not their cause. Families must also face the cold read-window and complete growth-cycle results; this panel does not select a universal 14/28-slot preset.

Use the 14-family as the next wide-map consolidation control: it captures most of the storage saving with smaller buckets than 28. Keep the narrow incumbent and linked-bag control while qualifying the shared mutation/ownership boundary. Complete transfer and retirement costs now deserve attention alongside external multimap storage, rather than another isolated selector or append sweep.

Evidence and full archive recipes: [Zen5](evidence/worker-format-zen5/artifact.json), [V2](evidence/worker-format-neon/artifact.json), [Granite](evidence/worker-format-granite/artifact.json). Each archive includes all repetitions, both baseline orders, compiler/host context, binaries and 18 complete dispatcher disassemblies. Static inventories include cold branches and do not measure dynamic instruction costs.
