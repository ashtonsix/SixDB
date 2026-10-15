# Architectural reassessment, 14 October 2026

Ashton proposes separate fingerprint controls and transposed, shared payload
arrays. This review covers that complete proposal. It supersedes the earlier
recommendation to start with two broad counter-designs; the current table and an
independently fused baseline remain comparison arms. No successor has been
implemented or performance-qualified. Incremental tuning remains paused.

The [scorecard](RESULTS.md) separates serial latency, reciprocal throughput and
memory. The existing cold-throughput wins are substantial: Zen5 fuller C32 hits
are 7.15 ns/query against 17.81 for the better Abseil arm; above-LLC Granite is
22.03 against 31.04, with only one repetition for that large panel. Several hot
serial deficits are a few ns. Their application importance differs from a
similar absolute gap in independent throughput. Ashton accepts that MVCC may
make some mutation deficits tolerable; this is a weighting change, not evidence
that copying, retention or scratch mutation no longer matter.

## What the existing evidence says

- The common-hit loss occurs without appreciable overflow: 6,133 of 6,144 keys
  in the representative K40/W2 history are primary. Hash/selection/addressing
  and consumption need consideration, not only overflow tuning.
- Current complete-row bucket sizing couples search capacity to value width and
  multimap inline capacity. Separating those decisions is a substantive change.
- Several admitted packing transfers compile identically to direct accesses.
  Generic composition is not a sufficient explanation of the remaining losses.
- Short-bag first spill and full lifetimes remain expensive, although established
  appends can win. The first link is already contiguous and per-bag grown buffers
  have been measured; merely saying “contiguous storage” is not a new solution.
- Large linear small-table scans lose throughput despite useful memory savings.
  The low-count regime and its transition must remain explicit design inputs.

## Encoding, controls and all key domains

Let the domain be `2^K`, the number of fingerprint buckets `2^a`, and the number
of buckets sharing a payload array `M=2^m`. The latest brief says every power of
two from 256 to `2^64`: treat that as every integer K in 8..64, including the
previously required K8/16/24/32/40/48/56/64 anchors. Do not silently constrain the
new grammar to byte multiples. Native implementation families can share code.

A seed-keyed bijection produces a K-bit word, split into `a` bucket bits,
`f=min(7,K-a)` fingerprint bits and `c=K-a-f` suffix bits. Require `a<=K` and
`M<=2^a`. In one explicit convention:

```
y = permute(seed, key)
bucket = low_a(y)
fingerprint = next_f(y)
suffix = remaining_c(y)
array = bucket >> m
column = bucket & (M-1)
index = slot*M + column
```

The inverse combines bucket, fingerprint and suffix before inverse permutation.
Array ID and the index's column recover the bucket. The slot is insertion-chosen
and conveys no additional key bits. A packed tail needs its bitmap/select mapping
to recover the column during iteration. These are bits of the permuted word,
not unchanged bits of the original key.

If `c=0`, the occupied fingerprint match identifies a set member exactly, and a
set need not allocate a payload plane. Maps and multimaps still have values.
Overflow entries must retain fingerprint plus suffix, or an equivalent complete
bucket-relative remainder: their physical positions do not carry the primary
control's fingerprint. Domain boundaries, K64 shifts and empty/full cases need
explicit implementations without undefined full-width shifts.

Use one byte per control: occupied fingerprints 0..127, with the high bit set
for empty. For f<7 the unused fingerprint bits are fixed. Equality against an
occupied code excludes empty bytes directly; SIMD also finds empties. This gives
one filtering operation without a separately loaded occupancy count. All controls
must be initialized, including padded lanes. N need not be a power of two; only
bucket count and M have that requirement. Mask or qualify any rounded SIMD tail.

Reversibility is not sufficient mixing. In particular, XOR with a hidden seed,
odd multiplication and then taking low bucket bits preserve equality of the
input low bits: an adversary can construct collisions without knowing the seed.
Use a whole-domain permutation with qualified distribution in the actual bucket
bits and revisit the stated attacker model. A single permutation can produce all
codes but is not automatically faster than the current arithmetic. Measure its
critical path, explicit vector versions, structured keys and growth inversion.
The compact GPU Iceberg implementation's one-round Feistel is prior art for
quotienting, not a CPU or adversarial-security guarantee.

## Lookup, overflow and deletion

The rule “an empty primary slot proves there is no overflow to search” is sound
under the invariant `overflow nonempty => all N primary controls occupied`.
Check every matching primary fingerprint's suffix; a false candidate is not a
negative result. If none matches and the bucket is full, read its overflow entry.
A full bucket can have a null overflow reference. Finding a unique set/map key
ends the search; multimap lookup finds its one key-to-bag entry, not duplicate
key entries distributed throughout the chain.

Keep each bucket's occupied primary slots as a dense prefix. Without overflow,
delete by moving the last primary entry into the hole. With overflow, backfill
the hole before making the result visible, keeping primary occupancy N until
spill is exhausted. Fingerprint, suffix and value/bag reference move together.
Insertion into a full bucket searches overflow for an existing key before adding
one. Overflow deletion can fill its hole from another live overflow slot; its
chain occupancy/lifetime stays independent of primary control occupancy.

Publish the resulting controls, payloads, overflow references and counts as one
coherent interpretation. Temporarily exposing an empty primary control while
spill remains would create a false negative. This is not solved by an atomic
control-byte store. SW/private drafts and retained immutable readers fit; shared
in-place readers need an additional concurrency protocol, not an implied promise.

Geometric link capacities reduce allocation frequency and hop count for long
spill, at the cost of last-link slack and pointer dependencies. Do not assume a
first link of four is good when most overflows contain one key. Keep the grown
buffer alternative and compact short-link families in the comparison. Chain
links remain StreamPack block/rank references with representation-local lifetime;
reusing a rank must not make an old retained reference name new contents. Overflow
keys and multimap values need not share identical first-link/growth policies.

## Transposition, shared arrays and packed upper rows

The layout `slot*M+column` gives arithmetic addressing after a control match,
amortizes array descriptors and allocations, and makes whole-array row scans
contiguous. It does not make one bucket's candidates contiguous: they are separated
by M row strides. That affects multiple matches, bucket scans, deletion backfill
and merge. Sequential whole-table iteration must transpose/control-match its
occupancy information; inactive payload cells need not contain valid values.

A growable rectangular array uses the maximum occupied slot among its M buckets.
One tall bucket therefore reserves payload cells for every column. Holes or churn
without shrinking can make the maximum a historical high-water mark. Larger M
reduces descriptors but increases this effect, ownership scope and false sharing.
An array descriptor is also a possible dependent load; its address is known from
the key early enough to fetch in parallel with controls.

A fixed short base plus bitmap-packed upper rows is a real response to that
slack. For tail row j, an M-bit bitmap identifies occupied columns; its physical
position is `tail_row_start[j] + popcount(mask[j] below column)`. A row-start/prefix
summary is required unless preceding masks are cheap enough to count in registers.
The bitmap and its directory are charged metadata. They cannot be reconstructed
cheaply from all the other buckets' cold controls on every lookup.

The bitmap order creates a mutation trade-off: inserting or deleting one packed
entry moves later entries or rebuilds a bounded tail. Row-local extents reduce
movement but add offsets/capacity/slack; a single packed tail moves more bytes.
An indirection/free-list solution would change the design again and must not be
introduced as a hidden workaround. Make the tail an explicit bounded array-level
rewrite, qualify its cost, and keep the rectangular candidate as a control. M is
therefore also the mutation/retention coupling parameter, not merely a SIMD knob.

The [analytic model](transposed_model.py) assumes independent uniform buckets in
a large domain, solves for 70% occupied primary controls, and uses dense primary
prefixes without historical over-allocation. It is not a timing prediction or a
model of structured/adversarial inputs. Small dense finite domains need a
without-replacement model.

| N | Full primary buckets | Keys in overflow | Absent query with any primary FP candidate |
| ---: | ---: | ---: | ---: |
| 8 | 24.04% | 4.85% | 4.29% |
| 16 | 11.31% | 1.42% | 8.38% |
| 32 | 3.35% | 0.25% | 16.06% |
| 64 | 0.40% | 0.017% | 29.53% |

The last column assumes seven useful fingerprint bits and a large remaining key
domain. Full-bucket directory probes and false fingerprint candidates overlap;
do not sum those probabilities. A full bucket is not necessarily overflowing.
Seven-bit filtering trades fewer spill probes at larger N for more false payload
probes. The lack of a second independently hashed table changes both that balance
and robustness to skew.

For N16/M16 the rectangle needs 1.410 payload slots per primary entry. An eight-row
base plus a perfectly packed tail needs 1.021 and keeps 69.35% of primary entries
in the base; a twelve-row base needs 1.151 and covers 92.04%. These exclude control,
bitmap, offset, descriptor, overflow and allocator costs. Tail addressing is not
rare with the half-height base. With M4 the rectangular slack is smaller; the
model emits all these controls. Cold/hot hit distributions need not be uniform
across slots, especially after deletion backfill.

At N16 in that model, controls cost 1.408 bytes per distinct key; a four-byte
per-bucket overflow directory adds 0.352 bytes, and eight-byte references double
that term. The actual reference width must cover block/rank IDs. Array descriptors
cost `descriptor_bytes / (M * assigned_keys_per_bucket)`. A compact directory may
save space but adds another rank/address dependency. Charge it rather than assuming
per-bucket references or per-array owners are free. Full accounting includes
allocation rounding, all live/retained generations and unique resident backing.

## Growth, small tables and dense domains

A 70% threshold is an initial control, not a chosen policy. Distinguish occupied
primary controls, total distinct keys including spill, and total multimap values.
Track spill separately: primary occupancy alone can stay low while one bucket
collects a long chain. A global capacity threshold must not conceal that case.

Start the comparison with doubling: immediate occupancy drops from about 70% to
35%, whereas quadrupling drops it to 17.5% and enlarges the control working set
more. Payloads can remain lazy, but controls/directories and migration coexistence
still cost memory. Quadrupling is a distinct amortization option if full lifecycle
measurements justify it. Model old/new coexistence under actual retained readers;
full rehash latency cannot disappear from the accounting.

Growth uses fresh caller-supplied unpredictable entropy. A fresh permutation
requires reconstructing each key and applying the new permutation; same-seed
bit splitting is not a substitute. Widths/layout can change with a: suffixes lose
bits but byte-contained fields shrink only at some boundaries. Growth prepares a
new coherent geometry and root before publication. Incremental migration, if
needed, introduces a dual-root lookup protocol and must be evaluated explicitly.

Keep the one-bucket compact-list regime: for tiny n, co-locate controls, payload
and local header in one allocation and avoid an external descriptor/directory.
N may be larger here than in the hierarchy. Do not carry the old 256-entry linear
scan's lookup losses into the new design unnoticed; measure its complete lifetime
and promotion. The hierarchy can start at M1 and increase sharing later. At high
domain density, emit the previously requested bitset+dense-storage advisory, with
all storage terms counted, without implementing that representation now.

## Maps, multimaps and worker modes

Maps replace a suffix cell with suffix/value storage, preserving the independent
control capacity. Keep opaque values W1..64, including awkward widths, in the
solver and consumer measurements. Inline adjacent small values avoid another
lookup; large values can occupy a separate arithmetic region if payload pollution
or alignment merits it. A false fingerprint candidate should not force loading
an entire wide value. Padding, line crossings and selected-field consumers matter.

Multimaps replace the value with a width-dependent inline bag and external reference.
The new layout prevents larger inline budgets from mechanically reducing N, which
is a useful architectural freedom, but charges that budget in every reserved cell.
Two inline values is not a universal answer. Distinct-key count, value count,
empty-bag removal, duplicate retention and erase-one/erase-all semantics remain
separate. Bag spill, erase, compaction and retained versions shape the lifetime
boundary from the outset, rather than being added to a set-only design later.

Key-domain partitioning works with exclusive worker tables/regions and bounded
exception routing. Sharing one transposed array across independently mutable
workers creates growth/relocation coordination and potential cache-line contention.
Either assign the entire shared array to one owner or give workers separate
physical arrays. Original-key affinity and permuted-bucket ownership are not
interchangeable; routing must honor the selected scheme. Rebalance occupancy and
coordinate growth rather than assuming uniform intake.

Same-seed overlapping worker tables align bucket IDs only when permutation epoch,
a, M and interpretation agree. Slots can differ by insertion history, and identical
fingerprints can denote different keys. Merge still compares suffixes, unions sets,
resolves map conflicts under caller semantics and combines multimap bags. It can
avoid unnecessary key reconstruction/re-hashing while the seed and geometry agree;
it is not a memcpy. Widening N during a private merge can be a useful temporary
form, but needs qualified larger scans and a fold back to the selected final form.
Count private inputs, temporary expanded buckets, output, deduplication and any
retained versions in peak memory. Fresh growth seeds must be coordinated, not
kept fixed forever for merge convenience.

## Loom, heat, SIMD and platform selection

A dense control plane can stay cache-resident when payload does not. This is the
strongest cold-throughput hypothesis: negative filtering avoids many payload
fetches, and true hits fetch candidate payloads through known dependencies. It
is not enough to inherit the old Ring speedups; extra descriptor/rank misses or
more bandwidth may offset the gain. Hot paths also pay extra addressing/loads.

Fetch controls and the array descriptor together when their addresses are known.
After the match, compute/hint the valid payload address; fetch tail metadata with
the descriptor where economical. Overflow reference then link address is another
conditional dependency. Fuse ready bitmap/arithmetic work within that stage.
Do not yield merely because two planes are separate, or prefetch invalid payload
cells. Predictable scans may need fewer yields than random cold queries.

Loom LEAD confirms the present Ring supports repeated inline prefetch calls, but
push has only one initial demand. A first active preparation visit can issue the
control and descriptor hints together; pre-hinting at admission can be premature
for overflow/refused work. Each disjoint span needs its own qualified window.
Synchronous Ring visits within a stable owner call need no per-hop Hold. Actual
Task suspension/escape needs retained physical and interpretation lifetimes;
Ring frames do not acquire those automatically.

Caller-held heat is a useful execution input. Today Loom has no bucket/address
heat oracle or recency-update API. Hardware calibration is a prior, and allocation
release temperature is reclamation advice. Keep caller-known direct/cold selection
explicit. A later indicator may distinguish hot controls from cold payload, but
its timestamp, expiry, generation/cache-domain, update and false-sharing costs
must be compared against disabled instrumentation. Completed demand and issued
prefetch are different observations. Heat is physical advice, not MVCC logical
content requiring replicated damage/publication on every read.

Corrected adjacent-line characterization exists. Loom reports conditional forward
benefit on Zen5, mixed/unknown Granite results, and no observed V2 benefit in the
tested histories. The API does not prove a serving cache level or present residence;
ARM runtime fallback is unsupported. Use the [corrected study](../memory-characterisation/ADJACENT.md),
not historical unfenced measurements. N, M, base height, padding, row stride and
native footprint must be selected together for AVX2, AVX-512, NEON and possibly
SVE2, with AMD/Intel distinctions only where measured. Wider vectors do not
mandate larger N; the fingerprint false-match cost is part of that choice.

Explicit SIMD hashing and getK/setK still need complete-operation evidence.
Fingerprint matching, popcount/rank and cross-query packet work are distinct
opportunities. Gather/scatter availability does not make awkward-width payloads
free. Batch writes must resolve repeated keys/bucket collisions in defined input
order and preserve refusal semantics; growth cannot invalidate other prepared
lanes. Choose a public fixed K only after qualification; do not confuse it with
key-domain bit width K used above. The current API packet width remains unselected.

## Composition, damage and protection

Use SeriesPack for the native eight-bit control/filter plane, TuplePack for
suffix/value/inline-vector fields, and StreamPack for chains/bags. Keep native
values and masks available across fused stages. The layout solver describes the
whole search and payload organization for schema, kind, platform and growth level;
it must include c=0, byte-boundary crossings, tags, padding, directory costs and
MVCC footprint, not merely capacity. Compare independently fused complete bodies
as well as primitive micro-operations. Low-level hot calls stay inline; CPS is
used at real owner/latency boundaries. Avoid a public matrix of spike knobs.

Ikea currently reports source-qualified issued writes through `effects.h`.
Hooks run before each local write group, cannot fail/suspend, and include preserved
neighbor bytes. They are neither a transaction-wide preflight nor read protection.
Orbital currently captures immutable material/interpretation and physical lifetime;
it does not expose a general byte-range lock manager. Hold prevents relocation
but does not make contents immutable. A shared owner pointer alone is insufficient
protection for a growable allocation. Orbital LEAD confirms that today's immutable
whole-material snapshot already protects a read; adding per-load registration
just to fill an API gap would be needless overhead.

There are partial access descriptions worth composing: TuplePack's reader/writer
plans expose physical payload read/write envelopes, and StreamPack's
`visit_value_metadata` exposes trusted point-lookup metadata windows. Neither is
a complete operation protection description; borrowed control/layout objects,
payloads, count, actual widened reads and miss dependencies must also be covered.

The new container needs a complete access/protection description: controls and
padding; descriptor/directory; tail bitmap and row starts; candidate suffix/value;
chain and StreamPack sidecars; seed/geometry/root generation. Protect every part
needed to interpret later addresses, including a negative result. Byte read ranges
cannot by themselves express Engine's logical key-absence dependency. Choose
coarse stable allocation/array footprints where correct and cheaper than per-probe
journaling; improve precision only when it earns its accounting cost.

Mutations must admit their complete potentially relocating footprint before
writes and preserve old readers via the enclosing material protocol. Packed-tail
moves enlarge that footprint beyond the target bucket. Sparse transposed bucket
cells cannot be described as one tiny contiguous “whole bucket” span: publish
actual spans or a valid enclosing array extent. Scratch/whole-region/exact damage
substitution stays possible, with pre-admitted journal capacity. Initialization,
control changes, pointer replacement, compaction and byte-preserving SIMD stores
all count. No hashtable-local MVCC or PMR allocator is introduced.

Orbital's existing shared-prefix growth test captures old/new immutable extents
over one Allocation plus a relocation-blocking Hold. It permits writes only to
previously uncaptured bytes; real format padding and preserving stores can make
a seemingly new logical slot overlap old physical reads. Reserve exhaustion
requires refusal or a new retained backing generation. Updating one global base
pointer cannot repair old raw addresses or pointer-bearing Controls. Backfilling
a primary entry or rewriting a retained chain's next/count changes existing
contents and requires material-level preservation. Shared immutable chunks are
available, but Orbital does not automatically select or implement that granularity.

Publish exact distinct-key deltas, separate multimap-value deltas, and geometry/
allocation-root changes with the new representation. Occupancy summaries used by
growth stay coherent with it. Heat observations belong to the caller/Loom policy.
Support scoped get/contains/iteration, insert-or-replace or bag append, erase,
clear, reserve/grow and merge. Results/references cannot escape array growth or
retirement without owner-backed lifetime; clear/release and asynchronous returns
remain part of lifecycle measurements.

## Recommendation and evidence needed

This is a coherent successor candidate: the separation removes a real coupling,
keeps compression and cheap base addressing, and gives Loom useful stages. Keep
the full rectangular and base-plus-packed versions as measured alternatives while
choosing the ownership granularity; do not assume the most compact tail wins.
Doubling is the initial growth control. N/M/base and fingerprint-vs-payload heat
policy are not selected production defaults.

The decisive comparison must put the new complete design beside the current
qualified Ring path and strong Abseil/exact-width/multimap/worker baselines. Report
absolute serial ns, reciprocal ns and bytes separately at hot, LLC and actual
DRAM sizes. Include true/false misses, skew/churn, all domains/kinds, narrow/wide
values, list promotion, first spill/long bags, delete backfill, growth/merge,
retained-reader publication and allocation reuse. The structural model does not
replace native disassembly, llvm-mca, profiling or full-operation measurements.
Keep existing wins visible; an otherwise worthwhile representation can have an
explicit small hot-latency or MVCC-mutation trade-off. No such trade-off has yet
been measured for this proposed representation.

## Sources and reproducibility

- [Abseil control/filter design](https://abseil.io/about/design/swisstables): separate
  one-byte metadata and candidate confirmation. Our overflow termination and
  transposed/packed payload organization differ.
- [Compact Parallel Hash Tables on the GPU](https://pure.tue.nl/ws/portalfiles/portal/349614491/978-3-031-69766-1_16.pdf),
  sections 2.4/4.3: reversible quotienting and implementation-specific Feistel.
- [Ikea owner integration](../../../ikea/docs/integration.md) and
  [issued effects](../../../ikea/include/ikea/effects.h).
- [Orbital current capture](../../../orbital/README.md#embedded-objects) and
  [proposed access path](../../design/orbital/DESIGN.md#future-admitted-input-path).
- [Loom Ring](../../../loom/docs/ring.md) and
  [hardware/heat status](../../../loom/docs/hardware.md).

Run `python3 workbench/spikes/hashtable/transposed_model.py` from the Linux
repository root (prefix with `orb -m ubuntu` in this workspace). It emits JSON
for N8/16/32/64, M1/4/16/64 and half/three-quarter-height bases. Probability mass,
primary+spill conservation and the M1 rectangle identity are checked. The output
is a structural model, not measurements of an implemented table.
