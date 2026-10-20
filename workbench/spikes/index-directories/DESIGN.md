# Directory layout and paper design review

The choices here are implementation candidates. Segment identities, physical-key
order, layer geometry and query semantics remain the supplied model.

## Keep three independent choices open

Directory facts are segment existence, routing range, representation and Part
locations. Summary validity and pending corrections are data facts. Physical
update and publication grain are a third choice. A record write does not force
all three to change. In particular, the directory must not manufacture five
unchanged deltas, walk five layers, or capture a replacement merely to confirm
that its facts did not change.

| Choice | Concrete candidates to retain | Costs and correctness obligations |
| --- | --- | --- |
| Routing range | Exact occupied bounds; stable monotone enclosing fences supplied by placement | Exact bounds can require frequent repair. Stable fences reduce edits within padding but can visit extra segments; exact segment refinement still determines query results. |
| Summary maintenance | Direct eager; eager with private shallow contributions; simple bounded append plus invalidation/pristine metadata; optional dirty-prefix index | Base data, pending corrections and validity must agree at the reader's snapshot. Dirty reads correct, descend/bypass or trigger coherent flush; no stale summary is consumed as exact. |
| Physical data update | Disjoint append with stable Part addresses; changed selected metadata/geometry with retained data; replacement/port; a version-qualified resource locator | Price actual byte changes, ownership, selection and publication. A raw-byte COW locator that changes requires a directory edit; that coupling is a candidate cost, not an index requirement. |
| Publication grain | Actual supplied finished changes in one coherent packet; amortized producer/compaction batches; a proposed finer update path owned by Orbital | Do not equate one semantic effect with one new Object, or omit data/delta/validity work. Captured bytes remain sealed under the current implementation. |

The aggregate-maintenance spike did not select one universal winner. Its
[full-cycle hot case](../aggregate-maintenance/dirty-buffer/FINDINGS.md) measured
22.0 ns/update for no-filter span buffering, 29.3 with Bloom marking and 70.9
for eager with private shallow summaries, including replay/reset. Dispersed
updates favored eager; the single-writer screen favored direct eager. The
earlier sorted-location probe reduced ancestor adjustments by 71% but cost
about 24x resident eager. These are count/sum experiments with quiescent reads,
not a production snapshot, min/max, NULL or concurrency protocol. Retain simple
append and eager controls; do not impose sorted runs or a Bloom filter.

The directory experiment therefore accepts independently supplied event mixes:
no directory event for ordinary stable-fact edits, append/invalidation without
assumed relocation, actual bounds changes, and compaction/port bursts. It does
not implement the summary producer. Publication/locator alternatives must be
qualified with Orbital; missing current support is a module question, not a
reason to collapse the design back into eager five-layer COW.

Orbital's source review identifies three concrete compositions to price before
choosing the ordinary producer path:

1. Stable raw addresses with disjoint append. A counted owner holds the real
   Allocation; each immutable selected Control admits its initialized prefix.
   Later appends write only outside ALL sealed bodies/padding. A new Owner
   publishes the extent, so old readers keep their earlier prefix. The directory
   pointers can remain equal; changed runtime geometry requires its selected
   Control rather than a stale directory-cached count. StreamPack append is not
   automatically such a layout: sidecar count/extent/length and boundary stores
   can overlap old footprints.
2. Fixed base plus bounded delta material. Retain base bytes and committed delta
   chunks directly, with immutable applied/committed offsets and validity law.
   One partition/collection material publication can describe actual changes
   without manufacturing five eager ancestor images. If the delta itself holds
   the authoritative row change, a separate full row image need not be published;
   otherwise include the actual changed row Owners. This is a proposed material
   algorithm using current Orbital ownership/publication, not a shipped codec.
   Reads pay lookup/correction/bypass; compaction publishes base and applied
   boundary together. Capture must not retain an unbounded chain of predecessors.
3. Version-qualified locator recipe. Current ObjectId+Part selection already
   gives stable logical identity across physical replacement. The dirent recipe
   and compact identity encoding are Engine proposals; resolution uses the real
   Scope and its cost. An eight-byte locator is not a truncated128-bit ObjectId:
   it must combine with an admitted index/namespace/Part recipe without collision.
   A cheap direct Port/handle without that selection is a separate Orbital seam.

The content offsets above delimit bytes represented by a selected material;
they do not replace Orbital's cut, reader history or commit protocol. A global
reservation tail is not a committed prefix, and holes/stalled writers cannot
become visible. Pristine can be derived from no relevant visible delta since the
selected base rather than rewriting a bit in every ancestor. Dirty-read and
compaction algorithms must preserve that meaning at every retained snapshot.

Overwrite of a captured body is unsupported today, even with unique refcount,
no reader already bound, same-value stores, a Hold or an Ikea damage journal.
An Orbital-managed preservation/update seam is a possible research question;
Engine may not emulate it with another undo or visibility system.

## Paper revisions

The slot and fence slack discussed here is physical-keyspace padding/gaps,
distinct from the readable byte padding sealed by Representation capture.

| Revision | Cost exposed | Disposition |
| --- | --- | --- |
| One immutable table/hash/boundlist per layer | A one-entry change can clone a population-sized owner. | Reject. |
| Bounded leaves beneath a flat outer roster | Rewriting the outer roster still grows with population. Physical child pointers also propagate relocation. | Reject. |
| Balanced storage tree with stable child Object names | Ordinary payload updates no longer change parents, but a many-Part leaf capture still retains/describes every Part. | Keep the routing rule; revise payload placement. |
| One smaller copied Part inside each leaf | Reduces bytes but not stock capture's all-Part work. A 2 KiB chunk is still a 2 KiB copy, not a 64-byte update. | Explicit comparison, not assumed cheap. |
| Separate payload-chunk Objects | Locator updates capture one Part; reads pay an additional Object selection unless already in a cursor/binding. | Current large-leaf candidate; count both costs. |
| Direct dispatch by the known J-byte prefix | Sparse J=2 regions need not traverse a global logical tree to reach one segment. | Keep. Region roots belong to each layer independently. |
| Small embedded versus separate large payloads | Singleton/small regions avoid an extra selection; larger regions avoid copying and retaining all payload Parts. | Two concrete representations to price. |
| Narrow exact endpoints and lossy quantised guides | Wide endpoints can waste SIMD lanes and parent writes; a guide can add ambiguous-run reads and more dependent selections. | Compare exact native widths, relative ends and both guide placements below. |
| One finished replacement slab per batch | Can amortize backing acquisition, but does not remove descriptor/capture/publication work and retains a coarser allocation. | Supported shared-owner control; proposed native range source belongs to Orbital. |
| Reusing Orbital publication workspace and resolving each Object once | Removes repeated allocation/search work without changing visibility semantics. | Proposed Orbital-owned change, not an existing Engine helper. |
| Counting structural captures for independent payload Objects | A split/compaction can turn a small byte copy into dozens of catalog changes/tombstones. | Reopen publication grain: compare a 32-entry, five-Part leaf Object before committing to independent chunks. |
| Treating every record write as five segment and five directory replacements | Assumes eager summary afterimages, moving raw-byte material and a particular publication grain. | Reject as a baseline. Keep only as an explicitly offered relocation stress case. |
| Exact occupied bounds as the only routing policy | Couples new extrema to directory repair even when stable enclosing ranges could remain valid. | Reopen; compare fences, refinement/read amplification and actual placement events. |

There is no favorable cost sum yet for every path. IF the producer offers five
segment replacements that also require five directory-chunk replacements,
that particular packet has ten changes. It is not an ordinary-write minimum.

## Layer and region dispatch

L0/L1 use ordinary bounded tables. L0 has one possible prefix; L1 has 256.
Store their dirents in small independently selected chunks, plus an ordered
occupied-prefix/bounds view for logical seek. A direct table hit does not search
that ordered view. A membership change can move at most the bounded top-layer
metadata, never a layer-sized dirent payload.

For L>=2 with L<=J, the query supplies the complete physical segment prefix.
Partition that prefix into `group=p>>4` and `suffix=p&15`. A group has at most
sixteen segment dirents. Its Object name is derived directly from index, layer
and group. The singleton representation compares its one suffix. The compact
multi-entry representation uses actual `ikea::hashtable::table<4,0,1>` to map
suffix to payload slot 0..15. Its bitmap geometry cannot spill to Swiss.
Only a membership/slot-map change clones the map owner; an ordinary locator
change retains it and replaces the bounded payload.

The group still needs its selected routing bounds. An exact prefix miss is not a
logical seek result: an ordered presence tree finds the next group, then its
next eligible suffix. No optimistic miss is returned as end-of-layer.

For L>J, use the first J logical bytes to select the matching per-layer region;
the same bytes are a physical prefix. J=0 has one region. A region root has a
singleton/small-leaf representation until it needs a balanced Boundlist tree.
Range traversal crosses regions through that layer's ordered region-presence
tree. A point whose natural region exists can skip that outer search.

All ordered entries follow physical prefix order. Exact occupied minima and
maxima are each nondecreasing. An enclosing-fence recipe must independently
admit containment and monotone endpoints; arbitrary conservative intervals do
not automatically have that property. Searches use
exact full-key comparisons: `lower_bound(maxima,q)` for a closed lower edge and
`upper_bound(maxima,q)` for an open one. Duplicate runs can cross every page
boundary. No unique logical fence or order among duplicate records is assumed.
Enclosing fences return candidate segments; exact segment processing filters
them. Open/closed flags apply to exact values, and conservative fence tests must
never discard a possible equality. Broad fences are charged as read amplification.

## Dirent bytes

Proposed resident metadata interpretation, preserving the supplied 64-byte
size. The representation schema owns the locator interpretation; this is not
yet a portable wire contract.

| Offset | Bytes | Field | Meaning |
| --- | ---: | --- | --- |
| 0..47 | 48 | `parts[6]` | Six memory pointers or resource locators for the segment's inline Parts. |
| 48..51 | 4 | `representation` | Stable representation identifier, resolved through the selected index schema. |
| 52..53 | 2 | `flags` | Proposed bits0..5 mapped-inline mask; bits6..7 extra recipe (none/StreamPack/paged/reserved); bits8..9 admitted locator-recipe class; bits10..15 zero/reserved. Schema defines each Part's actual interpretation. |
| 54..55 | 2 | `overflow_rank` | Assigned StreamPack rank, 0..255 for that recipe. Ignored for no-overflow/paged-extra recipes. |
| 56..63 | 8 | `overflow_block` | Scoped block name/resource coordinate; physical replacement of that block does not change the name. |

No prefix, logical bound, record summary, raw pointer owner, MVCC cut or
artificial revision counter is hidden in those sixteen bytes. Orbital owns the
cut and history. A changed representation identifier is a real directory edit;
a new logical cut alone is not a reason to rewrite an otherwise equal dirent.
Unused inline slots and unused metadata are zero; actual Part count and permitted
locator recipes come from the admitted representation, not guessed null-pointer
semantics. Reserved encodings reject rather than silently selecting a format.

## Routing and payload bytes

Native-width, relative and prefix-stripped endpoint recipes are part of the
initial paper comparison. K=1/2/4 does not intrinsically require eight-byte
lanes. The following wide native layout is a conservative control, not a chosen
128-by-28-byte architecture. Fanouts 16/32/64/128 must be costed with their
actual tree heights, selection waves, child-name bytes and update work.

The leading small-tree comparison is F32/C8 with ONE leaf Object and five
Parts: routing metadata plus four fixed512 B payload Parts. A locator edit copies
one 512 B Part, retains the other four sources, captures all five Parts and makes
one directory publication change. Reads use that selected leaf's Part slots,
avoiding the independent payload Object lookup. Five Parts stay below stock
capture's >8-source scratch threshold. The cost is four retained-source uses
and a larger descriptor per edit; this is not a one-Part capture claim.

Its wide metadata is 728 B (56 header +32*21 B rows); internal metadata is 952 B.
Payload capacity is 2,048 B. Chunk coordinates are derived fixed Part ordinals
in this recipe, with no stored coordinate vector or extra Object names. Common32 B
Control gives a 40 B Representation header plus five 16 B native slots =120 B
descriptor before resource/proof/allocation overhead. Compare exact narrow
metadata as well. Unused capacity is initialized/sealed and fully charged.

This revises the independent-chunk candidate for a concrete reason: a 128-row
split can require up to 17 output payload captures plus routing/counter changes;
a merge of fragmented leaves can also retire many old chunk Objects. With the
F32 co-located recipe, a split has two output leaf Owners, changed parents and
the namespace counter, with no per-payload-Object tombstones. It still captures
every admitted Part. Compare one versus several actual prepare changes and
physical retained uses; neither read depth nor source fanout is free.

| Structure | Logical bytes |
| --- | --- |
| Segment dirent | 64 |
| Logical routing endpoints | `2*K` per segment for fixed-width exact/enclosing recipes; sentinel/inclusion recipe data is additional, separate from its dirent |
| Ordered leaf row, wide K<=8 control | prefix u32 (4), two normalized endpoint u64s (16), local payload slot u8 (1): 21 |
| Internal routing row, K<=8 | physical lower prefix u32 (4), two occupied endpoint u64s (16), child name u64 (8): 28 |
| Full leaf, wide128 control | 2,872 B: 56-byte header + sixteen8 B chunk names +128*21 B rows; columns separately aligned |
| Full internal node, wide128 control | 3,640 B: 56-byte header +128*28 B rows; smaller counts can add4 B alignment padding |
| Payload chunk, independent C8 control | eight 64 B dirents =512 B, one admitted Part; leaf owns its payload-slot population |
| Payload chunk, C16 control | sixteen64 B dirents =1,024 B; compare saved capture/catalog metadata with doubled copy bytes |
| Compact hash map | sixteen four-bit-domain keys, one-byte slot values; assigned primary geometry 18 B, heap policy rounds its buffer to 64 B |
| Compact group payload | `64*n`, n<=16 |
| Compact group bounds | `2*K*n`; original-suffix population u16 plus slot-to-suffix u8[n] are additional |
| Ordinary L1 inverse/occupancy data | at most 512 B for u16 ranks and 32 B population, if that representation uses both |
| Shared-Prefix region entry | region prefix plus scoped root name; no copied segment payload |

Columns have no implicit C++ struct padding in these byte counts. A native
Dirent chunk starts aligned to 64 and uses a 64-byte stride. Other columns are
separately aligned as their admitted reader requires. Empty physical Parts have
no invented initialized capacity; spare readable bytes/padding are charged and
sealed if granted.

The wide node-header proposal is 56 bytes: assigned count u16 at 0; height u8
at 2; kind u8 at 3; flags u32 at 4; root-only dynamic-namespace high u64 at 8; region
prefix u32 at 16; chunk count u16 at 20; reserved u16 at 22; leaf payload-slot
mask 128 bits at 24; exact-key tail-root name u64 at 40; subtree segment count u64
at 48. Internal nodes zero unused leaf fields. The subtree count is u64 because
L4 can contain 2^32 segments. These fields are immutable selected data.

For the common one-Part payload, compare a 32-byte native Control containing
resource pointer8, immutable format-proof pointer8, local Object name8, assigned
count4 and geometry/state selector4. Then the stock Representation header is
40 B on an 8-aligned ABI, and one native/shared slot makes 56/64 B. This is a paper
layout to confirm with Clang, not a measurement. The format proof owns its real
interpretation dependencies; a hash group's proof must also retain its actual
Ikea owner/header and its matched payload-slot meanings. Those bytes/retains
are additional, not hidden inside a claim of one cheap Control.

For K<8, use an order-preserving native-width source where it fits; the wide
control can zero-extend into its numeric lane. K=0/J=0 is the all-equal
logical-key case. For K>8, the first
eight bytes are only an order-preserving abbreviation. Equal abbreviations
read the full endpoint from immutable tail blocks. Tail references and lengths
are additional metadata; compare/copy their actual bytes, including the
all-shared-prefix worst case. A numeric predecessor trick cannot stand in for
an exact byte-key comparison.

An ordered leaf retains up to sixteen chunk names and a 128-bit payload-slot
mask. Its row selects a local slot0..127: chunk=`slot/8`, row=`slot%8`. The
chunk-name vector provides the scoped Object name; this is independent of
ordered rank and of record physical-key slots. Child/block names select Orbital Objects
under the same Scope; they are not mutable memory pointers. Allocated child
names are not reused while older selections can name them. Deterministic
region/group names continue to denote that same logical identity across
birth/death; Orbital history selects its state at the Scope's cut. Page allocation
uses a versioned index/layer namespace allocator, retained even while all its
regions are empty. Its one-Part eight-byte counter changes once per structural
batch, and that additional Object/capture/publication is charged. Compact child
names are low64 bits under an admitted, unique namespace high64; deterministic
root/group names use disjoint assigned namespaces. This preserves full128-bit
Object identity without a per-entry128-bit name or accidental collisions.
Namespace assignment belongs to index construction and is counted there.
Exhaustion returns capacity; no wraparound or pointer-derived identity is
permitted. Empty-region recreation cannot silently restart an old counter.

For compact hash groups, use a concrete two-Part control: payload and bounds.
The immutable Control/proof additionally retains a fixed-address mapping image
owning the actual Ikea table/header/buffer, original-suffix mask and inverse.
Locator-only replacement retains that image and bounds source; capture still
admits BOTH Parts. Membership copies/edits the mapping image once. Compare this
with a three-Part mapped-image treatment if the actual Storage seam requires it;
it must include all three capture slots. Neither treatment counts the 18-byte
hash geometry as its complete ownership footprint.

## Tiny Boundlist candidates

These compare real source domains, not a claim that arbitrary logical keys fit
the segment's 16-bit physical slot coordinate. Generic Boundlist sources are
count-parametric; a small routing node does not need the curated 256-rank
reservation sidecar. StreamPack overflow is a different use of that sidecar.

1. Exact native/relative endpoints: use u8/u16/u32/u64 at their actual width,
   remove a shared logical prefix/origin, and divide by a power-of-two quantum
   only when ALL endpoints align exactly. Queries still need exact rounding and
   out-of-domain handling. A prefix-stripped SeriesPack endpoint source is a
   parallel exact packed control with reusable native filters/boundaries.
2. Lossy endpoint guide: stable `G[i]=floor((E[i]-origin)/2^s)`, with u8/u16
   codes, or an equivalent order-preserving prefix code for byte keys. Unequal
   codes determine order; an equal-code interval always refines against exact
   endpoints. Keep the first greater-code rank as the fallback. All-equal and
   wide ambiguous runs fall back to ordinary exact search rather than forcing
   an extra unsuccessful guide pass.
3. Quantised sizes: derive `D[i]=G[i]-G[i-1]` from those endpoints, with group
   checkpoints. Do NOT round each original width independently. At 32 ranks,
   four-bit differences and G16 byte checkpoints need 16+2 bytes per endpoint
   plane if each difference<=15 and the total guide span<=255. At 64 ranks,
   it is 32+4. Zero guide width does not mean an empty child. Packing, prefix
   dependence, both extrema planes, origin/shift and exact correction all count.

A 32-child min/max byte guide is 64 bytes, but child names alone add 256 bytes,
before exact bounds and headers. A smaller guide is not a one-cacheline whole
node. Likewise, a narrower node can lose by adding another dependent tree level.

Compare two guide placements. Co-located exact endpoints make correction cheap
but still rewrite exact parent bounds. Guide-only parents can omit a parent
write when a NON-extreme child bound changes within the same quantisation bin;
correction then selects exact child bounds under the same Scope. Exact aggregate
min/max changes at a subtree edge still propagate if the parent stores them.
Membership, changed guide codes and explicit recoding also still repair parents.
Stable origin/scale is essential; deriving a new origin from every changed
minimum would erase the proposed write saving.

Ikea owns any native kernel change. Its current AVX-512 direct-byte convergence
mask is capped at 32 ranks, and the opt-in scan source excludes u8; a full
64-byte-lane guide is therefore a proposed Ikea improvement, not a current API.
A specialised four-bit width prefix may avoid generic widening, but must earn
that change in the complete-node caller. Exact lower/upper_bound APIs never
return an approximate rank; the owner can retain a guide candidate window and
finish it against coherent exact data outside any wait.

## Metadata and ownership ledger

Use an admitted immutable format proof shared by compatible nodes. Geometry
for these fixed recipes is derived from that proof and the selected node
Control; do not allocate a Geometry vector for every update. The planned Control
contains only its resource/proof reference, format/kind, layer, counts, fixed
geometry selectors, live mask, root height/namespace when applicable, and
owned references genuinely required for interpretation. Its exact ABI and all
padding must be enumerated by the assembly fixture before timing.

For stock Orbital, the descriptor byte formula is:

```
header = sizeof(orbital::Representation<Control>)
independent native: header + 16*Parts
independent shared: header + 24*Parts
independent mixed:  header + 32*Parts
grouped native:    header + 8*Parts + 8
grouped shared:    header + 8*Parts + 16
```

Grouped capture requires the same physical owner across all Parts in that
Representation. It cannot group distinct old and new owners or retain one
transferred Allocation across separate captures for free. Certified retained
sources avoid predecessor chains; they still cause admitted source/owner work.

Current catalog entries are 40 B; old-version nodes 16 B plus separately used
physical-directory slots; Scope 32 B and Prepared 48 B in the supported ABI.
Bucket/slab capacity, mutation workspace, proof/header objects, native Pool
metadata, shared-owner controls, free slots and old/new coexistence are additional
bytes. They belong in the footprint, not a footnote saying only 64 B/segment.

For a batch, account separately for current directory bodies, current format
proofs/caches, catalog capacity/history, private changed bytes, captured
descriptor bytes, mapping/retention granularity, reusable workspace, Arena
charge and RSS. A slab with one still-retained slice retains its backing.
Derive bytes from actual live capacities and ownership; do not sum incomparable
RSS and logical counters.

## Balanced ordered storage

For F32, use capacity 32/minimum 8 and compare merge limit 26, split 33->16/17.
The analogous potential is high above 26 at all nodes plus low below 13 at
non-root nodes. Underflow 7 merges to 15..26 or redistributes 27..39 to 13..20,
releasing at least 6 credits; split releases 7. Newly normalized batch outputs
target 13..26. This has fewer credits than F128 but much smaller repair and
publication grain. A 400 ns structural goal gives about 67 ns/credit before
cascades. It must be earned by complete capture/publication measurements.

The wide control has leaf/internal capacities 128 and minimum non-root
occupancy 32. Compare merge limits 96 and 106; split 129 near the middle. The
106 limit leaves 23 insertion slots after a maximum-size merge, while a smallest
redistributed pair 107 becomes 53/54 and needs 22 deletions to underflow. The 96
control uses less full merges but can redistribute to 48/49, with only 17 deletion
slots. Neither is a blanket amortization proof under changing sibling pairs.
On underflow, merge an adjacent pair if it fits the chosen limit; otherwise
redistribute near the middle. Root contraction removes a single-child level. Empty regions/pages
retire. These are candidate storage thresholds, not segment policy.

For the 106 candidate, a useful scalar potential is
`Phi=sum_all_nodes(max(0,n-106))+sum_nonroot_nodes(max(0,53-n))`.
The root is exempt from only the low-occupancy term.
One ordinary membership insertion/deletion changes it by at most one at that
depth. A split 129->64/65 releases 23 credits. An underflow 31 merged with a legal
sibling releases at least 22: the merged count is 63..106. Redistribution of
31 plus 76..128 gives 53..80 per output and releases at least 22. Every repaired
output has zero potential. This argument is global over nodes, so changing the
sibling pair or moving the hotspot does not evade it. Root height changes are
charged to their triggering split/merge. It bounds structural event count,
not measured time, copied bytes or retained-reader space.

Retain initial potential: a prefilled 32-entry non-root node already has 21
credits. Parent membership changes caused by split/merge are also events,
not free. For original leaf membership events M and total scalar repairs E,
a conservative aggregate bound is
`22E <= Phi_initial + M + E - Phi_final`, hence
`21E <= Phi_initial + M - Phi_final`, with root changes charged as above.
For F32 the analogous conservative denominator is 5 rather than 6. Do not turn
the depth-local target division into a whole-tree per-original-write promise;
actual internal and batch work still needs its measured sum.

Batch normalization uses the same target interval 53..106 for newly materialized
non-root outputs. Extend a deficient final run until such outputs are possible,
or contract the whole tree into its root exception. Existing legal nodes outside
the run retain their boundaries. A bulk deletion can consume many credits in
one request; it must not be presented as a cheap scalar event. Price the full
run and its neighbours once, plus internal membership events at the next depth.
The 96 control similarly has target 48..96 and only 17 guaranteed scalar credits.
This gives a reason to prefer 106 for the first candidate: the 1.5 us structural
goal divided by 22 is about 68 ns/event before cascades; the 75 ns amortized goal
is therefore plausible only if actual copies/captures also meet that goal.
These are target arithmetic and event bounds, not an observed timing fit.

Check alternating insert/delete and moving-hotspot histories against this
potential and actual bytes. Bulk batches merge sorted incoming changes with
each affected leaf once, rather than replaying scalar splits that repeatedly
copy the same leaf.

The tree is bounded by its actual occupancy, not five index layers. At most
2^32 segment prefixes occur in L4; global populations therefore use u64 even
though individual Boundlist nodes have only 128 entries. A maximum prefix is
valid; end-of-layer is a separate state, never `last_prefix+1` in u32.

Only actual over/underflow, changed membership/topology, or explicitly priced
compaction seeds structural normalization. A descriptor/bounds-only edit to
healthy adjacent leaves of 40 and 80 entries preserves their boundaries; it must
not redistribute them to 60/60 just because both are dirty. Bounds propagation
alone changes metadata on the existing path.

Payload chunks stay with one current leaf. A split can retain a whole chunk
only if all its rows stay in one output; otherwise compact within the affected
pair, charging copies, changed chunk Owners, tombstones and old-reader footprint.
A control can preserve shared chunks across leaves only with an explicit bounded
fragmentation/holder model, not an invisible global reverse map. Unbounded fragmentation is not an
acceptable way to make balancing look cheap.

## Extra locators

The default candidate groups at most sixteen active extra lists in one
StreamPack block keyed by a stable physical-prefix group. Every list contains
eight-byte locators. A dirent records that block and assigned rank. Ranks are
not global occupied-directory ordinals.

Replace in reservation when the list fits. Growth appends a new reservation,
retargets that ONE dirent and retires the old rank. Empty lists do not make an
existing assigned rank disappear. Packing transfers only the local live ranks
and repairs only their holders in the same Orbital publication. Start with a
packing trigger at 32 assigned ranks or >50% dead reservation bytes, then price
alternating shrink/grow and the actual amortized work. Repacking every update
is rejected.

Direct and Width are genuine alternatives: Direct makes projection simpler;
Width can reduce the capacity sidecar. Include full reserved directory/control,
assigned length groups, payload reservation and readable group tails. Select
from actual representation limits and measured whole projection/edit costs,
not from the size of the useful eight-byte value alone.

An extra list larger than StreamPack's value limit uses a paged locator-vector
representation, not truncation. Its immutable nodes contain count/height and
scoped child names, with fixed-size u64 locator blocks. An ordinal selects one
block; one locator edit replaces that block, and only extent/topology changes
repair ancestors. This representation and its O(height) projections receive
separate E=64/256/large cases.

## Prior art carried into the experiment

The [Calico trie record](../../../../calico/design/TRIE.md) gives specific
strengths and limitations to test, rather than a tree to copy wholesale:

| Calico observation | Consequence for this experiment |
| --- | --- |
| Prefix identity kept relocation out of descendant references; QHash handled direct access and the rope handled ordered access. | Preserve prefix identity and price direct and ordered paths separately. Stable directory child names need no physical-address ancestor repair. |
| Overlapping 16-bit windows allowed a 16-bit navigation hop without removing the intervening segment. | Explicit one/two-layer descent is a lookup in the requested layer; storage-tree height and index layers remain separate. |
| Empty segments stayed resident; ordinary creation eagerly populated every byte depth. | SixDB's supplied nonempty-only segments require birth/death churn and presence repair. Do not import eager ancestor construction. |
| The rope's successor interface lacked the logical prefix needed for safe same-depth demanded-span traversal. | Cursor output retains the layer and physical prefix. No cross-layer order or rope shortcut is assumed. |
| Independent container relocation and one-hop tombstones limited local repair, but production packing/recovery remained incomplete. | Compare bounded Part movement, retained-reader interpretation and local overflow packing; charge holders and retirement. Do not inherit its storage protocol. |
| QHash clustered access was much stronger than distinct/random access. | Both clustering and dispersed/cold directory populations are first-class comparisons; a warm clustered result cannot stand in for either. |
| Dense-key examples had very few segments relative to records. | Vary segment population separately from record population and padding/placement. A million records need not mean a million directory entries. |

Related SixDB research used in this plan:

| Research | Relevant outcome |
| --- | --- |
| [Boundlist](../boundlist/README.md), [StreamPack](../streampack/README.md), [Hashtable](../hashtable/README.md) and current Ikea docs | Use shipped ordered/hash/overflow kernels; historical owning multimap is not the map core. |
| [MVCC](../orbital-mvcc/OUTCOMES.md), [Stage2 composition](../orbital-stage2-composition/README.md), [selected access](../orbital-selected-access/README.md), [conversion](../orbital-representation-conversion/README.md) | Coherent selection, actual Parts/geometry and conversion constraints; disposable caller policy is not Engine. |
| [Allocation live-tail](../allocation-live-tail/README.md), [temperature reuse](../allocation-reuse-temperature/README.md), [palettes](../allocation-palettes/README.md) | Physical range/padding lifetimes, recycled versus aged inventory, capacity and retention costs. |
| [Loom execution](../loom-execution/README.md), [locality](../loom-locality-execution/README.md), [hardware policy](../loom-hardware-policy/README.md) | Ready direct work versus actual independent dependencies and genuine waits. |
| [Memory characterization](../memory-characterisation/README.md), [assembly calls](../assembly-calls/README.md), [call boundaries](../call-boundaries/README.md) | Cold dependencies, real owning ABI/result transfers and whole-operation attribution. |
| [Aggregate maintenance](../aggregate-maintenance/CONCLUSIONS.md), [trie remapping](../trie-remapping/FINDINGS.md), [layout analyser](../layout-analyser/README.md) | Eager/buffered alternatives, keyspace slack versus payload movement, and measured layout tradeoffs. |

- [Calico trie](../../../../calico/design/TRIE.md): prefix naming avoids subtree
  fixups on relocation; QHash and ordered traversal have distinct jobs. Its
  resident empty segments, rope limitations, rank-packed movement and older
  storage contracts are not inherited by SixDB.
- [LMDB source](https://github.com/LMDB/lmdb/blob/mdb.master/libraries/liblmdb/mdb.c):
  inspect page touch, split/rebalance and cursor repair alongside competent
  transactions/cursors, not an invented generic Store adapter. Its copying and
  snapshot work belong in the comparator's measured operation.
- [CSB+ trees](https://www.cs.columbia.edu/~junr/research.html): cache-conscious
  node size and contiguous child access motivate the fanout/read-footprint
  comparison. Their pointer arrangement is not automatically suitable for
  versioned physical replacement.
- [ART](https://db.in.tum.de/~leis/papers/ART.pdf): adaptive small representations
  and path compression motivate singleton/small/large region forms. ART is not
  evidence that the supplied overlapping segment geometry should change.
- [HOT](https://dbis.uibk.ac.at/sites/default/files/2018-06/hot-height-optimized.pdf):
  adaptive discriminative partial keys in 8/16/32-bit lanes and compact SIMD
  nodes strengthen the narrow-routing comparison. Charge extraction/recode
  and exact-key verification. Its BMI2 PEXT path is not a universal ARM kernel
  or evidence for SixDB snapshot/publication cost.
- [Wormhole](https://wuxb45.github.io/papers/wormhole.pdf): hashed routing and an
  ordered leaf organization are relevant to the shared-prefix/ordered split;
  anchor and boundary repair must still be charged. Its concurrency/versioning
  mechanism is not copied into Engine.
- [Bw-tree](https://www.microsoft.com/en-us/research/publication/the-bw-tree-a-b-tree-for-new-hardware/):
  stable indirection illustrates why physical relocation need not change every
  parent. Delta chains and its reclamation machinery are not an Engine MVCC
  implementation plan.
- [Cache-oblivious B-trees](https://erikdemaine.org/papers/FOCS2000b/): density
  slack and local redistribution inform the adversarial balancing/fragmentation
  tests. The directory does not thereby acquire a new record-key allocator.
- [SixDB aggregate maintenance](../aggregate-maintenance/CONCLUSIONS.md) and
  [trie remapping](../trie-remapping/FINDINGS.md): reduced logical work can lose
  to temporary construction; gaps must be priced with payload movement and
  references. Directory input is segment deltas, not another summary engine.

## Unsettled costs

The joint pseudocode must settle selection count versus capture granularity,
the exact small-group hash/proof ownership, the native/shared batch allocator
choice, coherent physical conversion, stock versus proposed Orbital preparation,
cold catalog dependency staging, and chunk fragmentation. These are measured
implementation questions. They are not reasons to change the supplied index
concept or to claim that a paper design already beats LMDB.
