# Joint directory pseudocode

Algorithm pseudocode, not C++ or an implemented API. `Scope.bind`,
`source(selection,p)`, consuming/shared capture, `prepare`, `install`, `abort`
and bounded `drain` name current Orbital operations. Hashtable/Boundlist/
StreamPack operations name their actual families. Helpers defined below are
Engine algorithms. The section headed “Proposed Orbital work” is explicitly
module-owned work to investigate; it must not be silently emulated in Engine.

## Inputs and retained state

```
SegmentName = (index, layer, physical_prefix)
Dirent      = six u64 Part locators + the 16 metadata bytes in DESIGN.md
Bounds      = selected monotone logical routing endpoints and their admitted recipe
              (exact occupied bounds OR enclosing fences; not summary values)
Entry       = (physical_prefix, Bounds, Dirent)

SegmentDelta:
    name
    expected_before_binding or expected_absence
    finished_after_primary Owner when actually replaced, or deletion/unchanged
       association qualified by the producer's real update/publication protocol
    after_bounds, representation, inline Part BYTE addresses/resources
    touched extra ordinals/new list, OR qualified complete extra-association
       unchanged evidence tied to the before binding and finished after material
    independently retained format/geometry proof
    # Emitted ONLY for a possible DIRECTORY fact change, not every record write.
    # This describes supplied segment state, not record rows or an undo action.

Read:
    read token: uninterrupted current-cut Borrowed, OR SAME stationary Scope
    never promote Borrowed or move an enrolled Scope across a wait
    index schema selection and reusable admitted definitions/read laws
    optional already-qualified authoritative material state, including deltas
    layer, query endpoints and endpoint policies
    mode-specific cursor position defined below, separate from payload slots
    optional selected payload/extra block for repeated consumption
    admitted layout: fanout F, payload chunk slots C, co-located or independent

Edit:
    original Scope/basis and offered SegmentDeltas
    scratch keyed by FULL 128-bit ObjectId, private to this attempt
       small attempt: inline full-ID array
       larger attempt: Ikea u64 high-half -> per-namespace Ikea u64 low-half map
       # Ikea's current key width is<=64; never truncate identity or invent W128.
    owned mutable drafts and new byte ranges; no published state is mutated
    offered finished segment Owners and eventual directory Owners
    current scoped root/namespace-counter drafts when topology changes
    candidate allocation/effect storage and publication workspace
```

An edit's draft table only deduplicates work. It is not an authoritative segment
roster, version table, reader registry or transaction log. Its scratch capacity,
initialization and touched-key cleanup are charged. A few deltas may use a fixed
array instead if that complete caller is faster; no `std::map` is on this path.
Both identity halves participate in lookup/equality. Nested map allocation,
lookups and cleanup are charged; the small inline case compares full IDs too.

## Producer boundary, before any directory work

```
ON_PRODUCER_CHANGE(change):
    producer already owns the base/data, pending deltas and validity semantics
    if its actual protocol proves membership/routing/representation/ALL locators
       and directory-owned interpretation unchanged:
        emit NO SegmentDelta; perform NO directory route/edit/capture/publication
        complete the actual data/summary publication through Orbital, if required
        # Initial directory lookup to locate the data is still charged normally.
    else:
        emit the actual changed SegmentDelta(s), not one for every index layer
        preserve required coherence with any actual data/summary/schema changes

READ_DIRTY_SUMMARY(selected_data, selected_validity, selected_pending):
    # Producer's semantic obligation, not a new directory summary implementation.
    consume exact summary when pristine at this snapshot
    otherwise use an admitted exact correction, descend/bypass, or coherent flush
    never use stale extrema as an exact answer or an unsafe pruning predicate
    min/max deletion, NULL and noninvertible summaries require their actual laws
```

The append/pristine producer and the eager producer are distinct test inputs.
Current captures do not authorize overwriting sealed bytes. Stable append,
versioned resource binding, private material and finer physical update seams
must be qualified with Orbital, not implemented as Engine rollback/history.

The bounded native DRIVER now carries two append controls and a small-object
eager counterpart. These are finite producer codecs, not selected Engine formats.
The plain control has an eight-byte zero header followed by24-byte absolute
afterimages. It performs NO footer/index/SUM update on an ordinary publication.
Dirty points scan backward to the latest matching row; dirty SUM/extrema and a
full fold load current bases and apply the visible event stream ONCE into64
private values. Pristine advice bypasses this work for unchanged fields/rows.

```
APPEND_PLAIN_AFTERIMAGES(basis, commands):
    use the same selected-prefix/custody/refusal rules as indexed append below
    admit each distinct touched row once; retain its actual advice in private work
    process commands in order; remove final no-ops; walk only changed-row bits
    append one24-byte absolute afterimage per final changed row
    no latest-offset table, eagerly maintained SUM or newly allocated header
    capture exact expanded prefix with immutable Control; one atomic publication

DIRTY_PLAIN_READ(scope, row):
    resolve directory and base at the SAME selected Scope
    if matching base advice is clean: return base
    select its own immutable collection extent; scan backward within that prefix
    return first matching ABSOLUTE afterimage, or selected base when absent

PLAIN_FOLD_OR_DIRTY_AGGREGATE(scope):
    load64 selected base field3 values into private scratch
    apply every selected afterimage in forward order ONCE, overwriting its row
    compute actual requested SUM128/extrema from the resulting values
    a fold builds actual fresh bases/summaries/advice and an EMPTY new collection
    publish all real moved-Part directory changes and collection in ONE packet
```

The indexed control can avoid a fresh header Allocation while maintaining an
exact correction index. One shared prefix ends with an immutable160-byte
lookup footer. It holds exact field3 SUM128 at0..15, event count u64 at16..23,
64 u16 latest-event offsets at24..151 (eight-byte units,0 means absent), and
eight zero reserved bytes. Each24-byte afterimage holds prefix:u32, zero:u32,
sequence:u64 and field3 aftervalue:u64. Other seven u64 fields stay in the base.
Footer and records are data-format choices in the DRIVER, not directory fields.

```
APPEND_INDEXED_AFTERIMAGES(basis, commands):
    select current prefix and its exact byte extent through the SAME Core/Scope
    old_footer = selected_prefix.end -160
    process commands IN ORDER in private scratch, reading each touched row once
       # RepeatedP9,P6 sees private9 before6; finalP6 implies SUM17 from7/11.
    coalesce final afterimages by touched row; discard actual no-ops
    if no final changes: return success without directory or collection mutation
    if current committed prefix +24*changed_rows +160 exceeds backing capacity:
        privately fold visible state INCLUDING these commands into actual bases
           and ALL summaries, applying the old event stream only once
        construct fresh advice generations; never clear an old generation
        start a new fixed-address backing with an empty160-byte indexed footer
        include actual resulting material/directory changes in THIS packet
        do not append these already-incorporated afterimages again
        capture empty rebased collection; prepare/install the complete packet
        return
    else if cached backing differs or sealed highwater != selected extent:
        acquire fresh backing; copy ONLY the selected committed byte prefix
        # Rotation alone changes no logical baseline or sequence meaning.
    admit the disjoint suffix, including actual native store widths
    append final afterimages; copy old footer to a NEW suffix position
    patch latest-offset entries, event count and exact absolute SUM128
    conservatively seal attempted physical highwater; never rewind failed space
    capture ONE independent_shared Part spanning the finished initialized prefix
       # Immutable Control owns its exact extent; no separate native header.
    prepare/install collection plus actual compaction/port changes in ONE packet
    charge private scratch, copies, capture, packet and amortized full settlement

INDEXED_AFTERIMAGE_POINT(scope, row, field3):
    directory lookup yields base bytes and generation-owned monotone dirty advice
    if proved clean: return base field3
    select authoritative collection under scope; use its OWN qualified extent
    footer = prefix.end -160
    if footer.latest[row] ==0: return base field3
    return absolute aftervalue at prefix +8*footer.latest[row] +16
    # Absolute replacement does not reapply a folded P contribution.

INDEXED_AFTERIMAGE_SUM(scope):
    use pristine selected summary when its generation is proved clean
    otherwise return the selected footer's exact absolute SUM128
    # Dirty MIN/MAX descend/recompute; no stale extrema pruning.
```

Footer copies are unreachable physical garbage in the newest prefix, bounded by
backing capacity. They are not a linked history or an Engine visibility service:
Orbital alone selects the old/new material and its extent. One indexed lookup
does not scan earlier footers. The first counterpart used competent private
coalescing and small exact afterimage/SUM objects, with ONE publication perB1/B64
request. Its measured dispersed capture fanout motivates the packed eager control
below. Compare hot and dispersed updates, query pressure and the complete
partial/full compaction cycle; neither format is assumed to win.

The ordinary attempt reuses a bounded PRIVATE publication vector reserved once
by the producer. Success clears its Owners; capacity refusal can retain the
finished candidate for retry against its original basis. This is scratch,
never lookup authority or logical visibility state. Small row arrays are read only
for touched rows, and changed-row bit walks replace full64-row scans onB1.
Cache a selected row's actual advice while admitting that row, avoiding a second
directory lookup just to mark it. The finite producer's four current summary
Backings share one real advice object; one selected L0 mark invalidates them all.
A general representation must prove its own advice coverage rather than assume
this sharing. Eager full compaction keeps existing absolute afterimages that
already equal the new bases; it does not manufacture64 unchanged replacements.

```
EAGER_PACKED_AFTERIMAGES(basis, commands):
    admit/coalesce final changed rows with the SAME before/after semantics
    select one576-byte correction image through basis
       header: absolute SUM128,valid-row bits,zero padding;64 absolute values
    allocate actual private576-byte native backing
    clone the selected image ONCE using exact64-byte blocks
    patch final changed row values, valid bits and actual SUM128
    mark selected base/summary advice before visibility
    capture ONE native Part and publish ONE actual Object in oneB1/B64 packet
    full fold retains unchanged correction image; it already equals new bases
    no per-row capture and no64-publication atomic-batch substitution
```

The following is the general append/delta composition retained for
the producer comparison. It specifies the directory boundary and snapshot
obligations; the finite native codec above is not a general summary law.

```
APPEND_MATERIAL_CANDIDATE(basis, actual_commands):
    select existing base/applied boundary/committed delta extent under basis
    acquire a PRIVATE suffix disjoint from every sealed body AND readable padding
       in counted fixed-address backing, or acquire fresh backing on rollover
    execute actual Ikea writes only after their complete coverage is admitted
       # Existing packed append may instead require private/versioned sidecars.
    finish actual data/corrections and immutable per-chunk interpretation
    construct next material Control with directly retained base/chunk sources,
       base-applied offsets and a complete hole-free committed extent
    derive pristine/bypass from THAT selected base and visible corrections
       or include actual versioned validity metadata with matching coverage
    capture finished material Owner(s); no chain of predecessor Representations
    offer only ACTUAL directory fact changes (usually none until rollover/ports)
    publish material plus other actual required changes in ONE Orbital packet
    # No separate image per index layer is manufactured by this algorithm.
    # Commands are not made visible by merely reserving or writing append bytes.

READ_MATERIAL_CANDIDATE(read, selected_material, query):
    use its selected base, applied boundary and committed chunks as ONE version
    consider visible pending additions BEFORE a stale summary can prune them
    if summary exact under its admitted law: consume it
    else correct from visible deltas, descend/bypass, or request coherent flush
    include actual point-delta lookup/merge and extra selections in Engine cost
    do not treat count/sum subtraction as general min/max/NULL repair

COMPACT_MATERIAL_CANDIDATE(basis, unit):
    select coherent base + visible committed deltas for a DECLARED frontier domain
    choose partition-local runs, or actual per-unit applied frontiers in collection
       material; a global offset is valid only when ALL its covered units are folded
    compute actual new base and summaries with the producer's laws
    construct new material excluding EXACTLY the incorporated domain-qualified deltas
    retain corrections for every unincorporated unit; P/Q/P compacting only P
       must preserve Q, not advance a global frontier past its record
    offer real directory changes for moved Parts, changed range/representation,
       segment birth/death; retain stable facts without directory work
    publish new base/material/applied boundary/required directory edits together
    old Scopes retain old base plus old delta range without gaps/double counting
    release old uses through Orbital; no private Engine reader/reuse ledger
    charge construction, captures, corrected holders and retirement to the history

CONSERVATIVE_ADVICE_CANDIDATE(selected_base_generation):
    base Control retains generation-local advice OUTSIDE all captured footprints
    initialize dirty for any relevant pending deltas already visible with this base
    before publishing a new mutation, invalidate every prefix that could otherwise
       trust an affected old summary; finish invalidation before visibility
    on read, use clean only with matching base/completeness generation proof
       dirty forces correction/descent against the SAME selected material
    advice may become conservatively dirty for an old Scope; that changes cost,
       never the old Scope's answer or selected committed extent
    compact into NEW advice for the new base; never clear old-generation advice
       while old material can select it; retain through actual Control dependencies
    retire old advice through Orbital-owned material lifetime, no reader ledger
    # Current embedded single-owner execution permits an uninterrupted mark/install.
    # Concurrent/stalled marking and reset require a separately proved protocol.
```

The fixed-base/delta and disjoint-append candidates can use the current shared
capture contract. Their exact codec, lookup/indexing and publication grain remain
material proposals. Ready preparation/ranged-native improvements below are
separate Orbital proposals, not prerequisites for expressing the semantics.
Compare conservative advice with fully versioned validity. A dirty advisory
table chooses a safe read path; it is not a second visibility protocol or one
versioned Object per ancestor flag. Its marking, retained capacity and false
positives remain measured work.

## Query binding, candidate traversal and continuation

Parent routing extrema are derived from their children's ROUTING endpoints,
not from children's occupied record extrema when enclosing fences are used.
A coarse/quantised parent must refine against the same-read child endpoints.
Every permitted enclosure contains its actual keys and has monotone endpoints.
Choosing a candidate does not establish an exact point hit. Exact misses advance
WITHIN the current region before considering the next region.

```
OPEN_READY_BORROW(index):
    token = ObjectStore.borrow() at current completed cut
    bind schema/material as required through this token
    finish the ENTIRE call and all borrowed uses before publication, alternative
       replacement, suspension or final Store reset; no reentry that invalidates it
    return synchronous Read; Scope enrollment is not charged to this control

OPEN_SCOPED_READ(index, requested_snapshot, stationary_frame):
    reuse an actual live Scope for this Core, or construct it IN PLACE at NOW cut
    # Numeric old cuts cannot reopen history; enrolled Scope is never moved.
    bind/admit schema once; keep borrowed selections under this SAME live token
    # No physical retain/capture per row or cursor step; escape owns separately.
    return reference to stationary frame's Read (not a moved Scope)

Cursor:
    token/Core/layer; logical query edges when present; end is an explicit state
    probe kind NONE, LOGICAL, or PHYSICAL_PREFIX; high-edge law matches that kind
    mode TOP: selected table, current physical prefix / occupied rank
    mode GROUP: group name/key, selected group, ORIGINAL suffix, packed slot
    mode TREE: region identity, ancestor frames(name,selection,child rank),
               selected leaf(name,selection), local ROUTING rank, payload slot
    outer group/region presence position OR lazy exclusive-successor key
    # Every successful seek, including singleton/exact fast paths, sets this state.
    # An outer presence lookup may be deferred; its identity cannot be forgotten.

GROUP_SLOT(group,suffix):
    require original_suffix_mask contains suffix
    mask=group.original_suffix_mask
    if admitted sorted mask/rank recipe:
        return popcount(mask & ((1u<<suffix)-1))
    else return admitted concrete Ikea reader.find(suffix)
    # Ordered suffix is NOT a packed slot; suffix14->slot2,15->slot0 is valid.

SET_POSITION(read, container, physical_position):
    keep selected container under the SAME token; set mode/layer/current physical prefix
    record original suffix or leaf routing rank SEPARATELY from payload slot
    record all existing ancestor frames and the leaf itself (also singleton root)
    save lazy exclusive group/region successor identity if outer cursor unbound

ENTRY_AT_POSITION(read):
    obtain physical prefix, routing endpoints and a borrowed descriptor reference
    if co-located leaf:
        payload = leaf.selection.part(1+slot/C); row = slot%C
    else if independent leaf:
        payload = token.bind(leaf.chunk_names[slot/C]); row = slot%C
    else use selected TOP/GROUP representation's admitted slot
    return EntryRef, not a mandatory materialized64 B copy
    # Enumeration may consume all 64 B; a Part query reads its needed fields.
    # Reuse the selected array/table and lazy correction image across this cohort;
    # requalify after a token, basis or admitted physical-treatment change.

FIND_EXACT_PREFIX(read,L,p):
    reset traversal frames; set layer L, probe kind NONE and no high-edge filter
    if L<2:
        bind ordinary table chunk; test original population
        if absent: return absent
        SET_POSITION(read,table,p); return ENTRY_AT_POSITION(read)
    if L<=J:
        group = token.bind(hash_group_name(index,L,p>>4)); suffix=p&15
        if absent or original suffix absent: return absent
        slot = GROUP_SLOT(group,suffix)
        SET_POSITION(read,group,suffix); return ENTRY_AT_POSITION(read)
    region = high J bytes of p  # zero for J=0
    bind deterministic region root
    descend physical-prefix child intervals, saving every ancestor
    find exact leaf prefix; if unequal: return absent
    SET_POSITION(read,leaf,routing_rank); return ENTRY_AT_POSITION(read)

TREE_SEEK_CANDIDATE(read,root,q,closed):
    while internal:
        choose first child whose routing maximum >=q (closed) or >q (open),
           using real exact/refined Boundlist; save frame and bind that child
        if none in this node: ADVANCE_TREE_SIBLING and keep searching SAME region
    r = corresponding boundary in leaf routing maxima
    if r==leaf.count:
        ADVANCE_TREE_SIBLING; continue same-region search, or report region exhausted
    SET_POSITION(read,leaf,r); return ENTRY_AT_POSITION(read)

SEEK_CANDIDATE(read,L,q,closed,high=+infinity,high_closed=true):
    reset traversal; set layer and LOGICAL low/high edges and policies
    if L<2:
        use bounded table ordered occupancy/bounds; SET_POSITION on success
    else if L<=J:
        p=first L bytes of q
        try natural group and suffix p&15; on success SET_POSITION
        otherwise seek subsequent occupied ORIGINAL suffix, then later groups
        test routing bounds; never treat a hash miss as end-of-layer
    else:
        region=first J bytes of q
        try natural region with TREE_SEEK_CANDIDATE
        only after that region's candidate stream is exhausted seek next region
    save outer cursor or lazy exclusive successor identity on EVERY success
    return first qualifying candidate, or explicit end-of-layer

SEEK_PHYSICAL_PREFIX(read,L,p,high_prefix):
    reset traversal; set layer and PHYSICAL_PREFIX inclusive high edge
    TOP: next set population bit at prefix>=p
    GROUP: first original suffix>=p's suffix, then later nonempty groups
    TREE: descend first physical child interval not entirely<p; at leaf take
       lower_bound(prefix_column,p), advancing same-region siblings if needed
    missing/exhausted natural group/region uses same-layer ordered presence
    on every success SET_POSITION, including lazy group/region successor identity
    return ENTRY_AT_POSITION when prefix<=high_prefix, else end

ADVANCE_TREE_SIBLING(read):
    ascend saved frames to first unvisited right child
    bind it under SAME token; descend its first possible children
    save the new leaf/local rank; exhaust region only when no saved sibling remains

NEXT_CANDIDATE(read):
    if TOP: next original occupied prefix in selected table / next table chunk
       SET_POSITION on the resulting physical prefix
    if GROUP:
        suffix=ctz(original_mask with all suffixes<=current cleared)
        if found: slot=GROUP_SLOT(group,suffix); SET_POSITION
        else advance ordered group-presence cursor, lazily seeking >group if needed
    if TREE:
        increment ROUTING rank in selected leaf; resolve payload slot separately
        if exhausted: ADVANCE_TREE_SIBLING
        only after whole region exhaustion advance region presence, lazily >region
        SET_POSITION on the final selected leaf/rank
    retain same token/layer; never reseek by a supposedly unique logical key
    if end: return end
    if LOGICAL and next routing minimum fails logical high edge: return end
    if PHYSICAL_PREFIX and next prefix>high_prefix: return end
    reuse selected payload/extra blocks; return ENTRY_AT_POSITION(read)
    # No u32(last_prefix+1) sentinel at maximum prefix.

READ_POINT(read,L,q,requested_operation):
    e=SEEK_CANDIDATE(read,L,q,closed=true,high=q,high_closed=true)
    while e exists and e.routing_minimum<=q:
        result=PROJECT_MANY(read,e,requested_operation.required_ordinals,query=q)
        if exact result(s): emit per declared duplicate/output contract
        e=NEXT_CANDIDATE(read)  # even after exact miss; stay in SAME region first
    return outputs/miss; no candidate is silently promoted to an exact row hit

START_RANGE(read,L,low,high,low_closed,high_closed):
    if query empty: return end
    e=SEEK_CANDIDATE(read,L,low,low_closed,high,high_closed)
    return e only if routing minimum passes high edge
    # Caller PROJECTs exact results then NEXT_CANDIDATE, including false candidates.

DESCEND(read,source_L,parent_prefix,parent_position,destination_L):
    require destination_L-source_L in {1,2} and destination_L<=4
    if one layer:
        p=(parent_prefix<<8)|(parent_position>>8)
        slots=[(parent_position&255)<<8,((parent_position&255)+1)<<8)
    else: p=(parent_prefix<<16)|parent_position; slots=[0,65536)
    return FIND_EXACT_PREFIX(read,destination_L,p),slots
    # Explicit new-layer lookup; never cross-layer NEXT.
```

For example, Aactual[1,2]/fence[0,10] and Bactual[5,6]/fence[4,20] admit A
first for q5. Exact A processing misses; NEXT_CANDIDATE must then visit B before
leaving their region. Duplicate equal keys can cross any leaf/group boundary;
continuation uses physical prefix/routing position, not uniqueness of q.
Long-key abbreviation ties compare full bytes. Enclosing fences, guides and
byte-padding grants remain distinct facts.

## Tiny exact sources and quantised guide alternatives

```
PREPARE_ENDPOINT_SOURCE(exact E[0:n], domain, placement):
    consider exact native u8/u16/u32/u64 at the ACTUAL domain width
    strip a common logical prefix/origin where it is a proven order transform
    exact relative recipe is legal only when every offset fits the selected lane
    exact quantum recipe also requires every endpoint aligned to 2^s
    consider prefix-stripped SeriesPack endpoints with a reused native boundary
    keep ordinary exact source for tiny/degenerate or very ambiguous nodes
    # Do not allocate or analyze this descriptor anew in every query.

PREPARE_GUIDE(exact E, stable_origin, shift):
    G[i] = floor((E[i]-stable_origin)/2^shift), admitted to selected code width
    for byte keys, use an equivalent ordered prefix code and exact prefix checks
    keep count/population independent of equal or zero guide codes
    save exact endpoint source and first-greater fallback semantics
    if packed widths are chosen:
        D[i] = G[i]-G[i-1]; store G16 checkpoints
        admit u4 only if every D<=15 and the checkpoint/group domains fit
        # Rounding original widths independently is NOT equivalent.

GUIDED_BOUNDARY(source, q, edge):
    edge is GE for lower_bound or GT for upper_bound
    if n==0: return n
    if admission proves EXACT constant endpoint x (including K=0):
        return next_eligible(0,n) when x satisfies edge, otherwise n
    compare q with admitted origin/domain BEFORE unsigned subtraction/shift
    if q precedes the whole domain: return source.next_eligible(0,n)
    if q exceeds the whole domain: return n
    code = quantise q under the SAME stable guide law
    first_equal,first_greater = geometric UNFILTERED bracket on G
    # Optimised Ikea proposal: ONE loaded guide/query preparation gives both ranks.
    # Packed widths feed a shared register-level prefix/bracket directly from
    # their decoder, not decode -> scalar scratch -> generic owner selection.
    # Two ordinary geometric boundary calls are its independently measured control.
    if first_equal == first_greater:
        global_rank = first_greater
    else:
        exact_window = UNFILTERED E[first_equal:first_greater]
        r = exact lower_bound(window,q) if edge==GE, else exact upper_bound(window,q)
        global_rank = first_equal+r       # first_greater is valid fallback
    return source.next_eligible(global_rank,n) ONCE on the COMPLETE source
    # Eligibility is identity when admission proves every rank populated.
    # Refine an equal CODE run, including duplicate exact keys. Never accept
    # an approximate rank or infer absence/population from a zero guide width.

GUIDE_ONLY_PARENT_BOUNDARY(parent, read, q, edge):
    bracket = owner-stage guide candidate interval and first-greater fallback
    resolve exact child ROUTING extrema under read.token when correction is required
    finish exact boundary against those coherent child sources
    retain the candidate/Scope if this needs a genuine wait
    # Exact Boundlist API still returns an exact answer; no suspension inside it.

UPDATE_GUIDE(edit, child_before, child_after):
    compute old/new endpoint codes with stable origin/scale
    check endpoint/quantiser domain BEFORE narrowing or testing unchanged
    if escaped: privately recode or disable guide and repair affected metadata
    else if guide_only AND codes_unchanged AND NOT exact_parent_extreme_changed
            AND NOT membership_changed:
        omit only the guide-induced parent write
        still publish any other changed parent reference/proof/metadata
    else repair changed guide/exact extrema on the real path
    publish descriptor, guide and exact sources coherently; invalidate old binding
```

The guide-only variant must scan/refine coherently, not mix approximate parent
codes with a newer child's extrema. All-equal guides can lose; a rightmost append
can still change the exact aggregate maximum at every ancestor. Fanout and
child-name access, exact correction, captured Parts and update invalidation
are charged before selecting this variant.

## Directory to Part handoff

The fast raw-pointer path has a concrete cross-Object invariant: this selected
directory's byte pointer was obtained from the primary segment published in
the same coherent graph; any still-equal pointer is retained with the complete
required footprint by the newer selected primary. The same live Core/Scope
protects late discovery even if the segment was not separately bound.

That rule protects BYTE addresses, not an old Representation header, Control,
View address slot or invented locator cell. The directory material must retain
its own interpretation/geometry dependency. A directory Part exported alone,
a fresh Scope, or an unsealed alternative does not preserve that graph.
Borrowed may protect the uninterrupted current-call variant by excluding every
invalidating boundary; it supplies no retained old-cut or post-return lifetime.

```
QUALIFY_MATERIAL(read,entry,operation):
    reuse already-qualified material ONLY when its identity, token/cut, schema,
       geometry and committed/applied frontier cover this operation
    else if admitted representation law proves delta-free for THIS operation/read
            AND all required runtime interpretation is already available:
        use that qualified direct context; no extra segment bind
    else:
        bind authoritative segment/partition/collection material via read.token
        admit its base, applied frontier, committed delta extent and read law
    # Fixed pointers/geometry alone do NOT prove a fixed committed delta extent.
    return selected context; reuse one selection across all requested Parts

PROJECT(read,entry,part_ordinal,requested_operation):
    return PROJECT_MANY(read,entry,[part_ordinal],requested_operation)

PROJECT_MANY(read,entry,ordinals,requested_operation):
    def = reuse admitted definition(entry.dirent.representation,read.layer)
    material = QUALIFY_MATERIAL(read,entry,requested_operation)
    obtain needed geometry from material/directory proof/admitted byte header
       bind shape once only if it remains unavailable; charge actual selection
    if any requested ordinal>=6: extra=SELECT_EXTRA_LIST_ONCE(read,entry)
    for each ordinal:
        locator = inline field or EXTRA_LOCATOR(extra,ordinal-6)
        resolve actual resource binding/readiness when recipe requires it
        admit complete byte footprint and interpretation for needed native binding
    if material proves direct/delta-free:
        invoke real bounded native/Ikea consumer for requested output
    else:
        READ_MATERIAL_CANDIDATE(read,material,requested_operation)
           using the qualified native bindings above
        # Includes visible corrections/bypass, not just base-byte consumption.
    return useful output within token lifetime, not an escaping unprotected pointer

SELECT_EXTRA_LIST_ONCE(read,e):
    if StreamPack recipe:
        block = reuse current block or read.token.bind(e.overflow_block)
        view = already-admitted StreamPack view of this selected block
        value = StreamPack.value(view,e.overflow_rank)
        return protected locator span and selected length
    if paged vector recipe:
        return selected root, length and reusable path/block cache

EXTRA_LOCATOR(extra,ordinal):
    check ordinal against selected list length
    if direct StreamPack span: return its eight-byte locator at ordinal
    otherwise descend ordinal's fixed-fanout path under the SAME token
       reusing selected path/block for subsequent ordinals
    return selected block's locator at local ordinal
```

The fixture must exercise qualified direct byte handoff, selected material
with fixed geometry but changed delta extent, and any additional geometry bind. It may not price the former then silently implement the latter. If a
resource locator intentionally names an Orbital segment/Part instead of a raw
body, its extra selection and eliminated address repairs are a separate proposal;
the supplied memory-pointer interpretation is not silently replaced.

## Private editing

```
BEGIN_EDIT(basis, deltas, optional_after_schema_Owners):
    keep the real basis and offered finished Owners alive in the SAME Core
    select before schema from basis
    select offered AFTER schema with existing select_private over FINISHED Owners
       or reuse before schema when no schema change is offered
    # Uncaptured drafts are not passed to select_private.
    validate before bindings/absence under before schema and actual finished
       replacement or retained unchanged material under AFTER schema
    equal numeric cuts across Cores are invalid
    coalesce changes to each SegmentName using actual Ikea scratch
    reserve/grow private scratch while no accepted publication exists
    mark all leaves/groups with final membership/bounds changes BEFORE copying
       native payloads; structural batches materialize after normalization
    return Edit

PRIVATE_OBJECT(edit, object_name):
    find full ObjectId in inline full-ID array or high64/low64 nested Ikea maps
    if a mutable draft already exists for THAT full identity: return it
    old = edit.basis.bind(object_name)
    make an uncaptured draft with retained-source references to old Parts
    put it under BOTH identity halves in private scratch; return it

WRITABLE_PART(draft, p):
    called during final materialization, or the proven nonstructural fast path
    if already privately writable: return it
    acquire a NEW destination range disjoint from all sealed bodies/padding
    copy the complete old admitted Part body to it, if any
    retain its exact initialized extent, alignment and readable padding
    replace only draft's Part p source; no old storage is modified
    return private bytes

APPLY_DELTAS(edit):
    for each coalesced SegmentDelta d:
        select expected old Entry and qualify its before-primary association
        derive after fields/bounds from FINISHED replacement or qualified retained
           material under AFTER schema, using the actual selected locator recipe
        save the resulting private Entry intent, or deletion intent
    PLAN_EXTRAS_FOR_BATCH(edit)                 # before the descriptor no-op test
    for each offered intent and additional holder-coordinate repair:
        after = intent plus FINAL overflow block/rank/recipe from extra plan
        if deletion: DELETE_ENTRY(edit,name)
        else if old absent: INSERT_ENTRY(edit,after)
        else if descriptor, bounds and DIRECTORY-OWNED interpretation are unchanged:
            omit the dirent/bounds write
            # Extra-only replacement may ALREADY be a changed block draft.
            # Unchanged64 B dirent NEVER proves the seventh pointer unchanged.
        else: UPDATE_ENTRY(edit,old,after)
    seed normalization ONLY with actual final occupancy violations or explicitly
       requested/priced compaction; bounds-only dirty nodes keep legal boundaries
    normalize those runs bottom-up, including neighbouring nodes needed
       to make EVERY final non-root node legal
    FINALIZE_EXTRA_BLOCK once for each affected final local plan
    materialize final metadata/payload Parts once per changed object
    verify all new pointers name after-primary bytes in the SAME packet,
       or exact retained footprints/proofs still selected by its basis
    freeze only after all holder/bounds/topology work is complete

PLAN_EXTRAS_FOR_BATCH(edit):
    below is the default local StreamPack path; paged-vector recipes dispatch
       to their admitted ordinal/block edit law, not to StreamPack sidecar writes
    omit only no-old/no-final association, or qualified COMPLETE unchanged
       association for a nondeleted holder with a nonempty final list
    coalesce all other after-lists/deletions per physical-prefix group/block
    for each affected block:
        enumerate current holders by physical-prefix cursor over its16-prefix group
           merge with coalesced offered Entry intents to obtain FINAL live holders
        # No global reverse map. Charge routing/cursor work, at most 16 holders.
        call PLAN_EXTRA_REMOVAL FIRST, then PLAN_EXTRA_NONEMPTY for remaining lists
        if final plan exceeds assigned/dead-byte/capacity policy:
            build packed final block directly ONCE, not append-then-pack repeatedly
        else plan only actual legal replace/append/retire operations
        derive final block/rank/recipe overrides for every affected holder
        add metadata-only holder intents, including holders without a segment Owner
    # Record source certificate and private-copy requirements for every touched plane.
    # Actual writes happen only after complete plan/capacity/effect admission.

ENUMERATE_LOCAL_HOLDERS(edit,block):
    derive index/layer/group from admitted block identity
    high=min((group<<4)|15,maximum_prefix_for_layer), computed without u32 overflow
    start SEEK_PHYSICAL_PREFIX at group<<4 through high under edit.basis
    visit NEXT_CANDIDATE in that SAME layer with the PHYSICAL_PREFIX edge law
    keep entries actually naming this block; merge offered births/deaths/replacements
    verify <=16 live holders and unique assigned rank per holder in default recipe
    retain every source through final transfer; no logical-key uniqueness assumed

UPDATE_ENTRY(edit, before, after):
    route by exact physical prefix, not a record-row search
    choose small-group payload, co-located leaf Part1+slot/C, or independent chunk
    if descriptor differs: record its64 B replacement in the chunk draft
    materialize that Part once after final placement; a proven locator-only batch
       can immediately take WRITABLE_PART without a balancing phase
    if bounds/geometry differ:
        record edits to the leaf's routing/tail draft
        compare the leaf's old/new routing extrema
        propagate ONLY changed routing extrema toward the root
    apply the already-final extra-plan metadata, without rerunning list packing
    if only representation changes: replace the real representation field/proof
    # No forced revision change, rank repair, page balancing or map rebuild.

EDIT_HASH_MAPPING(group, membership_or_slot_change):
    if mapping unchanged: retain the exact admitted old Ikea mapping owner
    else:
        clone that actual <=16-key Ikea owner once, before any guarded admission
        use ordinary private-owner insert/modify/erase for the changed suffixes
        # Slot meaning and payload/bounds move together. Deletion can fill the
        # removed payload slot with the last row and repair that one mapping.
        # slot_to_suffix[last] identifies that moved suffix; update its inverse.
        # Maintain ORIGINAL-suffix mask separately from the permuted hash bitmap.
        # table<4,0,1> has bitmap capacity 16 and no Swiss/growth path.
        retain this new mapping and matched payload in the finished group proof
```

Ordinary private-image Hashtable calls are valid when the enclosing operation
publishes the whole independent replacement. Guarded calls are used when there
is an actual grant/issued-effect consumer: `enter` admits stable access,
`prepare` reserves every reported damage range, then infallible `before` hooks
precede stores. None may retarget already-computed addresses. Growth/COW ends
the old admission and obtains a fresh one. Ikea issued spans are resolved while
their named source lives; they are not an Engine rollback journal.

```
INSERT_ENTRY(edit,e):
    if top table or shared-prefix microhash group:
        make group/table draft, allocate its bounded payload slot, write Entry
        update its population/mapping and exact bounds
        if first member: insert that group in SAME-layer ordered presence
        return
    find its region and ordered leaf by physical prefix intervals
    if absent region: create its singleton root and insert region presence
    else:
        record an unplaced Entry intent and insert its logical routing row
        assign ALLOCATE_PAYLOAD_SLOT only after final output count fits capacity
        # Slot mask is selected/published state, never an unversioned freelist.
        check neighbouring exact bounds, including across a leaf boundary
        queue this leaf/path for final batch balancing

ALLOCATE_PAYLOAD_SLOT(edit, leaf):
    slot = first zero bit in leaf's PRIVATE payload-slot mask (admitted capacity)
    require a final legal-sized output leaf with a free slot; overfull input
       remains an unplaced intent until normalization, never an invalid slot write
    chunk_index = slot/C; row = slot%C
    if co-located recipe:
        clone/edit leaf Part1+chunk_index; set private slot bit
        return leaf name, Part1+chunk_index, row
        # New leaf initializes ceil(F/C) admitted64*C-byte payload Parts once.
    if leaf.chunk_names[chunk_index] is empty:
        allocate a fresh scoped name via ALLOCATE_NAMES(edit,1)
        create a 64*C-byte chunk with all admitted bytes initialized ONCE
        leaf.chunk_names[chunk_index] = that name
    clone/edit that chunk Part; set leaf's private slot bit
    return chunk name and row

ALLOCATE_NAMES(edit,n):
    counter = PRIVATE_OBJECT(index_layer_namespace_counter)
    checked reserve n low64 names; exhaustion refuses without wrapping
    combine with index/layer's admitted unique dynamic namespace high64
    increment private counter once; include its finished one-Part Owner in packet
    # Counter survives empty regions; no aliasing on region recreation.

DELETE_ENTRY(edit,name):
    find existing Entry; respect the offered absence/update semantics
    remove its routing/mapping row and clear its private payload slot/mask bit
    if an INDEPENDENT payload chunk becomes empty: retire its Object in packet
    co-located fixed Parts retain their admitted capacity; unused slots are unread
    if group/region becomes empty: remove its ordered presence entry and root
    otherwise repair changed routing extrema and queue underflow handling
    retire the supplied segment only through the coupled Orbital change
    # No segment identity is retained merely to avoid handling empty membership.
```

## Ordered balancing and payload compaction

```
NORMALIZE_LEVEL(edit, final_nodes, capacity, minimum, merge_limit):
    target_low = 13 when (capacity,merge_limit)==(32,26)
                 53 when (128,106), 48 when (128,96)
    separate ordinary descriptor/bounds edits from structural violations
    seed runs with overfull/underfull non-root nodes or explicit paid compaction
    preserve boundaries of other legal nodes, even when their metadata is dirty
    form maximal adjacent runs of the seeded nodes at this storage depth
    read their FINAL row/child streams after offered edits and lower-level repair
    if a nonempty run has no output count r with
       ceil(total/merge_limit)<=r<=floor(total/target_low):
        extend with an adjacent node, even across a parent boundary
        coalesce any pending run it touches BEFORE materializing output
        repeat until legal, or until this is the whole region tree
    empty runs retire their node names and remove their parent rows
    for each final nonempty run:
        if it covers whole region and total<=capacity:
            plan a root-sized leaf/output and contract the obsolete ancestor chain
        else if one pair can merge with target_low<=total<=merge_limit:
            produce one output using the left name
        else:
            choose r in [ceil(total/merge_limit), floor(total/target_low)]
            prefer the existing legal count unless an over/underflow requires change
            distribute total near evenly into r outputs in [target_low,merge_limit]
        reuse legal node names, allocate extra names, retire surplus names
        BUILD_BALANCED_OUTPUTS for this final run ONCE
        replace child rows/bounds in every affected parent, not just one parent
    apply resulting parent rows/bounds; propagate ordinary bound changes in place
    repeat only actual parent occupancy violations at the next depth
    root with one child contracts; empty root/region/presence entry retires
    if final root overflows, allocate child names and publish a higher root
       at the SAME deterministic region-root name; never make it its own child
    contraction copies/adopts the sole child's legal contents into that root
       name and retires the obsolete child; carry namespace allocation state
    do not freeze until every final non-root node satisfies occupancy

SCALAR_FAST_BALANCE(path, leaf):
    if overfull: split near half; update parent and recurse upward if required
    if underfull: use legal adjacent sibling; merge when union<=merge_limit
       else redistribute; repair parent and handle its underflow
    if a whole batch underfills BOTH siblings, use NORMALIZE_LEVEL instead
    otherwise propagate only changed ROUTING extrema under its admitted recipe
    # The scalar pair rule is NOT a proof for arbitrary final-batch deletion.

BUILD_BALANCED_OUTPUTS(outputs,old_nodes):
    if this storage depth is INTERNAL:
        BUILD_INTERNAL_OUTPUTS(outputs,final_child_rows); return
    BUILD_LEAF_OUTPUTS(outputs,final_Entry_rows,old_nodes)

BUILD_INTERNAL_OUTPUTS(outputs,child_rows):
    for each final output:
        form one routing-metadata Part:48-byte header + child prefix/bound/name columns
        use stable child names under admitted namespace, correct height/count
        derive parent bounds from CHILD ROUTING endpoints, not record extrema
        no dirent payload Parts, slot copying or subtree-segment-count field
    record new/replaced/retired internal names and root changes as drafts
    # FREEZE_AND_PUBLISH captures each final Object once, not here.

BUILD_LEAF_OUTPUTS(outputs,Entry_rows,old_leaves):
    if co-located recipe:
        for each output form routing Part plus ceil(F/C) payload Parts of 64*C bytes
        copy live rows once; initialize unused admitted bytes/padding
        retain entire old Part only if its byte/slot interpretation remains valid
        record actual 1+ceil(F/C) sources per output; no payload-Object tombstones
    else:
        retain whole payload chunks only when all live rows stay in one output
        compact remaining rows into exclusive output chunks of 64*C bytes
        form each chunk-name vector of ceil(F/C) names and local slot mask/map
        retire emptied old chunks; record all new/replaced/retired chunk drafts
    form final prefix/bounds/slot columns, preserving 64-byte dirent stride
    record output leaves, changed parents/counter and retired names
    # Publication is deferred until all batch repairs are complete.
    # Worst work includes every copied row, source, tombstone and retained backing.
```

F and C come from the admitted recipe on every path. C8 uses 512-byte Parts;
C16 uses 1,024-byte Parts, slot/C and slot%C, and half as many payload coordinates.
F32/C8 co-location captures five Parts of one leaf after copying one 512-byte
Part; F32/C16 captures three after copying 1,024 bytes. An independent payload
Object captures one Part but adds a selected Object on reads. Full source fanout
is charged even when only one Part changes.

For a NEW compound leaf, current grouped native capture is a control: one fresh
Allocation covers all admitted Parts, is consumed once, and each Part's geometry
and readable padding is validated. Partial COW can then capture independent
native sources, one fresh plus the retained Parts from that grouped owner;
retained Parts may keep the WHOLE original Allocation alive. Split outputs each
have their own valid physical owner. One Allocation is never consumed by two
captures. Ranged native capture remains a separate proposed Orbital optimization.

## Extra Parts and schema

```
PLAN_EXTRA_REMOVAL(before,after):
    if deletion or final extra-count==0:
        if old list exists:
            require admitted tombstone D, or plan copy/convert to a D-enabled block
            record private D coverage required if StreamPack.retire is executed
            mark old rank retired in FINAL local plan; old Scopes keep old D
        clear after overflow recipe/block/rank; return handled
    return not_handled

PLAN_EXTRA_NONEMPTY(before,after):
    if PLAN_EXTRA_REMOVAL handled: return
    if qualified unchanged: return retained
    Unew=8*final_extra_count; Rold=selected reservation when old list exists
    replace_legal = old live rank AND Unew<=Rold AND schema/maximum accepts Unew
                    AND (has_U OR Unew==Rold)
    # Full/no-U R64->U56 is ILLEGAL despite fitting; use a legal new reservation.
    if replace_legal:
        record required private payload and every issued U/control plane
        plan actual StreamPack.replace, OR qualified fixed-length TuplePack locator
           replacement when length/reservation/sidecar facts really stay unchanged
    else:
        choose legal reservation Rnew, respecting fixed/max/full/no-U law
        if none exists, plan format conversion or paged vector, not illegal append
        plan append to private payload/control/directory/lengths, and retire old D
        # Append can write count,extent,checkpoints,R,U,whole G16 groups,D,padding.
        # Logical suffix-only growth does not grant writes to sealed sidecars.
    defer assigned/dead-byte/capacity decision until ALL holder plans coalesced

FINALIZE_EXTRA_BLOCK(edit,final_plan):
    if no final holders: retire the unused block Object in packet; return
    choose final destination/source plan; materialize required private Parts ONCE
    admit actual payload/control/directory/R/U/group-length footprints BEFORE calls
       including preserving neighbours, zeroed reservations and readable padding
    if packing/growth needed:
        native transfer initializes private destination sidecars including D and
           includes only FINAL live lists; derive local rank remap
        omit removed ranks, never mutate old source D or any captured destination
    else execute admitted replace/append/retire on private affected Parts
    initialized destination sidecars are private even when payload growth is disjoint
    list beyond StreamPack limits uses paged locator-vector recipe, never truncates
    finish every holder coordinate repair before FREEZE_AND_PUBLISH
    # One packing decision/block/batch; no independent intermediate publication.

SCHEMA_DEFINITION(read,rep_id,layer):
    header=read.selected_definition_header  # definition_namespace high64,next_id u64
    if uint64(rep_id)>=header.next_id: reject outside THIS selected schema domain
    # A newer escaping cache record can outlive an older Scope/header; ownership
    # does not make that definition visible to the old schema.
    name=ObjectId(header.definition_namespace,uint64(rep_id))
    record=read.cached_qualified_definition(name) or read.token.bind(name)
    reject absence or layer not admitted by selected record
    admit/reuse descriptor/Part/read laws; retain actual immutable dependencies
    cache escaping the read must OWN those dependencies, not a closed-Scope borrow
    # No population-sized schema roster, no truncation of full ObjectId.

APPEND_SCHEMA_DEFINITIONS(edit,offered_definitions):
    # Concrete direct-name experiment control; not a selected universal schema format.
    schema_header=PRIVATE_OBJECT(index_schema_header_name)
    checked reserve fresh32-bit rep_ids via u64 next_id; exhaustion refuses, no reuse
    for each offered definition:
        validate actual descriptor/Part laws and per-layer admission mask
        construct immutable definition record with namespace-derived ObjectId
        capture that record's actual descriptor material into a FINISHED Owner
    increment private header.next_id; capture changed16-byte schema header
    edit.after_schema=select_private(basis,FINISHED definition/header changes,names)
    publish definitions/header/actual segment+directory changes in ONE Core packet
    # D new definitions cost D record captures/catalog changes plus one header.
    # No existing definition or 100k-sized roster is copied/captured.
    # Cache is namespace/Core/definition-qualified; rep_id alone is insufficient.

MIGRATE_SEGMENT(edit, name, target_definition):
    select source primary/definition under edit.basis and before-schema
    admit target definition from edit.after_schema, including offered new definitions
    construct exact equivalent target privately with the producer/real codecs
    offer its FINISHED Owner plus new locator/interpretation/bounds to APPLY_DELTAS
    publish directory and primary together at one completed cut
```

That migration uses current atomic logical publication. Same-logical-version
alternative adoption is a distinct Orbital question: individual `adopt` calls
cannot be assumed to atomically repair a separately stored raw-pointer
directory. No primary/alternative substitution or Engine conversion-version
ledger is introduced to pretend otherwise.

## Capture, publication and failure

```
FREEZE_AND_PUBLISH(edit, mode):
    use a stationary EMPTY Prepared for this attempt
    # Today's packet is single-use after acceptance, install or abort. Only a
    # refused still-empty packet is retryable; RESET_PREPARED below is proposed.
    finish ALL mutable destination writes, including holder/bounds repairs
    for each changed directory draft, once:
        make admitted immutable Control from the shared format proof and counts
        sources = certified source(old_selection,p) for unchanged Parts
                  + finished new bounded sources for changed Parts
        result = capture through Orbital
        if capture failed:
            keep before-source lifetimes through all cleanup callbacks
            release finished candidate Owners and unconsumed Allocations/drafts
            # Refused capture did not consume that capture's fresh Allocations.
            # Earlier captures may already own theirs; settle each use once.
            charge synchronous proof disposal and remaining physical debt
            keep attempted append highwater sealed; return actual error with logical input
            # Retry reselects/revalidates; it never rewrites the failed suffix.
        retain returned finished Owner; its body+padding is sealed before prepare
    all_changes = offered segment changes + changed directory/schema/extra Objects
                  + real deletion/tombstone changes
    omit wholly unchanged Objects; include no duplicate publication for one name
    if all_changes empty: release scratch; return unchanged
    result = Orbital.prepare(edit.basis, all_changes, prepared_workspace)
    if stale:
        abort/release caller-held candidate uses; Orbital settles physical returns
        # Do not force a full drain or background-reclamation wait here.
        # Actual synchronous owner/proof disposal can still run and is charged.
        # Pending physical uses remain charged and prevent premature storage reuse.
        return stale with original inputs; recompute against a new actual basis
        # Opening a fresh Scope alone never validates these older afterimages.
    if capacity refused:
        # Failed prepare leaves the packet empty and input Owners unchanged.
        retain finished candidates, original basis and interpretation dependencies
        refill/wait only with an actual resource-progress contract, outside acceptance
        retry the SAME candidates if that basis is still current
        if basis became stale: abandon/recompute as above
        # Do not rotate/copy a sealed append prefix merely to retry prepare.
        # A synchronous caller may instead return refusal and settle its candidates.
    if another error: settle candidates and return that error, not a refill loop
    if accepted:
        Orbital.install(prepared_workspace) once
        # One cut; no allocating Engine cache update or undo commit afterwards.
    release source certificates/drafts; schedule bounded Orbital history traversal
    # A traversal step can trigger last-owner graph disposal; that cost is NOT
    # bounded by the traversal limit. Include it or arrange owner-side disposal.
    charge retirement, failed attempts and retained capacities to full history

ABORT(edit):
    if packet was accepted: Orbital abort settles it under existing exclusion rules
    # A failed prepare has no accepted reservation to abort.
    release all caller-held candidate Owners and physical sources as well
    discard private drafts and the scratch Hashtable
    published directory/segments are unchanged; there is no reverse replay
```

Two supported physical controls precede any proposed native optimization:

```
CONSUMING_NATIVE:
    each fresh Allocation transferred once to its capturing Representation
    distinct captures do NOT consume the same Allocation again

COUNTED_SHARED_BATCH_SLAB:
    one counted owner holds a real Loom Allocation, stable address and capacity
    partition PRIVATE, nonoverlapping body/padding ranges
    finish writes; submit bounded slices as shared/mixed PartSources
    each capture/escape retains actual backing through Orbital's physical contract
    rotate at exhaustion; retained old slices can retain whole slab capacity
    no Engine reader ledger permits hole reuse or relocation
```

These are custody mechanisms, not a prescription to pool Segment bodies.
Segments keep independent Allocations. Directory storage can use bounded top
tables, actual Hashtable-owned inline Dirents and partitioned arrays below.
Several Parts may name one admitted directory backing; they still incur their
descriptor slots, validation and physical retention. Narrowing a certified Part
does not independently reclaim its surrounding native backing.

## Proposed Orbital work

This section is coordinated with Orbital LEAD. It changes physical retention
and publication cost inside Orbital, not Engine's visibility semantics.
The following ready-path changes belong inside Orbital; Engine should not add
private allocation inventories, reader lists or version maps to avoid them:

```
ORBITAL_OWNED_COSTS:
    choose eligible finite geometry before growing inventory
    find a pending preparation without rescanning every owned Group
    validate every captured Part, but preflight one grouped fresh supplier ONCE
    drain selected history by cut intervals, not every reader cut per Object
    keep last-owner disposal distinct from bounded history traversal
```

These are implementation changes, not a new caller API. The range-source,
prepared-stock and staged-resolution mechanisms below remain proposals.
Today's prepare may allocate metadata and resolves identities in several
passes; its cost is still charged until the proposed path is implemented.

```
PROPOSED_BOUNDED_NATIVE_SOURCE(writer_allocation, body_range):
    borrow identity/address/extent until capture; factory itself adds no pin
    capture revalidates after metadata/proof callbacks
    admit destination body+padding; enroll its REAL native range use
    seal exactly that admitted footprint; writer Allocation survives
    writer can only write other physically disjoint, unsealed bytes
    preserve range through retained recapture, grouping, export and final return
    on refusal undo acquired physical uses without consuming writer authority
    # Keep current cheap consuming mode. No free arbitrary-update heap is implied.

PROPOSED_PREPARE_STOCK:
    prepare physical inventory, descriptor/proof storage, mutation workspace
       and actual catalog/history/registry/debt capacity before final ready visit
    use the SAME stable Core memory-resource identity for descriptor pooling
    wait with no accepted packet and no borrowed catalog cell/head
    stock/capacity warming grants no exclusive slots or later acceptance

PROPOSED_READY_PREPARE(basis, ALL_changes, workspace):
    use existing prepared capacity; no heap/VM work, wait or catalog reentry
    validate Core/incarnation, exact current basis, packet/pending state, cut bound
    validate finished Owner admission and DISTINCT full Object IDs in Orbital
    resolve each Object ID ONCE into workspace
    classify required old tips, entries, registry slots and debt/history records
    claim actual free capacity, including tombstone/load/rehash headroom
    if insufficient:
        release all temporary claims, retain input Owners, return capacity
    build predecessor records, retain replacements, pin entries
    mark accepted/pending only after complete success
    # Preserve old tips even if no reader is enrolled now: one can enroll before install.

INSTALL:
    same uninterrupted Orbital loop over ALL changes and one cut advance
    no allocation or owner-disposal callback

RESET_PREPARED:
    legal only after installation or FULL abort/disposal callback settlement
    preserve original Core/resource identity; retain reusable workspace capacity
    never reclaim descriptor/proof memory still owned by a Representation or Part

PROPOSED_COLD_BIND:
    optional advisory Scope-prefetch(ObjectId), without retaining raw catalog cells
    if full staging is needed, Orbital owns the staged resolver:
        retain Scope + full ObjectId
        queue those identities/state with EMPTY initial MemoryDependency
        # push/promotion may hint before callback: no mutable bucket address here
        derive/validate a dependency when the Ring actually activates the frame
        discover bucket/entry/history/Representation dependencies
        revalidate/restart across rehash, entry reuse and history-slot reuse
        revalidate before dereference after returned-boundary mutation
    cut equality alone is insufficient structural validation
```

These mechanisms still charge range enrollment/drop, capture descriptor work,
all admitted Parts, Owner admission and reclamation. A native batch source is
rotated/released at exhaustion; no caller can recycle an old hole just because
its logical entry was replaced. Actual reuse belongs to Orbital allocation.
For a permanently closed append backing, the current writer can already
relinquish an unused tail after the highest attempted sealed footprint, including
readable padding, then request cold trimming. The latest published extent alone
is insufficient after a longer failed capture. This does not reclaim interior
holes or make native Part pins range-aware.

Range enrollment and release must describe exactly the same destination body
and padding, including narrowed sources and selected-copy exports. Deriving
that range cold from immutable Control geometry may preserve compact native
records; its mixed/grouped ownership representation and cost remain unqualified.

The paper histories that distinguish these changes are:

- Keep an old Scope, replace a referenced primary, then discover it late through
  the old directory. Body bytes remain valid; old metadata slots are not implied.
- Enroll a reader after prepare but before install. It still selects the
  predecessor, even if prepare initially observed no enrolled readers.
- Capture a native range, relinquish its writer tail, then trim. Body and padding
  survive until every relevant recapture/export retires with matching coverage.
- Refuse or abort with disposal callbacks and caller-held candidate Owners.
  Visible heads stay unchanged; packet reset waits for callback settlement,
  and storage reuse waits for its actual uses rather than only packet abort.

## Loom execution, admission and genuine waits

```
SYNCHRONOUS_READY_CALL:
    use Borrowed for a complete nonreentrant current-cut READ when permitted
    use actual Scope basis for edits/prepare, even when execution is ready
    call admitted native consumers directly; no Task/Ring per lookup
    if unexpected would-wait with no accepted stationary operation:
        finish/discard private work and end all stack borrows/Scope uses
        return explicit unavailable/refusal with retained LOGICAL retry input
        # No moving Scope or implicit Borrowed->old-cut promotion.
        # A later attempt constructs a new Scope and revalidates/recomputes.

ADMIT_POTENTIALLY_WAITING_OPERATION(worker,caller_owned_stationary_frame):
    retain input/result storage and construct Scope IN PLACE in the stable frame
    initialize fresh Task(context=frame,step=operation_step)
    if !loom.start(worker,Task):
        settle private ownership; return refused with frame/Worker unchanged by start
        caller retains or destroys frame only after its actual uses end
    else:
        return accepted pending operation; caller contract keeps frame/Task/Scope,
           Signal and result alive through terminal/detach AND native/physical cleanup
    # start queues, it does not run inline. Admission itself is not readiness.
    # No hidden unbounded pending queue or unadmitted frame wait is introduced.

KNOWN_INDEPENDENT_KEYS:
    use prefetch_for_each / IndependentStream ONLY for synchronous, nonthrowing,
       nonrecursive consumers that finish each indexed operation before returning
    qualify all input/native binding/hint footprints for the complete visit
    keep reductions local; publish before native tail transfer
    do not wait/throw/park mid-quantum: cursor updates only after the full visit
    if a request can need I/O/admission/producer wait, use separately retained
       operation state in an admitted Task instead, not partial-visit replay
    prefetch is a cheap qualified hint, never a full duplicated bind/tree walk

DEPENDENT_INDEPENDENT_REQUESTS:
    configure reused Ring storage outside hot loop; charge footprint/admission
    stable frame owns actual Scope/selection/input/output/continuation state
    use concrete hint stages below; fuse adjacent ready work first
    yield_unchanged at actual independent memory dependency, finish/retire on result
    charge bootstrap, refill, output, final drain and physical cleanup
    # Today's opaque Scope.bind remains one synchronous Orbital call.

ACTUAL_WAIT(frame,resource):
    require ALREADY accepted stationary Task with the retained-frame contract above
    observed=resource's REAL owner-Lane Signal FULL generation
    status=test actual resource predicate; if ready continue, if failed settle/error
    save status/dependencies/observed in stable frame
    retire/remove native continuations at returned boundary before ending borrows
       active AND overflow frames count; Task detach alone removes no Ring slot
    hold no accepted Prepared across wait; keep Scope/frame/Signal through enrollment
    return Progress.wait(actual_Signal,observed)
    # Cleanup may pulse Signal; never refresh observed without retesting predicate.
    on wake: recheck/repark; generation change grants no mapping/storage rights
    revalidate dependencies/basis after actual readiness, recompute stale work
    foreign completion retains physical-record Message, never a destroyable Signal
    cancellation ends interest; physical work retires independently on its owner Lane

ON_CAPACITY_REFUSAL(kind):
    distinguish native Arena/Pool inventory from descriptor/catalog/proof PMR,
       Task slots, mapping/I/O readiness and caller effect/scratch capacity
    refill or wait on the ACTUAL owner's supplied predicate/Signal contract
    absent such a contract, report refusal; do not invent a PMR Signal
    waking a native Pool does not establish PMR/catalog/Task capacity
```

Concrete Boundlist/hint choices use current source/phase contracts:

| Stage | Qualified read/hint and returned boundary |
| --- | --- |
| Core lookup | `Borrowed.bind`/`Scope.bind` is synchronous today. ObjectId alone supplies no qualified bucket/history address. Do not hint a raw mutable catalog cell or claim a yield inside this call. Separate Orbital staging remains proposed. |
| Raw direct endpoint source | `prepare_lower_bound` may COMPLETE the search. Hint qualified endpoint-column spans BEFORE this search if using a Ring visit; do not call raw prepare and then describe its empty local window as hidden search latency. |
| Prepared numeric binding | Binding/sample setup is retained and charged. Current phase defers endpoint reads; hint its `local_window` (bracket/full array), retain phase and eligibility, yield, then `finish`. Correction can read outside the bracket; full-source permission remains required. |
| Width source | Available checkpoints route to the 16-width local window. Hint that window, retain exact version/source/eligibility, yield, finish. Checkpoint reads already happened during prepare and count. |
| Selected routing rank | Derive/hint actual child-name word and any needed minimum/high-edge or long-key-tail bytes from this selected node. Separate planes/gaps require distinct qualification. |
| Child Object | Resolve its stable ObjectId, then hint selected child's required endpoint columns. No former native child address is reused across versions. |
| Selected payload slot | Derive Part ordinal or resolve independent chunk; hint the required descriptor line(s). After qualifying actual material, derive/hint requested native Part spans, not an unconditional whole64 B enumeration result. |
| Extra list | Hint actual selected directory/checkpoint/length spans, then locator-list bytes after list location is known; retain admission once per list, not per ordinal. |

Use current `bind_demand`/`bind_dependency` and retained policy with actual
contiguous grants. Current irregular classes select a first-line hint even for
large declared sizes; they do not cover all search columns. Compare qualified
linear demands or bounded explicit fixed-line controls when needing several
lines, charging each actual hint. Never enlarge one span across gaps, assert
residency from prefetch, or allocate one job per native stage. Record source
kind, exact spans/lines hinted, searches done before yield and remaining
child/payload dependencies. Direct fused work remains the control. These
choices require consumer composition, not a new Loom API.

Scope cohorts deliberately retain snapshots. Borrowed current calls and fresh
Scopes have distinct lifetimes even when both satisfy a current-read request.
Compare matching request semantics and report actual enrollment costs. Serial
latency, independent throughput and true waits remain separate observations.

## Consequential paper counterexamples

- Cache a newer definition while an old schema/header stays live: reject its ID
  outside old next_id BEFORE using owned cache bytes; lifetime is not visibility.
- Two Objects share low64 but have different high64: their drafts remain distinct.
- An INTERNAL underflow builds routing/child-name Parts only, never leaf payloads.
- suffix14->slot2 and suffix15->slot0: SEEK14 then NEXT returns15/slot0.
- Aactual[1,2]/fence[0,10], Bactual[5,6]/fence[4,20], q5: an exact miss in A
  continues to B in the same region, including singleton and fast-seek paths.
- Fixed geometry/pointers with base7 and newly committed correction9 must use
  selected material, not return7 from the base-only native path.
- Collection corrections P/Q/P: compacting P preserves Q's pending contribution.
- Full/no-U R64->U56 takes a legal new reservation; removal edits private D first.
- C16 slots15/16 select chunk0/1 and rows15/0 with 1,024-byte extents; C8 differs.
- Append one definition at 100k historic records: capture one new record and one
  header, retain previous records, preserve both before/after schema views.
- Ready would-wait with no admitted Task returns a defined refusal/retry outcome;
  no stack Scope moves, Borrowed promotion or half-quantum stream replay occurs.
- Change only the seventh Part pointer, keeping all 64 dirent bytes, representation,
  bounds and block/rank equal. A fresh Scope sees the new extra pointer; old and
  late-discovering Scopes see the old one. The segment+extra block publish together.
- From three 64-row leaves, delete the first two down to one row each. A two-row
  non-root merge is illegal while the third leaf stays64. Normalization must extend
  the final run or contract the whole resulting root correctly, at internal depths too.
- Edit only descriptors/bounds in two legal leaves of 40 and 80 rows. Neither
  seeds normalization or incurs payload redistribution; their boundaries remain.
- Empty a region while retaining an old Scope, then recreate it. Allocate fresh
  child names from the surviving namespace counter; old traversal still selects
  the old graph, and the counter/tombstones/new edges publish coherently.
- Invalidate advice, then refuse/abort: extra dirtiness remains safe. Compact
  while an old Scope lives: never clear its generation's advice. A stalled writer
  must revalidate its basis/generation before a later install.
- Append a definition, convert a segment to it and change its seventh Part together.
  Before-schema constructs the source; AFTER-schema constructs the finished target.
  One Core packet publishes the complete after graph. Read both discovery orders,
  retain a pre-install reader, and refuse/stale the attempt without leaking the mix.
- Guide endpoints [10,30,40], q20, quantum4, with rank1 ineligible and rank2 live:
  the no-tie bracket's geometric rank1 must become global eligible rank2. Do not
  confuse a filtered window-local sentinel with the whole source's end.

## Bulk construction and auxiliary storage

```
BULK_DIRECTORY(sorted SegmentDeltas):
    consume supplied finished segments in layer/physical-prefix order
    form groups/leaf streams once; build legal occupied nodes near target fill
    pack payload chunks and exact bounds; build routing bottom-up with real Ikea
    capture finished Objects and publish through the same Orbital protocol
    bounded publication chunks may be used only with explicit visibility semantics
    # No giant duplicate-check packet is assumed cheap, or silently split atomically.

AUXILIARY_GET/PUT/ERASE(kind, opaque_key, opaque_value):
    select/construct the actual auxiliary store representation under its Scope
    use its real Ikea operation and Orbital publication, charging actual bytes
    do not place query-plan construction or unbounded telemetry work in directory lookup
```

A live same-Scope cursor is defined above. Persisting a cursor or resuming it
under a different snapshot additionally requires the caller's declared bookmark,
duplicate/relabel and durability semantics. Opaque cursor bytes are not proof
that local cuts survive reopen. Query-plan meaning, telemetry policy and record
placement remain with their owners; this spike can price their explicit storage
operations without inventing those semantics.

## Required cost ledger beside each prototype operation

The motivated immutable-graph control changes physical directory ownership,
not per-layer segment identity, query order or Orbital visibility:

```
GRAPH_READ(scope, query):
    selected_root = scope.bind(actual_directory_root_id)
    node_bytes = selected_root.part(routing_part).address
    # Its Control owns the complete current directory child graph.
    while internal(node_bytes):
        rank = actual_Ikea_bound_search(admitted_columns(node_bytes), query)
        node_bytes = admitted_raw_child_pointer(node_bytes, rank)
    consume exact chosen payload / continue candidates within this layer
    bind authoritative segment/correction state as required under SAME scope
    finish all raw byte uses before that scope/borrow ends

GRAPH_EDIT(basis, actual_deltas):
    select root and affected paths under basis; deduplicate paths by full identity
    build private changed leaves with real native/Ikea constructors
    capture each finished leaf using actual new/retained sources
    bottom-up, for each changed parent ONCE:
        construct immutable proof owning actual finished/unchanged child Owners
        clone/edit admitted routing columns and raw child byte pointers
        capture new parent with that proof as an interpretation dependency
        # Never retain predecessor root; unchanged children are direct owners.
    prepare/install new logical root plus actual required segment/schema changes
       in one Orbital packet
    release all private temporaries and account amortized old-root reclamation
    refusal releases only candidate graph; basis and visible graph remain intact
```

```
record:
    request/batch snapshot boundary and effective peer-derived ceiling
    selected directory nodes / payload chunks / extra blocks / segment shapes
    actual bytes read/copied/initialized and full-key comparison bytes
    changed directory Objects, supplied segment Objects, tombstones
    admitted Parts per capture, retained sources, physical owner/range operations
    backing acquisitions, descriptor/proof allocations, packet/refill work
    full coupled prepare/install and amortized retirement/failed work
    current, private, retained, reusable and mapped/resident byte inventories
```

The mechanism enumeration consumer uses all 64 dirent bytes. A projected
consumer uses only its requested descriptor fields and actual selected material
bytes; both have independently matched output and binding costs. Direct byte-pointer handoff needs the graph negative checks above.
Dropping required binding, old-reader retention or publication work is not an
assembly optimization.
