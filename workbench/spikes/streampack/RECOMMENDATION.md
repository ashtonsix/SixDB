# StreamPack implementation recommendation

This records the implementation decisions, including the subsequent fixed-capacity
selection. The production API and its qualification are now described in the [Ikea guide](../../../ikea/docs/streampack/usage.md)
and [implementation findings](IMPLEMENTATION.md).

Implement StreamPack in Ikea now, using the spike as evidence and a source of
tested kernels, rather than promoting its experiment types wholesale. The useful
result is a small headless packing contract with two geometry families, cheap
ordinary calls and operation-specific native composition. Another broad layout
tournament is not needed before beginning that work.

StreamPack owns the interpretation and operations over caller-supplied bytes.
The enclosing collection owns allocation, visibility, logical references and
maintenance policy. Curated sidecars should make that division convenient at
the call site. These are implementation recommendations, not an already shipped
API or a frozen wire format. [Design](DESIGN.md) explains the model;
[results](RESULTS.md) preserves the measurements and their boundaries.

## What the whole spike taught us

The primitive is competitive enough to implement. Compact width geometry earns
substantial large-population and reverse-lookup wins; it is not just an RSS
optimization. Its hot dependent lookup and ordinary point mutation can still
lose. Strong descriptor and libc controls remain relevant, especially where
inline values avoid a dependent load or fragmented storage creates many useless
physical search candidates. There is no supported claim of universal superiority.

The larger opportunities came from doing the right work at the right grain:
explicit prefix/reverse decoding, running geometry for ordered traversal, and
preparing starts once for a dense shuffled operation. Full-MVCC widths save
roughly 5% of the measured mixed workload after closing the large x86 projection
loss. That result includes owner copying and publication; an isolated slower
writer did not predict it. Conversely, faster updates alone did not guarantee a
faster mixed workload. See [full publication](RESULTS.md#full-widths-publication-and-scheduled-consumers).

Field placement provides useful, usually smaller, conditional gains. Grouping
R/U helps the measured large overlapping value lookups, while dense U-only
streams favor a separate U plane. Padding and packing are not automatically
better. The corrected comparisons use explicit SIMD and inspected native code;
earlier compiler-selected layout rankings are withdrawn. The residual Zen5
ordered R+U gap has no established hardware explanation. It should not become
an architectural rule.

Ownership has the greatest consequences for composition. Publication granularity,
unchanged-Part copying, retained generations and when compaction runs can outweigh
the kernel differences. The reader-phase compaction controls changed complete
work by roughly 47–56% despite similar counted copying. A general StreamPack
allocator, transaction layer or fixed cleaning cadence would hide the wrong
decision inside Ikea. [Lifecycle evidence](RESULTS.md#sustained-allocation-and-publication-control)
belongs with the owning collection.

## Initial storage choices

Keep **Direct** and **Width** as the semantic geometry names. Endpoint width,
checkpoint width and ISA are implementation choices, not additional public
families such as Direct16/Direct32. All fields derive their legal range from the
schema. Native 8/16/32-bit storage can be the first implementation of those ranges;
a 12-bit maximum does not require every caller to pay generic packed extraction.

| Initial choice | Layout and intended use |
| --- | --- |
| Direct | Exclusive reservation endpoints, optional used-length plane, optional tombstone bitmap. Begin with native separated fields as the simple mutable baseline; preserve running endpoints for scans. |
| Width | Exactly `(N+15)/16` group-start checkpoints, with no terminal checkpoint, plus groups of sixteen reserved and sixteen used widths. Put the full tombstone/checkpoint directory first; the caller can grow the length region. This is the compact general-purpose candidate. |

Direct value lookup needs a preceding endpoint; Width needs a local width prefix.
That different input set is a reason to allow different physical recipes beneath
the same value/range interface.

For Width, use schema-sized checkpoints and naturally sized R/U lanes. At N256,
u16 R/U makes a 64B length group. A u32 checkpoint/tombstone directory owns 96B;
aligning the following length region to 64B adds a 32B gap. With u16 checkpoints
the directory owns 64B. These are format/alignment facts, not a requirement for a
particular allocator capacity class. Growth measurements used a captured Orbital
runtime; verify current allocation economics in the implementation caller.

Keep a dense separate-U option available for length-dominated consumers, but do
not make every application choose independent padding, planes and decode knobs.
The common schema facts should eliminate metadata: full reservations derive U;
immortal slots omit tombstones; fixed width can derive geometry. A maximum-zero
schema must work, including count-only appends. Those are semantic specializations,
not reasons to create unrelated containers. An unsupported mutation of an omitted
fact must refuse or require an explicit representation transition.

Do not select padded Direct groups or generic packed SeriesPack/TuplePack adapters
as the universal mutable default. Their read gains have countervailing update,
reverse or point-throughput costs. Preserve the substitution seam, and use the
existing primitives when bit packing is selected; do not write another general
bit codec. Shared schema preparation and native group outputs should compose
without per-slot generic routing. The small fixed tombstone bitmap needs no
compressed-bitset machinery in the initial recipes.

Use **256 slots per block**, for both Direct and Width. This replaces the earlier
recommendation to retain 128/256/512. The decision balances measured whole-copy
publication costs for mixed transactional/analytical use; it does not assert that
256 wins each workload. On Zen 5, batched work takes 13.852/9.876/6.833 seconds at
128/256/512, while isolated edit+admission+maintenance takes
15.318/20.648/33.654 µs/edit. We accept the smaller block's isolated-edit advantage
and the larger block's batched advantage to have one physical grain. The same
opposing trends appear on c9g; [capacity evidence](RESULTS.md#capacity-and-reader-phase-controls)
and [isolated edits](RESULTS.md#one-edit-publication-and-byte-budget-sealing) retain
exact workload definitions and their whole-copy-owner limitations.

256 gives byte-sized rank identities, a 32-byte tombstone bitmap and exactly
sixteen Width checkpoints: a 64-byte directory for maximum 255, or 96 bytes with
32-bit checkpoints. The maximum body extent fits 24 bits. Capacity is a compile-time
constant, absent from the schema and encoded description; transfer needs only
2 KiB of coordinate scratch. The version-2 description occupies eight bytes.
Keep the body-byte limit independent: forcing 16-bit endpoints would still cause
premature sealing and extra allocations. The production [reference](../../../ikea/docs/streampack/reference.md)
owns the exact encoding; the comparative research retains other sizes as evidence.

## A small ordinary interface, with native composition underneath

Use the same separation as the other Ikea primitives: a physical description and
prepared operation, a named borrowed storage binding, and typed author bodies.
Keep shared schema/ISA preparation separate from per-block pointers and count.
Ordinary callers should supply a preset and owner windows once, not assemble
child codec maps or keep a large self-referential adapter alive.
Persist the physical description, not native prepared controls or pointers.
Specify stored byte/bit order and occupied-tail rules independently of execution
ISA, so the same representation can be consumed on ARM and x86.
Resolve dynamic recipe selection outside rank loops. Specialize the small native
width/execution cases without instantiating a whole container for every combination
of maximum, placement and ISA.

| Surface | Required behavior |
| --- | --- |
| Describe and bind | Resolve schema bounds, optional facts, physical extents/alignment and execution preparation; borrow payload, sidecars and coherent count/occupied extent. |
| Construct and transfer | Initialize caller-provided storage, or copy selected source values in caller-chosen order. This supplies the kernel for hard rewriting without owning its allocation or deduplication policy. |
| Point and reverse | Read a live value span or its length; map an occupied byte offset to rank and tombstone, including reservation slack. Empty slots own no bytes. |
| Range and selection | Preserve requested rank identity, duplicates and order; share decoding across useful batches. Expose native spans/masks to composed consumers. |
| Mutate | Append, replace within a reservation, retire and rank-preserving packing, with command admission and issued-write effects. Reservation-changing logical edits append and retarget through the owner. |

Keep rank renumbering explicit through transfer and remapping; vector-style
insertion or erasure must not silently change the identity held by callers.

Do not force a point read to materialize a complete slot descriptor or decoded
directory. Do not implement a range as repeated erased point calls. Ordered
traversal carries its position; dense shuffled traversal can pay for temporary
starts once; sparse selection retains a direct path. Preparation and scratch
belong to the operation that uses them and expire when its geometry changes.

Separate structural binding from validation of externally supplied encoded
contents. Checked mutations preserve an admitted representation. Refreshing a
window after append must not rescan the whole valid block. Recovery or untrusted
input needs content validation, including endpoint monotonicity and coherent
count/extent. New checked commands still validate their rank, value, capacity,
aliasing and effects requirements before any store.

Implement explicit NEON, AVX2 and AVX-512 point-prefix, reverse and batch bodies
where vector work is useful; retain simple scalar endpoint access where that is
the operation. Tune AMD and Intel separately only where matched evidence warrants
it. SVE2 remains an extension. Inspect complete linked consumers, including effects
and sinks, and measure dependent latency as well as independent throughput.
Neither forced inlining everywhere nor a blanket widest-vector rule is justified.

CPS should wrap useful memory-dependent work using the existing Ikea/Loom chain
machinery. Keep ready arithmetic inline. The actual Loom Ring lost to the best
direct/manual controls in the regular tested workload, so it should be an optional
caller composition, not the ordinary StreamPack route. Irregular admission/refill
may change that result; no new scheduler belongs in this primitive.

## Publication and maintenance are part of correctness

Keep direct Orbital allocation in the owner. Growth can succeed in place;
relocating growth invalidates borrowed pointers and requires rebinding. Newly
exposed extents require initialized, admitted windows in either case. Resolve
aliased inputs before relocation. A retained allocation is not permission to mutate an old
reader's interpretation. Payload, sidecars, count and occupied extent must belong
to one coherent version.
A sidecar is a logical role, not a requirement for another allocation or Object
Part: caller-provided windows can be colocated when their admitted physical
extents and writes permit it.

Use Ikea's existing source-qualified damage mechanism, with a stable named view
and a calculable effects-capacity bound. Report actual issued stores, including
preserving stores to packed siblings, initialization and caller-control storage
the operation writes. Refusal preserves all supplied data, control and effects.
Preflight cannot suspend between proving capacity and issuing stores.

Count and occupied extent must be available coherently, without inventing a
sentinel we removed. Sentinel-free Width needs caller-owned extent; Direct can
derive it from the last assigned endpoint and need not store a duplicate word.
A zero-byte append changes count alone; soft packing may change extent without
changing checkpoints. Holders, remaps and logical summaries
remain owner obligations alongside physical damage. Do not add a second generic
change log to compensate for an underspecified owner handoff.

Soft packing preserves ranks and clears retired reservation widths; shrinking
live slack is an explicit choice. Hard rewriting composes construction/transfer
with caller-selected ordering, deduplication and repartitioning, and publishes
the complete remap with actual holder repair. Shared values require detach on
edit and retirement after the final holder leaves. Ranks remain representation
local. Positive reservations stay disjoint, so reverse lookup has one physical
owner; overlapping string encodings would need a different explicit contract.

Recommend pressure-based maintenance with opportunities to exploit reader release
and known destination sizes. Do not adopt a fixed interval or automatic sorting
policy from these experiments. Sorting improved a scan without establishing a
meaningful complete-workload win; retained generations changed the economics
much more. Bounded maintenance and repair-index policies belong to the collection.
String prefixes, encoding and lexical ordering remain outside StreamPack.

## How to carry this into implementation

Start with one ordinary mutable Width caller using the selected directory/group
recipe and one Direct control, through the real Orbital owner. That makes the
public binding/effects contract concrete while implementing the primitive:
partial groups, empty and maximal values, omitted facts, append, refusal,
relocation/rebind, old readers, soft packing and hard transfer/remap. Include the
maximum-zero schema, count-only changes and extent-only changes. The older MVCC
fixture proves these obligations for its own layout, not for the new recipe.

Keep the independent history, byte-owner, aliasing and afterimage oracles. Port
selected typed kernels and their boundary tests into Ikea; leave experiment
adapters, comparative formats, workload generators and evidence in Workbench.
Exercise an ordinary caller and a fused range/selection consumer using the same
semantic bodies. Reuse existing effects and chain facilities; extract a new shared
abstraction only when another Ikea primitive has the same concrete requirement.

The remaining performance questions limit claims rather than block this start:
skew/changing hot sets, long-value bandwidth, descriptor relocation/repair,
bounded maintenance under reader retention, multiwriter behavior and a strong
mutable-container baseline with matching ownership/effects. Add those cases as
their consumer is implemented. The current external read/search controls do not
establish best-in-class transactional writes. Preserve the measured losses as
regression targets instead of promising broad dominance from a layout alone.
