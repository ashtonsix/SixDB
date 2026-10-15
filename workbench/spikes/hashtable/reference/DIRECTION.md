# Hashtable: current design questions

**Frozen historical record.** The investigation moved to
[hashtable-design](../README.md) after the
[clean-sheet reflection](REASSESSMENT.md). Its original-brief accounting owns
current scope. Plans below record how this prototype evolved; they are not an
instruction to resume expanding it. The final bounded growth evidence is in
[RESULTS.md](RESULTS.md#bounded-online-growth-and-publication).

The target is a competitive, composable set/map/multimap, with a layout solver
over key geometry, value size, container kind and platform. The present prototype
has not met that target. Its implementation choices are hypotheses, not a format
to preserve. [RESULTS.md](RESULTS.md) owns measurements and recovery references;
this document owns the remaining decisions.

## What the evidence changes

| Regime | Established result | Consequence |
| --- | --- | --- |
| Many small maps | Solver-driven N16 K40/V16 maps win measured reads, updates and churn; construction loses. | Keep a genuine single-bucket list. Its selector is not a multi-bucket hash policy. |
| Small sets and N32 lists | Narrow-set serial reads and several full N32 operations lose. | One successful small-map family cannot select every domain or capacity. |
| Larger maps | Direct transfers still lose important reads; aging and capacity cliffs change both contenders. | Packing alone cannot resolve routing, maintenance and capacity choices. |
| Maintenance | Updating displaced keys into primary vacancies helps aged dependent reads, but slows writes. | Compare total workload costs; one complete update sweep cannot choose a cadence. |
| Table lifetime | Build/probe/destruction wins at 80% primary occupancy on V2/Zen5, but loses near 93%. | Capacity selection and expected reuse are design inputs. Scalar known-distinct rebuilding is only a partial construction control. |
| Primitive composition | Admitted windows closed a substantial read gap; remaining costs vary by operation. | Keep identical-storage direct controls and attribute residual work precisely. |
| Cold lookup | All-tier Ring wins large-footprint Zen5 throughput; V2 loses fresh hits and narrowly wins aged mixed reads. Hot serial loses on all three targets. | Scheduling and memory savings alone do not establish a broadly competitive lookup architecture. |
| Ownership | N16-to-N32 publication loses with little direct/composed gap; larger collections approach replacement parity. | Damage coverage and clone granularity are distinct. Charge both. |

The maintenance comparison uses 101-byte primary and 121-byte secondary
buckets, fixed 256/32 bucket counts and K40/V16. Payload memory is attractive;
independent reads remain roughly twice ordinary Abseil's cost. That bounded
case neither rejects the whole requested design nor justifies more warmed
scalar tuning. The attempted reuse of a bucket product for fingerprints was
rejected on a seed-blind structured counterexample; see
[the hash review](permutation_review.md). No further isolated hash sweep is planned.

## Complete cold lookup: evidence and verification

Use one solver-selected V64 map and flat allocation ownership. Freeze seeds,
keys, values, geometry and insertion/turnover history across candidate execution
arms. Compare ordinary Abseil (with and without its public prefetch), fused
candidate lookup, manual staging (hints off/on), and actual Loom Ring using the
same stages (hints off/on). Secondary routing is computable from the original
key but chosen lazily; it is not an inherent address dependency. Chain directory,
block metadata, StreamPack payload and next-link addresses are dependent stages.
Do not call the synchronous chain reader before yielding and count that as
staging its dependencies. Shared immutable facts belong outside Ring slots.

Use concurrency 1/8/32 with answer-dependent streams. Exact cardinalities need
not be powers of two or divisible by concurrency. Mixed misses must preserve
progression and full footprint; assigning the next key from a missing value
would collapse the trace. Charge operation-state preparation, enqueue, drain,
actual visits and hinted windows. Reusable Ring configuration is separate.

The bounded panel is a hot overhead control, a fresh footprint exceeding both
tables' LLC budgets, the same large case after churn, and an intermediate case
where capacities can cross a cache boundary differently. Add mixed misses to
the aged large case. Report memory and tier/link populations; a small downstream
working set must not be labelled DRAM-resident because the primary is large.
A manual win lost by Ring identifies scheduling cost; a win only at a capacity
crossing identifies footprint benefit. These are different outcomes.

The first executable is `table_ring.cpp`. Its V64 values carry the next request
without a timed external trace. Mixed lookup is approximately two-thirds hits:
half the present keys are preceded by an absent mate. Every arm consumes exactly
the same answers and request graph. StreamPack fixed/full addressing needs no
on-block slot-control read; directory → owner → payload is the real dependency.
That distinction removed one unnecessary visit during review. The potentially
outlined payload resolution uses a same-signature cold continuation; the ARM and Zen5 AVX2
ready callback has no ordinary calls or stack accesses in the inspected build.

The native screen covers V2 NEON and Zen5 AVX2/AVX-512, C1/C32, hot aged mixed, large fresh hits
and large aged mixed, with seven balanced arm rotations. It fixes whole-bucket/
whole-link hints; selective payload hints and C8/intermediate footprints remain
unmeasured. Potential staged hint targets are logical counters, not actual memory
traffic or Abseil hint counts. Scratch reads retain one stable owner throughout;
this comparison does not add publication, mutation, or multi-writer evidence.
Zen5 AVX2 wins large fresh/aged throughput by 2.03x/1.47x with actual Ring;
AVX-512 wins by 1.89x/1.41x. V2 loses fresh throughput by 27% and wins aged mixed
throughput by only 1.12x. Hot serial loses about 33–36% throughout. The cold
opportunity is real on AMD in this case, while the hot execution family and ARM
fresh throughput remain inadequate. Other shapes and seeds are not established
by it. Do not weaken the public-prefetch baseline.

## Architectural reassessment

The latest wider-record experiment removed a genuine interface restriction:
immediate typed value access now reaches routing, mutation, iteration, rebuilding
and staged completion. It also falsified the hope that larger consumers would
make the current row organization broadly competitive. Corrected native runs
lose most operations with composed and direct transfers alike, while saving
only 6.6% of large-case requested payload. One anomalous Zen5 direct whole-record
cell remains unfit for primitive attribution; the independent evidence of broad
loss does not depend on that cell.

The mistake to avoid is treating this implementation as the design and expanding
its coverage indefinitely. The brief fixes the reversible key decomposition,
primary/secondary/chain hierarchy and primitive composition. It does not fix
N16 rows, a secondary one eighth the primary size, byte-wide physical selectors,
selective scalar suffix checks, or ownership checks on exclusive scratch writes.
The solver represents more choices than our complete consumers have exercised.

Three distinctions guide the next work:

* **Evidence lineage.** The historical x86 advantage survives the corrected
  full-value trace, but comes from exact-remainder families and low primary
  occupancy. The later bridge's `current_packed_strong_mixer` is frozen `map.h`,
  not today's `table.h`. Its hash attribution cannot be transferred to the newer
  quotient construction. Primary hits dominated that bridge, so extra chain,
  publication or growth semantics do not explain its hot losses.
* **Search architecture.** Current wide-key lookup does fingerprint match → lane
  selection → suffix load/compare → value access. An exact vector comparison can
  select the lane only after all remainder bits have matched. This changes the
  dependency graph, with a possible throughput cost from extra eager loads and
  comparisons. It must be measured as a complete table operation.
* **Storage and capacity.** Packing fewer key bits does not by itself win memory
  once mostly empty reserved value slots dominate. Primary/secondary capacities,
  value placement and lifetime must be considered together. Increasing occupancy
  without charging spill, aged traffic and writes would repeat an earlier error.

### Completed byte-plane comparison

The K40/V64 hot and large comparison kept seeds, capacity and histories fixed.
It compared current rows, key-first byte planes with selective verification,
and the same planes with exact vector search against ordinary Abseil, including
its public prefetch in the cold comparison. This separated placement from removal
of the scalar suffix dependency. SeriesPack
supplies fingerprint bytes and TuplePack supplies suffix-byte
planes; explicit NEON/AVX2/AVX-512 equality masks combine before lane extraction.
The K40/A8 remainder is 32 bits, so sixteen slots need sixteen fingerprint bytes
plus three sixteen-byte suffix planes. No native-lane padding is necessary for
that candidate. Values remain independently addressable after the match.

The exact matcher also admits a cheaper reversible representation. For seeded
input `x`, current address width `A` and `r = x >> A`, retain
`bucket = (x & mask(A)) XOR H(r)` but store raw `r`. Reconstruction recovers the
low bits with the same `H`, then undoes the input seed. Bucket placement, primary
spill and conditional secondary placement are unchanged. Since every remainder
bit is compared together, a separately mixed eight-bit selector is unnecessary.
Then compare encoded-remainder and raw-remainder exact search on identical physical
planes to distinguish selector work from the change in matching. This is a
whole-container construction change, not a search for a lucky affine seed.

Use real prepared TuplePack identity-plane admission and its bounded native
load endpoint; do not expect generic shuffle controls to disappear automatically.
Keep an identical-window direct control. Check ordinary prepared projections
against the authored plane read, guard its physical extent, and inspect the fused
consumer. Hash inversion, unchanged bucket populations, unused lanes, partial
suffix bytes, dense erasure, all overflow tiers and reseeding belong in the same
experiment. Include changed updates and churn alongside hot serial/independent
reads and complete cold execution. A lookup improvement that loses the workload
or its storage advantage is not a selected family.

The complete native comparison is now in [RESULTS.md](RESULTS.md#exact-remainder-search-on-byte-planes).
All six operation arms and both complete Ring formats pass their checks on V2
and Zen5 AVX2/AVX-512, with unchanged candidate tier populations and payload.
Independent throughput improves, but pure hot dependent hits worsen on all three:
V2 rows/exact/Abseil are 14.38/16.26/9.78 ns; Zen5 AVX2 8.49/9.44/6.21 ns.
Composed/direct differences usually remain small. The cold AMD Ring advantage
survives with a modest increment; fresh ARM remains behind prefetched Abseil.

Reject this N16 exact byte-plane family as the general hot lookup choice. Do not
expand it into multimaps or production presets to justify the work already done.
The result does not test every exact-remainder encoding: four byte comparisons
and their mask reduction are a distinct physical recipe from native whole-word
remainder lanes. Any follow-up must isolate that distinction, not repeat a wider
sweep of this same recipe. Partial suffix bytes have a zero-high-bits invariant,
maintained by initialization and replacement; the exact comparison is not a
projection over arbitrary spare-bit contents.

The comparison holds geometry fixed for attribution; it does not choose N16.
Solver-generated key/value placement and capacity choices across N8/14/16/32
still need physical windows, initialized tails and real spill costs. Native spatial observations may inform two-line candidates but
cannot substitute for measured demand/hint behavior. Do not expand an unsuccessful
family through every key width or add another numbered `Bound` mode.

The historical K16/V64 six-slot, 64-byte family remains a useful regression
reference. Reproducing it under today's ownership and primitive boundaries can
isolate a lost capability; its cheap affine hash is only a diagnostic control,
not an acceptable default inferred from favorable timing.

## Decisions after the broader review

The next whole-container question is **capacity through growth**, not another
sweep of warmed lookup encodings. Fixed `table<P,Q>` makes address width, quotient
width, stride and execution shape compile-time constants. Pre-sized measurements
have not paid to change those bindings, nor established an efficient consumer
boundary when a table grows. A favorable fixed-capacity kernel is insufficient.

Exercise an owner through two geometry transitions, with unknown final size,
reads between transitions, disjoint turnover, a retained reader and clear/reuse.
Give Abseil the same knowledge of eventual size. Charge migration, old/new
coexistence, actual allocation capacity and publication. Compare binding once
around a stable read cohort with point dispatch; do not hide dispatch from the
point interface or reintroduce generic geometry interpretation on every lookup.
The experiment chooses capacity, lifetime and execution together. It does not
need a general-purpose owning framework. A further accidental constraint is the
current hash binding's `A > 0`: it forces at least two secondary buckets, although
a single secondary bucket is a valid power-of-two capacity. The first multi-bucket
growth levels must consider that missing case rather than inherit the floor.
This observation is not a measured selection of secondary capacity.

The completed R32 primary word-tile comparison closes the proposed exact-search
remedy; [its native evidence](RESULTS.md#native-word-remainder-discriminator) loses
hot serial lookup on V2 and both Zen5/Granite ISA builds. Sixteen native words use the same 64 key bytes, value offsets, bucket
hash and secondary implementation as raw byte planes. An authored SeriesPack
byte projection owns byte 3 of each word; TuplePack owns bytes 0–2. The parent
coalesces their admitted physical reads before comparing whole words. This is
explicitly not an unchanged SeriesPack Local view: its tile stride cannot express
these interleaved bytes. The probe retained projection semantics and disjoint
stores, and exposes that composition limitation. It adds no duplicate selector plane or
unaccounted padding. Hot latency remains materially behind: stop
expanding this search family. R32 alone does not select other remainders, sizes,
platforms or a cold policy.

A separate value pool is **not yet justified for the 24-byte consumer**. Its
large measured case reserves 4,718,592 bucket slots for 3,355,443 live records and
requests 129,269,760 bytes. Replacing each inline 24-byte reservation with a
four-byte slot ID gives the optimistic lower bound
`old_payload - 20 * reserved_slots + 24 * live_records / pool_occupancy`.
A perfectly full pool saves at most another 10.7%; at 90% occupancy, only 3.8%;
at 80%, it uses 4.9% more. These modeled figures exclude extra pool metadata,
rounding, ownership and relocation costs, and the added dependent read has not
been timed. Larger records can change the tradeoff, but this calculation does
not warrant building an allocator or assuming separation solves the wide loss.

The common operation driver now includes clear followed by refill. The prototype
clear retains primary/secondary allocations and directory capacity, releases its
chain-block references, and returns the removed count. It prepares every writable
bucket before the first logical store, so a failed preparation cannot expose a
partially cleared table. Retained-copy and all-tier reuse checks cover the new
path. Native lifecycle timings and full publication for it remain outstanding;
retaining bucket capacity is an experiment choice, not a selected clear policy.
The mechanical 3,276-entry driver run makes that distinction concrete: after
clear, the candidate still requests 57,376 payload bytes and ordinary Abseil
requests zero. Refill must therefore be considered together with empty lifetime
and retained memory; faster reuse alone would not establish an advantage.

For writes, admit exclusive scratch ownership once around a batch and compare
with current per-operation sharing preparation under identical geometry. This
small control can establish whether ownership work is material. It cannot explain
read losses, and an insignificant result should end that line of attribution.

The typed value boundary remains useful for a later multimap with inline
singletons and value-only StreamPack chains under skewed fanout. Its complete lifetime comparison should proceed alongside capacity decisions;
it need not wait for a universal hot-lookup winner. The latest Ring table
has only 6,352 chained keys among 13,421,772 keys; it is not evidence for
chain-heavy behavior. Explicit tags must preserve every value bit. Allocation,
singleton promotion, erase-one, erase-key, count deltas, retained dead capacity,
iteration and reclamation must enter the comparison, not just membership reads.

A two-worker count/sum/xor consumer can exercise same-seed merge now: charge
building both local maps, duplicate combination, source/destination coexistence,
and subsequent reads. This uses the existing immediate value boundary and does
not depend on completing multimap storage first. Two N8
children can concatenate encoded fields into an N16 parent with matching hash
geometry, but preserving their tiers also preserves their greater spill. A useful
comparison must charge subsequent parent probes, duplicate resolution and source
coexistence. Chain references require remapping or rebuilding. A fast merge alone
would not answer the present architectural uncertainty.

Keep `table_compare`, `table_ring`, the small-list consumer, solver admission
checks and ordinary baselines active. Historical/numbered Bound, old Ring and
hash/layout variants remain comparators with retained evidence; do not keep
expanding all their panels. One historical exact-remainder comparator preserves
that useful corner without multiplying the next experiment.

* **Capacity and packing:** bucket count, primary/secondary slot capacities,
  physical line tiling, SIMD groups and aged total storage at each growth level.
  A 96-byte 15-slot row bucket is a possible structural candidate at K40/A8/V16;
  its denser packing must pay for higher occupancy and spill. It is not selected.
* **Maintenance and lifetime:** selective hot updates, secondary growth,
  reseeding, bucket-oriented bulk construction and charged source merge. No
  fixed compaction interval follows from fresh-table lookup measurements.
* **Ownership and chains:** flat replacement versus shared chunks under clustered
  and scattered updates with retained readers; pooled geometric StreamPack links
  versus independently grown buffers. The current 256-slot pooled blocks can
  clone large payloads and retain dead assigned ranks. Those are owning-policy
  costs, not intrinsic Orbital/StreamPack requirements.
* **Execution and composition:** exact remainder versus fingerprint plus suffix,
  fused point versus batch 4/8 versus staged access; exclusive scratch authority
  versus sharing checks; precise/bucket/scratch effects around the same stores.
  Shared TuplePack descriptions and generic address expressions still warrant
  attribution, not assumptions about an optimized direct-path ceiling.
  The new 24-byte consumer now executes composed projections through every tier,
  including scalar reseeding. This does not select a wide-value layout or prove
  geometry growth. Its mutation recipe is infallible: multimap allocation needs
  explicit preparation before stores, with one key search and no prematurely
  published reference. Do not silently allocate inside that recipe or add a
  second lookup merely to avoid confronting the operation boundary.

No more numbered `Bound` modes, chunk-size sweeps or generic ownership framework.
A new comparison needs a concrete architectural question. Preserve complete
operations and strong baselines throughout; no correctness-first/performance-later
sequence and no primitive-only win standing in for container competitiveness.

## Required behavior and remaining scope

The requested family covers sets/maps/multimaps; K8/16/24/32/40/48/56/64;
AVX2/AVX-512 on AMD and Intel, NEON and possibly SVE2. Power-of-two bucket counts,
reversible seeded quotienting, separate occupancy/descendant counts, dense deletion
and primary → independent secondary → overflow remain constraints. Fresh caller
entropy on growth must not be derived predictably from the previous seed. Current
mixers are experimental noncryptographic candidates, not a proved attack defense.

SeriesPack stores selectors, TuplePack stores fields, StreamPack stores links.
The solver selects over physical layouts and operation families; it must not
expose every explored implementation option as a user-facing preset. Metadata
must charge actual initialized read/write windows, tails, padding and sharing.
The one-bucket regime is a list; dense-domain storage remains an advisory only.

Sets report existing versus inserted. Map assignment replaces the entire value;
misses preserve the caller's output. Multimaps have bag semantics, preserve
repeated values and distinguish removing one value from removing a whole key.
Full-width values reserve no sentinel for link tagging. Iteration consumes
contents without a required temporary vector. Chain references are representation
local; compaction repairs them through the owner able to enumerate them.

Mutations publish distinct-key deltas; multimaps additionally publish value-count
deltas. Caller heat remains separate. Precise, bucket-level and scratch damage
substitute around the same writes, admitted before the first store. Separated
metadata may need multiple extents; a bounding range would damage unrelated
bytes. Damage admission does not choose cloning. Orbital owns lifetime and
publication; the material chooses its sharing/clone units. Returned borrows and
staged state remain within one stable read epoch, including initialized windows.

Fallible individual mutations leave logical contents unchanged on allocation
failure. Repeated batch keys execute in input order; fallible batches may return
a completed prefix, with transaction-wide atomicity supplied by the owner.
Compare batch widths four/eight before selecting one interface across platforms.
Merge consumes real source contents and charges duplicate resolution, allocation,
publication and source retirement. Maps need an explicit conflict combiner; sets
union and multimaps concatenate bags. Matching seeds/geometry may save hashing
but do not make merge free.

Single-writer and multi-writer contracts remain separate. Multi-writer experiments
include partitioned-key routing and same-seed/same-growth worker-local instances
with charged merge. Strong concurrency, multimap and packed-key baselines augment
ordinary Abseil. Broad throughput/memory advantage, hot serial parity and cold
latency concessions justified by real Ring throughput remain the goals.

The multimap baselines are ordinary Abseil with `InlinedVector<uint64_t,1>` and
Folly `F14FastMap` with `small_vector<uint64_t,1>` (source candidate
[`v2026.10.05.00`](https://github.com/facebook/folly/blob/v2026.10.05.00/folly/container/F14Map.h)).
Pinned Abseil actually holds two words inline at requested capacity one; do not
force it to spill early. Folly's smaller singleton descriptor spills at the
second word, making it a useful independent space/time challenger. Preserve
duplicates; erase-one may swap/pop; consume bags without an output vector. Count
nested bag capacity separately from the outer table. Neither baseline has been
benchmarked here yet.

For worker-local strategies, place those ordinary private tables behind the same
router and merge boundary. A shared concurrent container has different visibility
and does not replace that comparison. If candidate merge consumes its sources,
let the baseline reuse its largest source, move absent bags and concatenate
conflicting bags. Immutable-source and consuming merges are different contracts.

Loom already has qualified adjacent-forward spatial-prefetch observations. Its
current policy does not provide a per-address heat oracle: bind caller-known-cheap
versus unknown at admission/cohort boundaries, retain observation provenance and
storage generation/cache-domain qualification, and avoid per-visit timestamp work.

The remaining implementation includes real growth/clear/merge, multimaps,
multi-writer modes, grown-buffer comparison, full mutation publication, matched
residence/RSS evidence and the relevant assembly/MCA/profiling. Completed bounded
comparisons do not close that scope. Main remains uncommitted.
