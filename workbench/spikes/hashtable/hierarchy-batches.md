# Packets through the complete hierarchy

A packet can share hash arithmetic across requests while each request still
searches and mutates the current container. This experiment extends the existing
explicit NEON/AVX2/AVX-512 hash packets through both bucket tiers, key-overflow
links and opaque multimap bags. It compares four/eight requests with scalar
operations and ordinary/exact-key Abseil in the same complete-operation driver.
The native evidence below recommends eight requests as the fixed interface.
Initial x86 reads use vector primary/scalar secondary preparation; V2 uses
per-lane scalar preparation. Mutation lowering remains operation-specific.

`record_table::with_packet` admits N initialized, immutable key objects for one
nonsuspending callback. It calculates primary hash facts with `split_packet` and
lazily calculates a secondary SIMD packet when the first downstream request needs
one. Both packets contain only hash facts. No count, match rank, payload pointer
or allocation binding is reused across mutations. Keys and values cannot alias
storage that these mutations change or relocate; keys must remain stable because
the lazy secondary calculation rereads them. The callback cannot reseed, replace
or migrate the index, and borrowed packet/record material cannot escape it.

The promoted owner retains responsibility for bag preparation, commit, damage
and caller deltas. Its scalar and packet entry points use one preparation body.
The index similarly shares one mutation body with a lazy secondary-hash provider.
The scalar provider caches its secondary result too: a packet must not receive
credit for eliminating a redundant scalar secondary hash on miss plus admission.
This is an internal composition boundary, not a proposal to expose hash facts in
the user-facing container interface.

`setN_bytes` commits active lanes in ascending order. Later duplicate keys see
earlier writes, and later lanes see changes in bucket counts and bag state.
Refusal returns the committed lane mask, including holes. Retrying its complement
must neither lose nor duplicate values. This is a logical-prefix guarantee;
existing allocation reservation/cancellation contracts may retain capacity or
publish structural events on a refused operation. Effects are admitted per lane,
so a packet does not introduce a new all-or-nothing reservation requirement.
`getN_bytes` returns a hit mask and visits all values for each matching active
lane before moving to the next lane. All N input objects must be readable even
when some lanes are inactive. Tail staging belongs to the caller.

The initial driver, `hierarchy_batch.cpp`, supports all eight key domains and all
three container kinds. CMake exposes per-case targets to bound compile memory;
the ordinary and exact-width competitors use opaque values of the same size.
Correctness histories force primary, secondary and key-overflow traffic, exercise
sparse repeated-key writes, inline/bag boundaries, removal/reuse, refusal at every
active position, retry, full per-lane byte/multiplicity checks and clone lifetime.
All thirty-four selected case targets and the two unrolled controls pass locally:
sets plus two-byte maps/multimaps at all eight key domains, and K40 maps/multimaps
with 1/3/8/16/64-byte values. Native execution is qualified below. Actual retained-reader damage consumption
remains separate: clone lifetime checks do not establish a byte-damage COW
consumer. The target compiler's two autovectorizers remain disabled.

Performance is part of this first implementation. The same executable times
construction, build, hit/miss reads, mixed-mask duplicate updates, reads after
updates, removal, reinsertion, release and their mutation-lifecycle sum. Pool
setup and independent inventories are outside these timers for all contenders.
The reported logical memory includes owner metadata; baseline transient rehash
peaks are not measured. Hierarchy geometry is fixed as an experimental control,
including the diagnostic K8 case; it cannot choose a small-list promotion policy.
These packet traces measure independent throughput, not dependent latency.

The initial implementation may lose: N4/N8 materialize 192/384 bytes of hash facts
if both tiers are used, and one downstream request triggers secondary arithmetic
for every lane. Sparse masks, all-primary packets, one downstream lane and crowded
packets need distinct interpretation. Inspect complete consumer code for spills,
outlined work and actual SIMD before attributing any timing difference. A scalar
lane loop around `split_packet` establishes explicit SIMD hashing, not SIMD bucket
search or a gather/scatter advantage; those are separate candidates if the measured
access pattern supports them.

## Initial evidence and next design question

The first two-byte K40 panel checked scalar, four and eight requests from the
first timing run. Its 768-key map placed 758 keys in primary buckets and ten in
secondary buckets. The same fixed geometry with a sixteen-byte inline multimap
budget placed 318/20/430 keys in primary/secondary/chains. Its severe chain cost
is a coupled capacity-policy result; the panel cannot select that geometry for
both kinds. The first miss diagnostic reused sixteen absent keys. V2 replaces
that with a shuffled absent cohort as large as the resident cohort, subject to
the K8 domain bound, and adds 64/192-key histories. Those small fixed hierarchies
remain diagnostics, not replacements for the compact-list regime.

Disassembly of the original unrolled form binds the actual timed binaries and
confirms explicit widened NEON hash arithmetic, lazy secondary calculation and
call-free candidate/Abseil reads. Inline map replacement and multimap append are
fused; allocation, growth and external-bag commit remain in complete writes.
N8 has substantial vector/packet spills. The K40 map read roots occupy
1,012/4,632/12,172 code bytes for scalar/N4/N8, with 144/368/1,008-byte frames.
Multimap write roots grow from 6,064 bytes and a 672-byte frame to 56,132 bytes
and a 1,456-byte frame. These are static root measurements, not transitive peaks
or dynamic instruction counts. Exact receipts and excerpts are under
`build/hashtable-design/hierarchy-packet-local/codegen-review`.

The next paired comparison keeps SIMD hashing but shares one lane body, using
an explicit scalar loop whose unrolling is disabled. `HT_HIERARCHY_PACKET_UNROLL`
retains the original body expansion as a measured control. Logical contents,
per-phase memory and tier populations agree across scalar/N4/N8. Across both
forms, both kinds, three cardinalities, three contenders, two passes and five
repetitions, the local panel has 10,800 phase rows from 216 processes.
[The retained summaries](evidence/hierarchy-packet-local/summary.json) keep every
phase and baseline comparison. Complete repeats, source snapshots and ELF hashes
remain in `build/hashtable-design/hierarchy-packet-loop-local`.

Sharing the lane body is not a general performance win. At 192 keys, N8 map
hits/misses are 1.085/1.222 times the unrolled times; multimap updates are 0.907.
At 768 keys, N4 map hits/misses are 1.231/1.275 times unrolled. These warmed
ARM/OrbStack diagnostics choose neither a public N nor a platform backend.
The next useful question is how to keep hash facts compact and consume them
without either expanding complete operations eightfold or forcing large indexed
stack transfers. This is a container composition issue; it has not demonstrated
new SeriesPack/TuplePack overhead.

The broader shared-body panel completes 89,100 phase rows from 1,782 fresh
processes: 34 key/kind/value cases, 99 valid cardinality histories, scalar/N4/N8,
three contenders, two opposite-order passes and five repetitions. Case identities,
full phase inventories, content oracles and timed binary hashes pass. All 36
Release check targets pass; ASan/UBSan also passes shared-body two-byte multimaps
and 64-byte maps/multimaps. [Surface evidence](evidence/hierarchy-packet-surface-local/context.json)
links every complete-operation summary and the exact local inputs.
The [archive receipt](evidence/hierarchy-packet-surface-local/artifact.json)
recovers all 34 timed binaries, source snapshots, compiler receipts and repeats.

N4 improves hits in only 11 of 99 cells and updates in 9; N8 does so in 29 and
10 cells respectively. These are descriptive counts over the experimental
matrix, not weighted workload scores. The architecture has not earned an
explicit-SIMD default. Two controls preserve the existing permutation: compact
hash facts, implemented below, and sharing the constant high-half seed product
across lanes when an input component is at most 32 bits. The latter remains a
proposal; neither is selected for production.

An independent mathematical review checks 16,505 full-fold cases and 9,216
compact-fact cases across all 288 K/A geometries. Constant-high folding must
retain the seed product's full 96 bits and the middle sum's carry, which can be
zero, one or two. Its applicability is separate for bucket and selector inputs;
the existing zero-width shortcuts must remain. Inspection of the original
K40/A6 N4 NEON caller finds two seed-only widened products still inside the
packet path. This supports testing explicit preparation, but does not establish
the behavior of the latest shared-body caller or x86. The oracle, counterexamples
and hashed excerpt are in
`build/hashtable-design/hierarchy-packet-constant-high-review`; they validate
the proposed algebra, not an implementation or performance claim.

## Compact facts with unchanged hashing

`HT_HIERARCHY_PACKET_COMPACT=1` stores bucket, fingerprint and suffix in one
K-bit word per lane, ordered from low to high bits. Explicit NEON, AVX2 and
AVX-512 construction directly encodes the SIMD result; it does not first store
and reload the three planes. Lane consumption extracts the required fields.
The input keys remain available for lazy secondary hashing and overflow lookup.
No persistent table bytes, hash family, lane ordering or mutation semantics
change. Temporary fact storage for both tiers falls from 192/384 bytes to
64/128 bytes for N4/N8, excluding the optional-state tag and other caller state.
Actual stack frames remain a generated-code question.

The first matched local comparison rebuilds plane and compact map/multimap arms
from one source state. It retains scalar and ordinary/exact Abseil controls,
64/192/768-key histories, both process orders and five repeats: 10,800 phase
rows from 216 processes. The independent byte and multiplicity oracles pass,
and paired logical memory agrees throughout. The aggregate at 192 keys initially
suggested gains below, but the broader rerun did not reproduce them. These rows
are retained as a measurement-instability witness, not a performance selection:

| Kind / operation | Compact / planes | Compact / scalar | Compact / ordinary Abseil |
| --- | ---: | ---: | ---: |
| Map hit | 0.890 | 1.098 | 1.793 |
| Map miss | 0.792 | 1.296 | 1.611 |
| Multimap hit | 0.936 | 1.228 | 1.407 |
| Multimap miss | 0.971 | 1.683 | 1.851 |

These are fixed-hierarchy, warmed ARM/OrbStack diagnostics. Small histories do
not choose the compact-list transition, and the 768-key multimap intentionally
retains the crowded geometry control. Benefits vary by cardinality and packet
width. Compact facts have not made batching generally competitive with scalar.
Short mutation/lifecycle phases show large variation even in unchanged scalar
controls; those ratios cannot establish a policy win. Full comparison inputs
and every paired phase are in `build/hashtable-design/hierarchy-packet-compact-local`.

The matched broader panel includes nine key/kind/value cases: K8 and K64 sets,
K16 two-byte maps/multimaps, K40 two-byte and 64-byte maps/multimaps, and K64
two-byte multimaps. Its 26 cardinality histories produce 46,800 rows from 936
processes, with both representations and all three contenders. Inventories,
content oracles and exact timed binary hashes pass. N4/N8 compact hit times are
lower than plane times in only 7/8 of the 26 cells, and lower than scalar in five
cells each. Descriptive median compact/plane hit ratios are 1.007/1.010; this is
not a workload-weighted score. The control therefore remains default-off.

The repeated K40 two-byte binaries are byte-identical across panels. In the
first panel, the plane N8 map hit costs 3.619/4.933 ns in the two process-order
passes, versus compact 3.890/3.917. In the broader panel these become
3.704/3.564 versus 3.855/4.146. Thus the initial aggregate's apparent hit win was
not stable across process order or repetition. The earlier miss win similarly
depended on an inflated plane pass. Even an unchanged wide-map scalar read shows
a 0.776 aggregate ratio with pass ratios 0.992/0.576. These local diagnostics
cannot resolve thin performance differences; a qualified native comparison is
still required. The frame-size result is a separate static observation.

A focused replay increases repetitions from five to 31 per process without
rebuilding the four K40 two-byte binaries. It validates 66,960 rows from 216
processes. Across its six cardinality/kind histories, unchanged scalar hit
ratios narrow to 0.990–1.003 and misses to 0.984–1.013. At 192 keys, N8 map
compact/plane hit and miss ratios are 1.080 and 1.064, with both process-order
passes agreeing. Multimap ratios are 0.987 and 1.033. The stable replay
therefore does not support the initial apparent map gains. Mutation/lifecycle
controls remain more variable and do not establish a choice.

The complete-caller assembly independently establishes lower frames: map N4/N8
656→448 and 928→640 bytes; multimap 704→496 and 992→672 bytes. SIMD widening
multiply inventories are unchanged. Scalar candidate and ordinary/exact Abseil
read controls have identical normalized instructions, and all 36 inspected read
roots are call-free. These measurements belong to the four K40 two-byte ELFs;
they do not qualify the additional wide-value or key-domain callers. A smaller
frame does not establish fewer dynamically executed spills or a throughput win.
Packed-field extraction changes dependencies as well as storage. This experiment
does not identify new SeriesPack/TuplePack overhead.

**Disposition:** keep compact facts default-off. The local evidence does not
support it as the shared-body packet default, and batching still usually loses
scalar. Native-platform selection and the public N remain open. Further work
should target hash/lookup dependencies and coupled container policy, with compact
facts retained as an attribution control rather than an assumed improvement.
The [retained comparison](evidence/compact-facts-local/surface/context.json),
[exact-binary replay](evidence/compact-facts-local/replay/context.json) and
[caller audit](evidence/compact-facts-local/local/codegen-review/review-index.json)
preserve the decision's scope. Their [verified archive](evidence/compact-facts-local/artifact.json)
recovers all three raw panels, timed binaries, full source capture and reports.

Release arithmetic checks cover 576 K/A/N geometries and 6,808,512 key cases
against independent uint128 expectations. Nine compact hierarchy cases and
their plane controls pass full-byte/multiplicity histories. ASan/UBSan passes
K64 every-A arithmetic, two-byte maps and 64-byte multimaps. These execute NEON
locally; AVX2/AVX-512 source review does not establish native execution.

This driver revision also refreshes untimed memory/population inventories after
each mutation. Earlier panels reused post-build snapshots for update, aged-hit,
erase and reuse rows; their final pre-release snapshots remain current, but those
intermediate fields must not be interpreted as current phase footprints. Their
timings remain evidence for their original driver revision. Missing cohorts now
respect remaining K16 domain capacity as well as K8; prior selected counts were
unaffected. The report checks timed binary hashes and key/value/kind identities.

## Native shared-body packets, 15 October

Zen 5 AVX2 job `20261015T005354Z-ce942885` completes 936 fresh processes,
84,240 phase rows and nine repetitions in each of two process orders. Nine
key/value/kind cases compare both fact representations, scalar/N4/N8 and
ordinary/exact Abseil. Every domain's independent arithmetic check, all eighteen
container checks and the final call-boundary audit pass. Read roots contain no
ordinary calls; their explicit widening multiplies and static stack references
are recorded, not treated as dynamic spill counts or timing predictions.

Selected absolute **reciprocal throughput ns/request**, with original word-plane
facts. These are warmed, fixed-geometry mechanism controls, not serial latency
or a selected layout for these cardinalities:

| Case / keys / operation | Ours scalar | Ours N4 | Ours N8 | Ordinary Abseil scalar | Ordinary Abseil N4 | Ordinary Abseil N8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| K64 set / 768 / hit | 2.412 | 2.526 | 2.292 | 1.460 | 1.321 | 1.130 |
| K16 W2 map / 768 / hit | 2.804 | 2.876 | 2.589 | 1.817 | 1.663 | 1.538 |
| K40 W2 map / 768 / hit | 2.810 | 3.086 | 2.706 | 2.143 | 1.876 | 1.697 |
| K40 W2 map / 768 / update | 6.548 | 8.363 | 7.292 | 5.893 | 4.821 | 4.449 |
| K40 W2 multimap / 192 / hit | 3.692 | 3.945 | 3.340 | 3.509 | 3.299 | 3.092 |
| K40 W64 multimap / 192 / update | 23.274 | 25.714 | 23.452 | 26.845 | 27.619 | 26.548 |

All packet widths have identical stored bytes and tier populations. Initial
logical bytes for the first four distinct cases above are ours/ordinary/exact:
K64 set 9,000/9,208/9,207; K16 W2 map 9,000/5,136/5,136; K40 W2 map
9,000/17,352/9,208; K40 W2 multimap 9,008/8,296/8,296. These controls deliberately
include underfilled and crowded geometries. K40 W64 map at 192 keys, for example,
has a severe capacity loss: N8 hits 29.632 versus ordinary Abseil 13.207 ns,
with 25,808 versus 18,296 logical bytes. SIMD arithmetic cannot repair that choice.
The K8 fixed hierarchy remains disqualified as a production small-table policy.

Across the 26 histories, N8 hits improve by more than 5% over scalar in twelve
and regress by more than 5% in none; N4 has two improvements and ten regressions.
Writes differ: N8 has three improvements and fourteen regressions; N4 has two
and nineteen. These are unweighted descriptive counts, not a workload score.
Abseil also benefits from grouped independent requests, so a scalar-relative win
does not imply winning the matched competitor.

Compact facts improve neither N4 nor N8 hits by more than 5% in any of the
26 histories; they regress eighteen/twelve respectively. N8 K40 W2 map hits
become 3.104 ns rather than 2.706, and misses 2.176 rather than 1.772. Retain
word-plane facts; the compact representation has not earned adoption. Do not
select SIMD mutation merely to share the read implementation.

[All comparisons](evidence/hierarchy-batch-zen5/panel/summary.csv) retain both
baselines and negative cases; [recovery](evidence/hierarchy-batch-zen5/artifact.json)
includes all repetitions, hashes, sources, binaries and disassembly. The
capture manifest is `6b3b15941be6ddc623224374f9ce98430012b337fb5fdfe0b872b6dc69727ecd`.

The matched V2 NEON job `20261015T005359Z-b2c0bafd` also completes all checks,
936 processes and 84,240 rows. It rules out selecting this SIMD-packet lowering
as the ARM default. N4/N8 hits improve by more than 5% in none of the 26 histories,
and regress in 24/20. Writes regress in 26/25, with no improvements above 5%.
Compact facts do not repair this result: N8 has no hit improvement above 5% over
planes and six regressions. Selected V2 reciprocal throughput ns/request:

| Case / keys / operation | Ours scalar | Ours N4 | Ours N8 | Ordinary Abseil scalar | Ordinary Abseil N4 | Ordinary Abseil N8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| K64 set / 768 / hit | 3.785 | 4.913 | 4.741 | 1.987 | 2.016 | 1.729 |
| K16 W2 map / 768 / hit | 4.613 | 5.595 | 4.936 | 2.976 | 2.812 | 2.738 |
| K40 W2 map / 768 / hit | 4.899 | 6.244 | 5.645 | 2.613 | 2.658 | 2.427 |
| K40 W2 map / 768 / update | 9.091 | 13.052 | 13.256 | 6.903 | 7.192 | 6.838 |
| K40 W2 multimap / 192 / hit | 6.171 | 8.395 | 8.431 | 5.173 | 4.314 | 4.261 |
| K40 W64 multimap / 192 / update | 40.208 | 51.000 | 44.631 | 39.488 | 38.262 | 39.137 |

These phases include the same byte consumers and ordered updates. A smaller
state or explicit SIMD label cannot override complete-operation evidence. The
next useful comparison is independent scalar hash scheduling within a packet,
against the explicit vector hash and current scalar caller; secondary facts
should not automatically be calculated for every lane when one lane spills.
Keep lane mutation order and refusal semantics. Public width and execution width
are separate decisions, and a public batch interface must not require every
platform/operation to use the same arithmetic lowering. Gather/scatter and
dependency-aware bucket work remain untested; neither Ring width nor these
hash-only packet experiments establish their benefit.

[V2 comparisons](evidence/hierarchy-batch-neon/panel/summary.csv) and
[recovery](evidence/hierarchy-batch-neon/artifact.json) preserve all contenders,
phases, exact instructions and repetitions. No table format or lifetime boundary
changed, and these results do not supersede the distinct cold Loom results.

## Hash preparation follows the operation

The next paired native study keeps the same lane body and adds two controls:
SIMD primary with per-lane scalar secondary hashes (`_hash1`), and per-lane
scalar hashes in both tiers (`_hash2`). Scalar secondary facts are cached within
one mutation so a miss followed by admission does not hash twice. No lookup
result, mutable rank or binding survives across lane mutations. The original
vector-both-tiers control, scalar calls and both Abseil baselines remain present.

Jobs `20261015T011027Z-cdc95ac3` (Zen5) and `20261015T011027Z-8591997b` (V2)
each complete 1,404 processes and 126,360 rows. All domain arithmetic checks,
27 container checks and 243 complete-read call audits pass on each host.
Their common capture is `6c1c3f2b76806fa94f847d62ed97e66b456e852097957a8040bf35d3d1dfa204`.
Selected **N8 reciprocal throughput ns/request**, with the same data and format:

| Host / case / operation | Vector both tiers | Vector primary, scalar secondary | Scalar per lane | Ordinary Abseil N8 |
| --- | ---: | ---: | ---: | ---: |
| Zen5 K64 set / 768 / hit | 2.348 | 2.248 | 2.394 | 1.120 |
| Zen5 K16 W2 map / 768 / hit | 2.581 | 2.444 | 2.675 | 1.561 |
| Zen5 K40 W2 map / 768 / miss | 1.811 | 1.580 | 1.768 | 0.988 |
| Zen5 K40 W2 map / 768 / update | 7.277 | 7.040 | 6.057 | 4.390 |
| V2 K64 set / 768 / hit | 4.818 | 4.539 | 3.745 | 1.734 |
| V2 K40 W2 map / 768 / hit | 5.502 | 5.296 | 4.907 | 2.430 |
| V2 K40 W2 map / 768 / miss | 4.898 | 3.909 | 3.028 | 1.847 |
| V2 K40 W2 map / 768 / update | 13.028 | 11.942 | 9.024 | 6.518 |
| V2 K40 W2 multimap / 192 / hit | 7.436 | 6.698 | 5.936 | 4.255 |

For V2, N8 per-lane hashing has no hit/miss regression above 5% relative to
its own scalar caller across the 26 histories. It substantially improves the
old vector control, while not establishing general speedup over scalar or
Abseil. N4 still has eight hit and ten miss regressions above 5% against scalar.
For Zen5, vector-primary/scalar-secondary N8 improves fifteen miss histories
above 5% against vector-both-tiers, with two regressions. Scalar lane hashing
usually helps writes but often harms AMD reads. Mutation still has losses;
wide-body preparation and the deliberately crowded formats remain costs.

This favors a fixed eight-request interface with independently selected internal
lowering. It does not favor one hashing strategy for every platform or operation.
Keep compact facts out, keep point operations available, and do not automatically
use a packet path for mutation. AVX-512 qualification of the same controls is now complete below. Caller arrays, code size
and sparse tails still matter; these controls do not establish a gather/scatter
win or dependent-latency improvement. Stored memory is identical across the
three preparation controls.

[Zen5](evidence/hierarchy-policy-zen5/panel/summary.csv) and
[V2](evidence/hierarchy-policy-neon/panel/summary.csv) retain all phases and both
baselines, including the negative crowded/wide cases. Each adjacent artifact
reference recovers the exact binaries, source, complete repetitions and assembly.


## Granite and the batch-width decision

Granite AVX-512 job `20261015T012508Z-9eace7ab` uses the same captured policy
controls, completing all 1,404 processes, 126,360 phase rows, domain checks,
27 container checks and 243 call-free read roots. The
[complete comparison](evidence/hierarchy-policy-granite/panel/summary.csv)
retains losses and both Abseil key encodings.

Selected N8 reciprocal throughput, ns/request:

| Case / keys / operation | Vector both tiers | Vector primary, scalar secondary | Scalar per lane | Ordinary Abseil N8 |
| --- | ---: | ---: | ---: | ---: |
| K64 set / 768 / hit | 2.736 | 2.571 | 2.803 | 1.363 |
| K16 W2 map / 768 / hit | 2.693 | 2.489 | 2.991 | 1.925 |
| K40 W2 map / 768 / hit | 3.153 | 3.055 | 3.233 | 1.978 |
| K40 W2 map / 768 / miss | 2.267 | 2.248 | 2.274 | 1.387 |
| K40 W2 map / 768 / update | 8.879 | 8.324 | 8.997 | 6.635 |
| K40 W2 multimap / 192 / hit | 3.509 | 3.290 | 3.665 | 3.436 |
| K40 W64 multimap / 192 / update | 31.274 | 31.411 | 33.220 | 37.845 |

N8 vector-primary/scalar-secondary improves 17/26 hit histories and 13/26 miss
histories by more than 5% relative to its matched scalar caller; four regress in
each category. N4 improves six/one and regresses ten/ten. Updates remain mixed:
N8 improves three and regresses eleven; per-lane scalar preparation improves six
and regresses six. These counts summarize only this finite fixed-geometry panel.

**Choose eight requests as the fixed batch interface.** Four is retained only
as a diagnostic control. Active masks cover tails and skipped lanes; mutation
retains ordered active-lane prefix/refusal semantics. This is an implementation
recommendation, not a claim that every eight-request operation beats point calls.
Keep ordinary point operations available. Use explicit vector primary hashing
with scalar secondary work as the initial x86 read lowering; V2 uses per-lane
scalar arithmetic for this permutation. Choose write lowering separately: the
measurements do not justify forcing vector preparation into all mutations.
Small-table and other geometry results can refine internal lowering without
changing the public width. A speculative gather/scatter implementation should
have to improve the complete caller before adding another backend.
