# Packed hash-table design direction

A table associates fixed-width integer keys with membership, one opaque value,
or a bag of opaque values. The key domain is exactly 8/16/24/32/40/48/56/64 bits.
Values have a schema-known width of 1–64 bytes; strings and variable-size objects
can be represented by caller-owned references. Multimaps preserve duplicates.
This document describes the implementation direction established by the spike,
not a claim that every experimental control is a production feature.

## Storage and routing

A tiny table is one compact list. It does not allocate two mostly empty hash tiers.
The experimental list grows privately through Orbital and admits up to 256 keys;
promotion is triggered by an actual absent-key capacity refusal. Duplicate updates
must not trigger promotion. K8 can remain a list for its complete domain. The
256-key ceiling is implemented; the complete cost of tiny owners and lookup is
still a performance issue.

Larger tables use a primary bucket array, a separately sized secondary array,
and overflow links. Bucket counts are powers of two; **slot counts are arbitrary**.
A reversible seeded permutation divides the key into location bits, fingerprint
and suffix. Location reconstructs omitted bits. A matching fingerprint is only a
filter: suffix confirmation establishes equality. Secondary routing uses its own
mapping. Growth obtains new caller-supplied seed material and reencodes keys; the
experimental mixer is not an established adversarial-security result.

A bucket stores occupied entries densely. Deletion swaps its final entry into the
vacancy. Occupancy and descendant counts are separate, so a primary vacancy cannot
hide an existing downstream key. The layout solver considers fingerprint/suffix
width, value/tag width, container kind, physical unit size, slots and platform.
It uses SeriesPack for selectors and TuplePack for record fields. Solved geometry
is an implementation recipe, not a public format choice for every measured flag.

The primary and secondary capacities, suffix width and growth threshold must be
chosen together. Doubling is the working growth direction; 90% is a qualified
experimental trigger for larger slot families, not a universal promise for all
formats. The latest actual Worker comparison keeps completed occupancy deltas,
with at most one admitted 64-record batch crossing the trigger. It advances a
14-slot family as the next wide-map control, while narrow owners retain their
incumbent geometry. Neither a universal slot count nor final platform presets
have been selected from the whole residence/growth surface.

## Values and multimaps

Sets have no value plane. Maps store their fixed-width value with the key suffix.
A multimap record has an inline byte budget and a separately encoded count/tag.
Inline capacity is floor(budget/value width), not a fixed number of values. The
implemented experimental budgets cover multiples of eight through 64 bytes.
The external form identifies representation-local storage with a compact reference
and insertion hint; neither becomes a stable cross-representation address.

External bags currently use geometrically growing StreamPack links. Pooled
contiguous vectors and direct grown Orbital buffers are implemented comparison
owners. None has earned universal selection. Enlarging inline budgets or shared
blocks can reduce allocations while increasing bucket/retained-version costs.
Wide bags remain a significant throughput and memory deficit. Per-value erasure,
long churn, reclamation and retained-version cost are unfinished selection inputs.
Whole-key deletion is distinct from erase-one.

## Operation and composition boundary

The small interface direction is point lookup, insert/update, deletion, clear,
bounded traversal, eight-key get/set packets, caller-supplied reduction and
bounded merge. Sets union; maps either replace or apply the explicit combiner;
multimaps append. Overlapping worker reduction does not define a total order for
arbitrary overwrites or deletes. Those operations require a caller ordering/version
policy rather than an accidental merge rule.

Ready lookup and value consumption fuse into synchronous callbacks. They borrow
bytes only through callback return. A continuation is used at a genuine dependent
memory/ownership boundary, not around every primitive call. Eight is the chosen
batch interface; NEON/AVX2/AVX-512 implementations use explicit SIMD, and read/write
lowering can differ. N8 batching is separate from Ring concurrency or 64-record
transport admission. The shared compact-window selector did not earn selection.

Mutation admits capacity and observation space before committing a logical group.
It reports distinct-key/value deltas. An ordered packet can return a committed
prefix and an untouched suffix, which the caller retains across promotion.
A failed group commits no values; preparation may retain unused reserved capacity.
A multimap fold can combine consecutive same-key values into one reservation.

Damage, preservation and logical deltas are separate. A table can replace primitive
leaf damage with whole-bucket observation or inline away scratch observers.
An issued-write range is not an undo journal. Orbital growth may retire old backing
before returning, and directory growth may invalidate owner records. A finer
retained-reader consumer needs pre-growth/retirement preservation of initialized
bytes and relevant metadata; copied prefixes are not automatically issued damage.
No post-grow event or physical Hold establishes this guarantee by itself.

## Orbital, Loom and multiple writers

Orbital owns allocation/growth. A private table stays bound to its owning Pool;
moving an Allocation does not transfer that authority. No replacement allocator
or PMR layer is part of this table design. Existing ObjectStore tests publish
independent whole-material bodies and preserve old discovery/read scopes. They
do not implement fine-grained byte-COW.

Cold execution uses Loom to hint a physical window, yield, then resume under a
retained interpretation. Wide maps benefit from a separate located-value stage;
narrow values often favor a whole-bucket hint. Warm tables normally use the fused
direct path. Caller-held execution selection follows Loom's qualified procedure
pair/identity/epoch policy; hardware neighbor-prefetch characterization does not
itself identify whether a particular table is hot. Hint ranges are not access or
protection grants.

Multi-writer modes are private key partitions with exception routing, or private
overlapping aggregation followed by fold. The actual Worker fixture bounds intake
credits, returns occupancy deltas, and coordinates prepare/cancel/commit growth.
It does not use exact future union cardinality. Work balancing and key-population
balancing remain different concerns.

The cross-Pool fold keeps an immutable source on its original Worker, copies bounded
chunks, and grows only the private destination. The source cursor and interpretation
survive destination cancellation and retries. It retires the source on its own
Worker after all accepted chunks finish. Both contenders pay this handoff. The
current protocol serializes chunks; pipelining and same-seed bucket-wise union are
unmeasured opportunities. Retained-reader destination publication is not established
by scratch fold alone.

## Code map

| Concern | Main source |
| --- | --- |
| Headless list/bucket operations | `list.h`, `record_table.h`, layout solver/recipes |
| Experimental owning compositions | `small.h`, `promoted.h` |
| External values/key-overflow storage | `links.h`, `bag_buffers.h`, `inline_bag.h` |
| Growth and bounded transfer | `migration.h`, `consolidation.h` |
| Actual Worker consumers | `worker_owner.h`, `worker_epochs.cpp`, `worker_fold.cpp` |
| Retained whole-material publication | `publication.h`, `staged_publication.cpp` |
| Explicit batch hashing | `hash_packet.h` |

The spike's compile-time growth alternatives, duplicated diagnostic bodies and
flags are measurement machinery. A production owner must not store one inactive
owner for every possible growth level. The [graduation assessment](GRADUATION.md)
separates established boundaries from the remaining performance/design work.
