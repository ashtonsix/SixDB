# Improved Boundlist and StreamPack

This is the next design and implementation plan, following the reopened
[encoding assessment](OWNERSHIP.md#geometry-encoding-decision). It specifies a
target, its reasons and the comparisons that settle the remaining empirical
choices. It is **not** a claim that Relative Ends, the new descriptor revisions,
or the proposed mutation helpers are implemented. The current production code
still implements Absolute Ends (`direct`) and Sizes (`width`).

The earlier extraction and ownership qualification remain useful evidence.
They do not close the encoding question. The public documentation rewrite is
parked until this work settles; its outline, imagegen concepts and naming review
are preserved under ignored `build/diagrams/boundlist/` and the imagegen store.

## Decisions that hold the design together

- **Boundlist owns geometry and eligibility. StreamPack owns byte values.**
  U, value packets, U/payload validation, replacement, packing and selected
  transfer stay in StreamPack. Neither module owns allocations, transactions,
  global identities, sorting policy or a scheduler.
- **Offer three stored geometry encodings:** Absolute Ends, Sizes with group
  checkpoints, and Relative Ends with group checkpoints. Fixed size and zero
  geometry remove stored information; they are not extra encoding families.
- **Keep useful omissions and placements.** U is optional; absent U means U=R.
  Retirement is optional. Grouped geometry/U and planar U have different useful
  consumers. Carrier widths and field ordering are not a public layout menu.
- **One source, one version, one useful dispatch.** Shared geometry traversal,
  native decoding and sequential encoding must fuse with the value consumer.
  No projected-view journal identities, extra metadata binding, per-rank erased
  calls or obligatory intermediate endpoint arrays.
- **Do not make StreamPack's storage bounds Boundlist's domain.** The curated
  storage recipe remains 256 ranks, G16 and sizes up to 65535. Generic ordered
  sources remain count-parametric and support wider coordinates and ordered
  nonnumeric domains. Engine's F16/32/64/128 nodes do not acquire a 256-slot header.
- **Judge complete work from its first prototype.** Query preparation, values,
  actual effects, append and packing matter alongside the geometric kernel.
  Neither a correctness-only format nor a fast isolated kernel establishes the
  design. Latency, reciprocal throughput and memory remain separate objectives.

The curated choices are deliberately asymmetric:

| Choice | What earns its place | What it costs |
| --- | --- | --- |
| Absolute Ends | Direct rank access and endpoint comparison; especially useful when the total extent already fits a narrow carrier. | Every stored end uses the global-coordinate carrier; a size change translates later ends. |
| Sizes + checkpoints | Smallest per-rank fields, size-only scans and cheap reservation resizing. | A point lookup reconstructs a local prefix; inverse lookup needs the strongest prefix kernel. |
| Relative Ends + checkpoints | Narrow local ends give direct local access and SIMD endpoint search without the full global-coordinate cost. | Local ends may widen beyond sizes; resizing updates the rest of a group. |
| Optional StreamPack U | Values can use less than their reservation. Omitting U removes real storage and work when U=R. | Storing U adds a field and coordinated value validation/effects. |
| Grouped or planar U | Grouping serves geometry+value consumers; a separate U plane serves U-only scans. | Either order can cost extra cache lines or unused prefix capacity for the other workload. |

Relative Ends is selected for the target portfolio, not yet promoted as a value
format. Its complete-consumer qualification is owed. The same-carrier point
advantage is sufficient reason to do that work; a geometry-only win is not
sufficient reason to replace the other two encodings or change the default.

## Geometry contract and vocabulary

A rank identifies an assigned position. Assigned count includes retired and
zero-size positions. Eligibility is independent of coordinate equality. The
`population` object supplies eligibility bits, not a cardinality. A logical
occupied range whose minimum equals maximum can contain a key; a zero-size
physical reservation owns no positions.

Ordered sources support lower/upper boundary and occupied-extrema overlap.
`lower_bound` finds the first eligible endpoint >=q; `upper_bound` uses >q.
Count is the no-answer sentinel. Overlap returns the first and one-past-last
eligible overlapping ranks; internal holes and gaps within child extrema remain
for the owner to check. Unique-key routing with exact child maxima needs one
boundary search; matches that can span children use the appropriate range query.

Physical reverse is a different projection: point -> (rank, retirement), including
retired reservations and slack. It uses strict end>point without eligibility
selection. Zero-size slots own no points. It is available on admitted physical
geometry with a known origin/extent; an arbitrary ordered maximum array does
not by itself establish containment.

Generic source counts/ranks stay unsigned, bounded by `maximum_count`; larger
outer identities compose local sources. Integral query classification precedes
narrowing. Byte-key order, virtual top fences and wider custom domains keep their
comparison path; neither interpolation nor subtraction is a universal requirement.

| Explanatory name | Proposed maintained C++ name / treatment |
| --- | --- |
| Absolute Ends | `geometry::absolute_ends`, replacing `direct` |
| Sizes | `geometry::sizes`, replacing `width` |
| Relative Ends | `geometry::relative_ends` |
| Endpoint array | Keep `direct_source<T>`: values can be inclusive occupied maxima/minima, not only exclusive ends. |
| Checkpointed deltas | Rename `width_source<W, Point>` to `delta_source<W, Point>`. |
| Checkpointed relative ends | Add `relative_end_source<LocalEnd, Point>`. |
| Fixed slot size | Boundlist `fixed_size`; StreamPack keeps `fixed_reservation`. |
| Geometry location | Boundlist offset/size, `slot_size(rank)` and native `sizes`; StreamPack retains offset/used length/reservation. |
| Physical owner | Boundlist `position_owner`; StreamPack can expose its byte-specific alias. |
| Reusable search binding / pending search | Distinguish source-level `bind_search` state from one query's prepared completion. |

Keep `size()`, lower/upper_bound, overlap, reverse, read16 and the scoped native
chain vocabulary. Preserve encoding values 0/1 for the first two encodings and
use 2 for Relative Ends. Migrate maintained users together; historical results
retain Direct/Width labels with this crosswalk. Avoid permanent obsolete public
aliases simply to compile research controls; use driver-level adapters where an
old captured implementation is deliberately compared.

## Three encodings, one set of geometry operations

For group width G=16, origin C[g], size R[r] and exclusive end E[r]:

```text
Absolute Ends: E[r] is stored.
Sizes:         E[r] = C[r/G] + sum(R[group start .. r]).
Relative Ends: E[r] = C[r/G] + L[r], where L is the local cumulative end.
```

Checkpoints include the origin, one per assigned group, with no terminal
checkpoint. Extent supplies the final assigned end for checkpointed storage.
Relative ends reset at each group; concatenating their raw fields does not make
an ordered global endpoint array. Tail fields in the last assigned group are
initialized zeros and masked outside count. They do not become live ranks.

All three can reconstruct starts/sizes, traverse consecutive ranks and answer
physical reverse. Fixed geometry derives these arithmetically. The stored
geometry emits no payload interpretation or compaction policy.

For a variable curated schema with maximum size M and total extent limit B:

```text
S = native carrier for min(M, B)              # individual size / U bound
P = native carrier for B                     # global coordinate
L = native carrier for min(16*M, B)          # local end
native carrier means the smallest of u8/u16/u32 that proves the bound
```

Fixed size supplies its tighter value bound. U uses its own proven value bound,
not L merely because Relative Ends stores L. A 32-bit local end can coexist with
16-bit U; 16-bit local ends can coexist with 8-bit U. No new maximum-U schema
parameter is needed without a consumer that earns it. Generic sources may use
u64 coordinates or other domains without widening every curated native packet.

These widths are properties of the immutable schema. They must not change
silently as assigned count grows. Changing bounds/encoding means a new layout
and explicit conversion. The old sweep changed B with count; the next one must
also grow partial blocks under one fixed schema.

Full-capacity geometry-only bytes, including D and control, illustrate the
tradeoff. These are layout calculations, not memory-residence measurements:

| M | B | Absolute Ends | Sizes | Relative Ends |
| ---: | ---: | ---: | ---: | ---: |
| 255 | 65280 | 546 | 326 | 582 |
| 4095 | 1048320 | 1058 | 614 | 614 |
| 4096 | 1048576 | 1058 | 614 | 1126 |
| 65535 | 65535 | 546 | 582 | 582 |

Relative Ends earns inclusion especially when L=S<P. The initial compact/read
recommendation is Relative Ends in that region, Sizes when its fields are
meaningfully narrower, and Absolute Ends for the shortest point path. Equal
coordinate widths, partial occupancy, expected lifetime and actual read/write
mix can change the choice. Keep explicit encoding selection available. Do not
publish a new universal default before complete consumers qualify it; the
existing Sizes default is a compatibility fact, not evidence for that decision.

### What does not become another encoding

The stronger full-prefix Sizes inverse has identical bytes. It is an internal
kernel candidate, selected by proven span bounds and ISA, not a new format.
Absolute Ends with redundant checkpoints is an optional search index over
Absolute Ends, not a fourth base encoding. Its point-plus-inverse benefit and
maintenance/bytes stay in the comparison; it is neither mandatory nor rejected.
Expose it through a source composition borrowing caller-owned checkpoint state,
initially derived once for a coherent geometry version. Charge construction,
descriptor/frame bytes and invalidation after endpoint/count/placement changes.
It does not require another persistent format bit or a checkpoint array in every
Absolute Ends block. Eligibility-only changes may reuse unchanged geometric
state only under an explicitly coherent new source binding.

Likewise, native lane widths, 32/48/64/96/128-byte group shapes, padding,
checkpoint/retirement interleaving and prefix algorithms are not independent
public switches. Test an internal variant only to answer a concrete cost question,
then choose a canonical layout or justified specialization. Arbitrary-bit packed
sources remain possible through SeriesPack/TuplePack composition; the native
curated format does not acquire every bit width as a new recipe.

## Physical composition and useful omissions

Retain the existing simple three-window geometry binding, and four-window
StreamPack binding. Initially keep full-capacity directory storage so this
experiment does not hide a second allocation protocol inside the encoding change.
Measure its partially occupied cost; full-capacity Absolute Ends is a storage
choice, not a mathematical requirement of the encoding.

| Window | Boundlist | StreamPack |
| --- | --- | --- |
| Directory | Optional D bitmap, then absolute ends or group checkpoints | Same geometry/population, source plane1 |
| Lengths | Grouped sizes or relative ends; absent for fixed/Absolute Ends | Geometry fields and optional U, source plane2 |
| Control | Assigned count; checkpointed geometry also has extent | Same coherent geometry controls, source plane3 |
| Payload | None | Initialized reservations, source plane0 |

No native prepared object is persisted. Source/schema identity is retained
outside packed payloads. A small immutable prepared layout shares geometry size
facts without storing another full description. The existing 28 B geometry /
44 B value preparation and 16 B typed access are useful footprint controls,
not a reason to reject information genuinely needed by the new format.

For checkpointed geometry with U:

- **Grouped:** one G16 geometry field run followed by its G16 U run. Mixed carrier
  widths naturally produce 48 B or 96 B groups as well as existing 32/64 B groups.
  This is the ordinary complete-value candidate.
- **Planar:** contiguous U supports U-only work. Compare a canonical U-first plane
  with current geometry-first storage; U-first has no larger fixed prefix because
  U's carrier is no wider than the geometry field. Keep one chosen order in the
  new format, not an order flag. Charge unused prefix capacity and copy/read
  footprints at partial occupancy, not only touched fields.

With U absent there is no U placement decision. Absolute Ends and fixed geometry
have no geometry stream in lengths, so their U-only storage also ignores it.
Normalize meaningless placement in new descriptions. Legacy decoding preserves
legacy byte and alignment laws. Required new alignment follows actual carriers;
do not demand `alignas(48)` or `alignas(96)`. Owner alignment and internal padding
are measured separately; a 64-byte base alignment gives different cache-line
behavior from merely natural alignment for a 96-byte group.

`full` continues to require U=R; it is not permission to accept short values
without length metadata. Fixed R can still have varying U. `immortal` omits D.
Zero geometry omits geometry/U facts proven redundant. Preserve schema maximum
and independent extent/byte limit; a small allocation is not an encoding bound.
No new per-slot heat, occupancy counters, string prefixes or fingerprints are
owned by either primitive. A schema can compose SeriesPack prefixes with
StreamPack suffixes without changing StreamPack's byte-value contract.

## Mutation semantics and the efficient shared seam

There are two distinct numeric geometry edits:

| Operation | Absolute Ends | Sizes | Relative Ends |
| --- | --- | --- | --- |
| Move boundary E[r], keep later endpoints fixed | Store E[r]. | Add delta to R[r], subtract it from R[r+1] when present. | Usually store L[r] only. At a group boundary, also move the next checkpoint and adjust that next group's local ends. |
| Resize R[r], translate its suffix | Add delta to E[r..]. | Store R[r], adjust later checkpoints. | Adjust local ends from r to the end of its group and later checkpoints. |

The last boundary changes extent. Both operations preflight ordering, bounds and
effect capacity. They are not interchangeable benchmark labels. Engine confirms
ordinary in-leaf edits need the first law; split/merge/redistribution constructs
one final sorted stream per output node. Arbitrary ordered/custom sources retain
their owner's native/packed writer; this is not a universal arithmetic mutation
protocol for strings or virtual fences.

Keep ordinary checked geometry initialize/append/retire and reads. Add narrowly
named checked numeric `set_boundary` and `resize` conveniences only over the
curated numeric geometry, backed by the same typed author kernels. Do not add
sorting, child movement, deduplication or a generic tree/container editing layer.
Middle insertion/deletion/reordering is an owner-coordinated stream rebuild
with explicit rank/child repair, not a disguised reservation resize.

The shared numeric bodies take admitted columns/count/stride/origin rather than
requiring a 256-rank metadata object. A generic numeric owner can use those
bodies with its own native/packed writer and effects. Search sources do not gain
a mandatory destination protocol. Nonmetric sources keep ordinary comparison.

The important reusable bodies are:

1. **An original-geometry walker**, including retired and zero-size ranks. It
   passes rank, original position and size. Capture original scalar state before
   invoking the sink. Absolute Ends carries the original preceding end. Relative
   Ends must additionally carry the original local end and reset it at G16;
   it must not reload an already rewritten checkpoint or preceding local end.
2. **A sequential geometry encoder** with carried new position/local end. It
   writes caller-chosen sizes and group origins without per-slot checkpoint
   reloads or count publication. It chooses no compaction policy. Group padding
   initialization and already-covered stores are explicitly admitted by the
   enclosing owner; a geometry helper never initializes sibling U by accident.
3. **Native group decoding** into a fused sink. Relative Ends uses previous-local
   ends/subtraction and one group origin, rather than a prefix sum. StreamPack
   projects U or R in that same consumer. Avoid an intermediate decoded array or
   a new packet handoff. Keep a start+U path when R is unnecessary.

A small typed cursor or equivalent inlined state can implement the encoder; it
is temporary computation, not an allocation/resource wrapper. `soft_pack` stays
in StreamPack and composes the original walker, payload movement, U decisions and
geometry encoder in one traversal. Live R can shrink to U, retired R/U become
zero, and assigned ranks/D remain stable. `transfer` preflights the complete
selection before destination initialization, accepts caller order/duplicates,
and can change encoding. Keep additive source/destination dispatch and bounded
operation scratch rather than a codec-pair template product.

StreamPack's ordinary contract remains initialize, append, value/length, range,
select, reverse/overlap, replace/extend, retire, soft_pack and selected transfer.
A reservation-changing logical edit normally appends elsewhere and retargets its
holder. Generic `resize` is not permission to move a StreamPack boundary without
coordinating payload. Soft packing preserves ranks; hard transfer, sorting and
cross-block co-compaction require owner remaps. Persistent ranks remain
representation-local. Clearing and block reuse follow owner publication/lifetime.
Compaction cadence uses workload/slack/retirement/retention evidence outside the
primitive; no automatic timer, sort order or hidden global rank identity.

## Search and execution

Keep exact lower/upper/overlap results across all encodings and eligibility
patterns. Relative Ends routes by checkpoints, converts to a bounded local
query, compares local ends natively and masks inactive ranks. Test repeated
checkpoints, all-zero groups, ties crossing G16, both endpoint closures and
out-of-domain points. A physical reverse must not skip retirement; a logical
boundary search applies eligibility after geometry.

Retain ordinary conditional-select narrowing plus explicit SIMD convergence.
Compare full native scans for small nodes. Keep source-level numeric binding
for repeated large/regular sources and actual interleaved independent batches;
lerp, shotgun and convergence thresholds remain implementation/composition
choices. Do not introduce a public search-algorithm enum. Scalar control and
strongest same-byte Sizes prefix/inverse are always in the comparison.

`prepare` identifies an actual unresolved local dependency; ordinary ready work
stays inline. A pending Relative Ends search has the local field pointer,
first/count/threshold state needed to finish without rereading its global
coordinate or checkpoint. Population selection can additionally borrow the
original named source. Do not let an escaping phase retain a temporary typed
access created in a dispatch callback: the factory must bind selection to that
stable original source, or make the borrow explicit and enforce it. Keep raw
geometry preparation distinct from population selection, and test phase lifetime
with the temporary access destroyed before finishing.

`local_window()` is a hint target, not complete protection. Current typed Sizes
search may read all sixteen checkpoints; generic sources promise only their
admitted checkpoints. Exact read visitors and retained footprints must match
whichever kernels run. All boundaries, population, U, payload and derived search
state belong to one version. Binding/query setup, local completion and actual
payload/child consumption all count in the measured request.

Loom remains caller policy. Compare direct, interleaved inline, manually staged
and actual reused Ring consumers with the same requests and useful results.
Hint after addresses resolve, keep ready arithmetic/native filters fused, and
charge frame state, refill and drain. Ending an Ikea CPS stage is not itself a
Loom suspension. There is no new heat tracker or scheduler in these modules.

## Engine composition without a byte-specific geometry ceiling

Add the generic relative-end source beside endpoint and checkpointed-delta
sources. Reuse their search kernels in the curated recipe rather than building a
second search implementation under StreamPack. Generic Point can be u64 and
origins nonzero; native curated offset packets remain u32 because their proven
physical domain fits. No universal 64-bit key or metric requirement is imposed.

Exercise Engine's F16/32/64/128 exact sources and final sorted-node construction.
Use native-width and prefix-stripped exact controls plus SeriesPack/TuplePack
ordered projections. A packed source binds its decoder once, not at every probe.
Compare complete search plus child access and maintained-view edits, not just
identity transfer between kernels using the same routing machinery.

Two concrete native-density gaps belong in the first Engine-shaped controls:
the present AVX-512 direct-byte convergence mask is limited to 32 ranks, and the
opt-in scan excludes u8. Compare a real 64-lane byte mask and u8 hot scans against
the current path at F32/64/128, including exact tails and result consumption.
Mask carriers must actually hold 64 lanes; scan accumulation must not overflow
byte lanes at the admitted count. Keep the same search surface and widen private
masks/accumulators as needed, rather than adding a byte-guide algorithm menu.

Quantised guides are optional accelerators, not approximate answers from
`lower_bound`. Define guide endpoints first:

```text
Q[i] = floor((E[i] - stable_origin) / 2^shift)
D[i] = Q[i] - Q[i-1]                    # if encoding guide sizes
```

Never round each original size independently. An equal-code interval refines
against exact coherent endpoints; retain the first-greater fallback, eligibility,
duplicate ties and out-of-domain behavior. All-equal/wide ambiguous windows fall
back without a compulsory losing pass. Origin/scale changes invalidate the guide.
Exact aligned quantisation still needs correct query rounding. Test co-located
exact data and guide-only parents under the same Scope. Four-bit deltas and byte
checkpoints are a generic packed-source/native-prefix opportunity if the complete
node earns them, not another mandatory curated encoding.

Engine owns tree/layer ordering, fences versus occupied bounds, summaries,
child references, guide placement and repair. This plan does not reopen their
producer/full-cycle design or require Relative Ends in every node. Bound ordering
is within a layer; this introduces no ordering relationship between layers.

## Effects, allocation and retained readers

Keep `before(source, byte_write)` over the actual named source and issued bytes.
The policy can coarsen to a bucket/Part or compile out for authorized scratch
work. It cannot silently omit preserving stores, vector tails or sibling bytes.
Effects are not beforeimages, read protection, a transaction or publication.
Preflight checked refusals before any write/effect; callbacks cannot fail or
suspend. Consume coherent count/extent and operation semantics for summaries;
do not infer live-count deltas from how many damage entries were emitted.
No new always-maintained occupancy/heat fields are required.

Use Orbital/Loom directly. Hold, virtual reservation, backed bytes, readable
padding and selected immutable body are distinct. An allocation remaining live
or uniquely referenced does not permit overwriting a captured old version.
Relocation ends borrowed access and requires rebinding. Preserve old geometry,
U, payload and interpretations through late as well as already-bound readers.

Ordinary StreamPack append is **not** automatically a disjoint unsealed-tail
write: it updates count/extent, population and sometimes existing readable group
padding/checkpoints. Compare current safe private/full-copy versions with a real
owner composition that privately versions metadata while sharing a payload prefix
and appending only beyond all sealed bodies/padding. Do not invent a local undo
layer or assume selective sharing is already qualified. If publication/access
cost dominates, take that concrete friction to Orbital/Engine.

After packing/sealing, the safe retained prefix includes payload extent (R,
not sum(U)) and every sidecar/readable-padding byte. Owner `relinquish_tail` and
`trim_cold` request live tail reclamation under retained-reader rules. Exhausting
256 ranks does not make a virtual payload reservation permanently resident;
packing also does not automatically return pages. Measure allocation accounting,
RSS, retained versions and actual reclamation separately.

## Portable format and code organization

A third encoding needs an explicit new descriptor, not a reinterpretation of
existing bytes or a silent change driven by CPU flags. Keep eight-byte portable
schemas. A concrete allocation of header bits fits:

| Target descriptor | Byte0 | Bytes1..7 |
| --- | --- | --- |
| Boundlist v4 | bits7..5=4; bits1..0 encoding; bit2 reserved zero; bit3 immortal; bit4 fixed-present | Existing maximum u16, extent-limit u24, fixed-size u16 |
| StreamPack v3 | bits7..6=3; bits1..0 encoding; bit2 planar; bit3 full; bit4 immortal; bit5 fixed-present | Existing maximum u16, byte-limit u24, fixed-R u16 |

Encoding3 is reserved/rejected. Fixed-zero presence stays distinct from absence.
The new header patterns do not overlap legacy headers. Decoding Boundlist v3 /
StreamPack v2 preserves their exact carrier, byte-order, placement and alignment
laws; a decoded layout re-encodes its own version. New preparation creates the
new format. Cross-version conversion uses a disjoint, checked rewrite/transfer;
never change a live version's interpretation in place. Retire legacy writers
only when actual consumers no longer need them; no forced all-block migration.
These are proposed revisions; native wire fixtures/admission tests must verify
them before promotion. Generic source adapters need no portable recipe header.

Compile schema preparation, cold validation and numeric binding preparation once.
Keep typed scalar/native decode/encode and useful sinks visible to the compiler.
Boundlist never depends on StreamPack. StreamPack owns its description/layout and
used companion, sharing prepared geometry facts rather than aliasing BL value
types. Dispatch legal carrier combinations around a complete consumer; avoid
multiplying unused codecs/U policies and source×destination templates. Record
code size and compile time alongside runtime cost when specialization expands.

Keep generic sources and numeric kernels in `boundlist/search.h` and
`boundlist/detail/`; they must not include StreamPack or require the curated
description. `boundlist/{description,view,read,write}.h` owns stored admission and
ordinary geometry operations; author headers expose typed/native/phase bodies.
`streampack/{description,view,read,write,transfer}.h` and its author headers own
value preparation and operations. Preserve the actual two compiled StreamPack
TUs for schema and view admission rather than reintroducing aliases or a second
metadata implementation. No Boundlist `transfer.h` or compaction TU returns.

## Evidence and implementation order

The retained [scorecard](evidence/ownership/scorecard.md),
[all samples](evidence/ownership/encoding.csv) and
[source identities](evidence/ownership/sources.json) are the starting controls.
At N256/M4095 on NEON, all geometry-only, admitted-query timings are:

| Metric | Sizes today | Sizes full-prefix control | Relative Ends |
| --- | ---: | ---: | ---: |
| Dependent point ns | 11.02 | 10.97 | 6.59 |
| Dependent checked reverse ns | 35.84 | 30.47 | 26.31 |
| Independent-eight checked reverse ns/request | 25.37 | 8.28 | 7.74 |
| Reservation-change ns | 14.21 | 14.19 | 18.48 |
| Encoded B | 614 | 614 | 614 |

The point advantage is real evidence. The 3.3x reverse claim is against a weaker
Sizes kernel; the stronger identical-byte control narrows that gap substantially.
The old mutation is suffix translation, not Engine's separator edit. The rebuild
timer also generates fixture widths/population and is not a pure bulk encoder.

Other limits matter: the sweep couples B=count*M, excludes native u8 coordinates
and relative-end fields, and does not time complete Relative Ends StreamPack
values/append/packing/U. Geometry backing bytes are not per-arm RSS or proof of
DRAM service. In particular, the narrowest large population fits Granite's
reported LLC. Do not turn these gaps into rejection of Relative Ends or evidence
of broad graduation.

Implementation proceeds in question-sized increments, each with an independent
oracle **and** a timed complete consumer from its first compiled candidate:

1. Add Relative Ends/source and the strong Sizes inverse through the shared
   typed/native bodies. Start with M4095 and independently tight B65535 controls,
   alongside M255 and the M4096 widening boundary. Include actual point/value
   and native consumers, append and one-pass packing immediately. Keep current
   production and independently fused ISA controls in every initial panel.
2. Resolve geometry/U carrier separation, grouped versus the one planar order,
   boundary-edit versus resize laws and schema/occupancy crossovers. Exercise
   zero/full/fixed/immortal facts. Implement the shared sequential encoder and
   coherent cross-format transfer, not a separate compactor for each codec.
3. Run the selected wider matrix and actual owner/Loom/Orbital material; diagnose
   linked consumers with disassembly, llvm-mca and profiles/counters. Fix strong
   same-byte baselines as well as candidates. Use observed cache behavior and
   useful whole-operation costs to choose defaults and any internal specializations.
4. Promote only the choices with distinct useful roles and a complete explanation
   of remaining losses. Rebuild the ordinary/composition examples, header checks,
   wire/admission/effects and retained-reader coverage. Then resume the public
   documentation rewrite and replace stale source/test guides together.

No repeat of the entire historical algorithm tournament is required. Existing
search paths remain controls; reopen one only when a diagnosed consumer gap
justifies it. Keep primary ownership in Ikea; Engine supplies its actual consumer
assessment and owns directory changes. Independent work uses captured sources
and explicit resource ownership, not overlapping mutable benchmarks.

The coverage below is a basis of boundary and representative mixed cases, not
an unbounded Cartesian run requirement:

| Dimension | Cases and comparison |
| --- | --- |
| Schema | M=0/1/15/16/255/256/4095/4096/65535; valid B around255/256,4095/4096,65535/65536 and capacity*M. Hold B fixed while varying occupancy. Exercise native u8/u16/u32 end fields and wider generic coordinates. |
| Population | Assigned0/1/15/16/17/31/32/33/64/128/255/256; live-zero, retired, long empty groups, duplicate ends; generic F16/32/64/128 and existing large binding controls. |
| Values | U absent/present, fixed R/varying U, U0/R0/full reservations; short/skewed/long-tail values, different rank/field arrangements, valid aliases and partial native groups. |
| Reads | Domain/query preparation + result-dependent C1; genuine independent C4/C8/C16; ordered/shuffled select, range, native predicate/reduction, logical overlap and physical reverse. Charge scratch setup. |
| Writes | Append including group starts and capacity failure; replace/extend/retire; set-boundary vs suffix resize; complete node construction; soft packing with/without shrink; transfer/reorder/duplicates and cross-format conversion. |
| SIMD | Explicit AVX2 and AVX-512 on Zen5 and Granite; NEON on native Neoverse. Include V3 when available; SVE2 is exploratory. Disable first-party loop/SLP vectorization; inspect actual emitted ISA, dependencies, mask reductions, calls and spills. |
| Residence | Small working sets, LLC populations and measured above-LLC streams. Qualify each host/service claim using shared residency/PMU tools; no cache labels from aggregate bytes alone. |
| Owners | Direct/manual/actual Ring, same requests/outputs and coherent footprints; old/fresh/late readers; metadata/private versus payload-shared drafts; relocation/refusal, seal/tail reclamation. |
| Costs | Absolute C1 ns/request, independent ns/request, ns/block and bytes moved for packing. Encoded/accessible/reserved/backed bytes, padding, scratch/frame memory, RSS, retained bytes, build/code size and publication separately. |

Adoption compares semantic outputs, not identical damage checksums: different
encodings legitimately issue different writes. Independent models verify every
changed byte is covered and every reported span is admitted. Add literal new/old
wire fixtures, protected tails, maximum ranks/sizes, arithmetic overflow,
refusal-before-write, original-geometry capture under rewrite, pending-source
lifetimes and model-based histories. Do not use a writer/reader round trip as the
only oracle. Separate direct primitive cost from full publication cost; the
existing 192 full-copy histories do not establish selective-sharing economics.

Choose explicit tradeoffs per workload rather than hiding losses in a median
across unrelated cases/units. No mandatory percentage threshold substitutes for
those judgments. Retain relevant winning/losing cells, repetitions and source/host
identities; raw assembly/binaries/captures stay in recoverable ignored/S3 storage.

## Plan checks and deferred documentation

`design-check.py` independently exercises append, boundary-edit, suffix-resize and
in-place carried-rewrite laws, including G16 crossings, zeros and high origins.
It checks the footprint arithmetic against all 16,400 retained rows while honoring
the old driver's u16 minimum, compares the two planar prefix orders, and verifies
the proposed header spaces do not collide with legacy tags. The current run
covers 81,138 operation-model cases and 460 placement cases. This verifies design
arithmetic, **not** native
performance, emitted instructions or implemented wire compatibility.

The docs restart remains authorized but parked. Once the encoding work settles,
write a concept-first reference and task-led usage guide from scratch. Explain
rank/eligibility, equivalent three-way geometry, and staged dependencies before
headers and low-level obligations. Rework the imagegen compositions to cover
Relative Ends, then generate checked accessible SVGs and inspect them at rendered
Markdown width. No SVG has been created yet. Correct old `docs/source.md` and
the Boundlist test guide too: they still describe U/transfer/aliases/TU ownership
that no longer match the current implementation. Preserve historical evidence
as history rather than another current API specification.
