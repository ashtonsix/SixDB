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
    SAME stationary Orbital Scope for the declared snapshot operation
    index schema selection and reusable admitted definitions/read laws
    layer, query endpoints and endpoint policies
    cursor stack of selected node names/selections and local ranks
    optional selected payload/extra block for repeated consumption
    admitted layout: F32/C8 co-located five-Part leaf or independent-chunk control

Edit:
    original Scope/basis and offered SegmentDeltas
    actual Ikea u64->draft-pointer Hashtable, private to this attempt
    owned mutable drafts and new byte ranges; no published state is mutated
    offered finished segment Owners and eventual directory Owners
    current scoped root/namespace-counter drafts when topology changes
    candidate allocation/effect storage and publication workspace
```

An edit's draft table only deduplicates work. It is not an authoritative segment
roster, version table, reader registry or transaction log. Its scratch capacity,
initialization and touched-key cleanup are charged. A few deltas may use a fixed
array instead if that complete caller is faster; no `std::map` is on this path.

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

The following is the cheapest concrete append/delta composition retained for
the producer comparison. It specifies the directory boundary and snapshot
obligations; this spike does not implement another summary or delta codec.

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

COMPACT_MATERIAL_CANDIDATE(basis, partition):
    select one coherent base + visible committed deltas
    compute actual new base and summaries with the producer's laws
    construct new material excluding EXACTLY the incorporated delta prefix
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

## Query binding and routing

```
OPEN_READ(index, requested_snapshot):
    reuse an actual live Scope for this Core, or enroll a new Scope at its NOW cut
    # A numeric old cut does not let the current API reopen a historical snapshot.
    select/admit the index schema once for this operation
    retain those selections through every pointer use and returned work frontier
    return Read

FIND_EXACT_PREFIX(read, L, p):
    if L < 2:
        select the ordinary-table chunk derived from p
        test its population; return its row or absent
    if L <= J:
        g = p >> 4; suffix = p & 15
        group = read.Scope.bind(hash_group_name(index,L,g))
        if absent: return absent
        if singleton: compare its suffix and return its row
        native = the group's already-admitted concrete Ikea reader
        slot = native.find(suffix)              # one byte value, not a new owner
        if absent: return absent
        return matched group payload/bounds[slot]
    region = high J bytes of p                 # zero when J=0
    root = read.Scope.bind(region_root_name(index,L,region))
    return TREE_FIND(root, PHYSICAL_PREFIX(p))

TREE_FIND(node, probe):
    while node is internal:
        if probe is logical:
            r = Boundlist.lower/upper_bound(node.routing_maxima, probe)
            # lower for closed edge; upper for open edge
        else:
            r = physical-prefix child interval containing probe.prefix
        if no child: return absent
        save (node name, selection, r) in read.cursor stack
        node = read.Scope.bind(node.child_name[r])
        # Stable Object name, NOT the child's former native Part address.
    if singleton/small embedded node: search its admitted local source
    else:
        r = the corresponding Boundlist boundary in its exact leaf source
        check physical equality when the probe requests an exact prefix
        if absent: return absent
        slot = node.payload_slot[r]
        if co-located F32 recipe:
            payload = node.selection.part(1+slot/8)   # no additional Scope.bind
        else:
            chunk = read.Scope.bind(node.chunk_names[slot/8])
            payload = chunk.selection.part(0)
        return payload.dirent[slot%8], node.prefix[r], node.routing_bounds[r]

SEEK(read, L, q, closed):
    read.layer = L
    if L < 2:
        return bounded top-table logical seek using its occupied ordered view
    if L <= J:
        p = first L bytes of q
        e = FIND_EXACT_PREFIX(read,L,p)
        if e exists and e.maximum satisfies the requested lower edge:
            return e
        # A hash miss or an exhausted present prefix still owes a successor.
        position a same-layer group-presence cursor at p>>4
        mask = group.original_suffix_mask with all earlier suffixes cleared
        repeatedly choose next suffix by ctz(mask), then Ikea.find(suffix)->slot
        advance the presence cursor when mask is empty; check exact bounds
        return first qualifying Entry or end-of-layer
    region = first J bytes of q
    root = select that layer's region root directly, if present
    e = TREE_FIND(root, LOGICAL_LOWER_EDGE(q,closed))
    if e exists: return e
    position this layer's region-presence cursor strictly after region
    return first qualifying Entry in the next nonempty region, or end
```

For a generic K-byte key, a node source compares a numeric abbreviation first
and full bytes whenever abbreviations tie. No conversion of a logical key into
a supposedly unique fence is permitted. An absent natural region may require a
presence-tree walk; that work is part of seek, not an O(1) miss result. For a
conservative enclosing recipe, the returned Entry is a candidate; the real
segment consumer performs exact query refinement. No summary value is silently
substituted for a routing endpoint. Endpoint sentinels/inclusion obey the
selected recipe, with no invented unique key at a duplicate boundary.

```
START_RANGE(read, L, low, high, low_closed, high_closed):
    if query is empty: return end
    e = SEEK(read,L,low,low_closed)
    retain current-layer cursor and the exact high edge
    if e absent or e.minimum fails high edge: return end
    return e

NEXT(read):
    advance local occupied rank in the SAME layer
    if exhausted:
        ascend saved cursor frames to the first unvisited right child
        select that child under the SAME Scope
        descend its first occupied children
        if region/group exhausted, advance same-layer presence cursor
    if no entry, or entry.minimum fails high edge: return end
    reuse selected payload chunk while its chunk name is unchanged
    return full Entry

DESCEND(read, source_L, parent_prefix, parent_position, destination_L):
    require destination_L-source_L is 1 or 2, and destination_L<=4
    if one layer:
        p = (parent_prefix<<8) | (parent_position>>8)
        slots = [(parent_position&255)<<8, ((parent_position&255)+1)<<8)
    else:
        p = (parent_prefix<<16) | parent_position
        slots = [0,65536)
    e = FIND_EXACT_PREFIX(read,destination_L,p)
    return e and slots, or absent
    # A different layer is an explicit new lookup. There is no cross-layer NEXT.
```

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
    resolve exact child extrema under read.Scope when correction is required
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

```
PROJECT(read, entry, part_ordinal, requested_operation):
    return PROJECT_MANY(read,entry,[part_ordinal],requested_operation)

PROJECT_MANY(read, entry, ordinals, requested_operation):
    def = reuse admitted definition(entry.dirent.representation, read.layer)
    if the operation's runtime geometry is not available in the directory proof
       or the actual representation's admitted byte header:
        segment = read.Scope.bind(segment_name(index,read.layer,entry.prefix))
        derive/admit the needed shape ONCE; charge this selection
    if any requested ordinal>=6:
        extra = SELECT_EXTRA_LIST_ONCE(read,entry)
    for each requested part_ordinal:
        locator = entry.dirent.parts[part_ordinal] when part_ordinal<6
                  else EXTRA_LOCATOR(extra,part_ordinal-6)
        if resident raw BYTE locator:
            prove requested body+padding footprint under the selected format law
            invoke the real bounded Ikea/native consumer directly
        if mapped/remote resource:
            use actual resource-binding/readiness; retain read state on wait
    return consumed result, not an unprotected pointer escaping the Scope

SELECT_EXTRA_LIST_ONCE(read,e):
    if StreamPack recipe:
        block = reuse current block or Scope.bind(e.overflow_block)
        view = already-admitted StreamPack view of this selected block
        value = StreamPack.value(view,e.overflow_rank)
        return protected locator span and selected length
    if paged vector recipe:
        return selected root, length and reusable path/block cache

EXTRA_LOCATOR(extra,ordinal):
    check ordinal against selected list length
    if direct StreamPack span: return its eight-byte locator at ordinal
    otherwise descend ordinal's fixed-fanout path under the SAME Scope
       reusing selected path/block for subsequent ordinals
    return selected block's locator at local ordinal
```

The fixture must exercise both direct byte handoff and the additional geometry
bind. It may not price the former then silently implement the latter. If a
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
    if a mutable draft already exists: return it
    old = edit.basis.bind(object_name)
    make an uncaptured draft with retained-source references to old Parts
    put it in the private draft Hashtable; return it

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
    materialize final metadata/payload Parts once per changed object
    verify all new pointers name after-primary bytes in the SAME packet,
       or exact retained footprints/proofs still selected by its basis
    freeze only after all holder/bounds/topology work is complete

PLAN_EXTRAS_FOR_BATCH(edit):
    for each intent:
        if qualified COMPLETE extra association is unchanged and no deletion:
            skip extra-list reads; keep its current scoped association
        else:
            apply only offered touched ordinals / real count-layout change
            plan UPDATE_EXTRAS against the selected/local block or paged vector
    after ALL offered extra edits for a block:
        decide growth/packing once, including insertions/deletions in this batch
        derive final block/rank overrides for every affected local holder
        add metadata-only holder intents even when no segment Owner changed
    # Mapping/rank coordinates can change, but are never global directory ranks.
    # Ordinary inline updates need no unbounded extra-list comparison.

UPDATE_ENTRY(edit, before, after):
    route by exact physical prefix, not a record-row search
    choose small-group payload, co-located leaf Part1+slot/8, or independent chunk
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
    chunk_index = slot/8; row = slot%8
    if co-located F32 recipe:
        clone/edit leaf Part1+chunk_index; set private slot bit
        return leaf name, Part1+chunk_index, row
        # New leaf initializes all four fixed512 B payload Parts once.
    if leaf.chunk_names[chunk_index] is empty:
        allocate a fresh scoped name via ALLOCATE_NAMES(edit,1)
        create a 512 B chunk with all admitted bytes initialized ONCE
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

BUILD_BALANCED_OUTPUTS(outputs, old_leaves):
    if co-located F32 recipe:
        form final routing/meta Part plus four payload Parts for each output
        copy live rows once; initialize unused admitted bytes/padding
        retain an entire old Part only when its byte/slot interpretation stays valid
        capture the actual FIVE Parts per output; no payload-chunk Object tombstones
        publish output leaves, changed parents/counter and retired leaf names together
        return
    retain whole payload chunks only when their live rows all stay in one output
    distribute remaining live Entry rows into exclusive output chunks
    use real private copy/transfer; form each output chunk-name vector and local
       payload-slot mask/map; update every affected routing slot reference
    preserve 64-byte dirent stride and exact full-key bounds
    retire emptied old chunks; include all new/replaced/retired chunks in packet
    publish chunk ownership, leaf rows and parent changes together
    # Worst-case work includes every row in the affected pair, all captures,
    # tombstones and live old/new backing. No global payload-owner reverse map.
```

The F32 candidate captures one five-Part leaf for a locator replacement, copying
one 512 B payload Part and retaining the other four sources. The independent
large-leaf control captures one one-Part payload Object, with C8 copying512 B
or C16 copying1 KiB. Its read pays one extra catalog selection unless already selected.
The embedded small-node control avoids that selection but captures its actual
whole small format. The alternative multi-Part leaf must charge all of its
admitted source slots even when it copies only one bounded Part.

## Extra Parts and schema

```
UPDATE_EXTRAS(edit,before,after):
    if list unchanged: retain it; return
    if default local StreamPack recipe and list fits its current reservation:
        draft = PRIVATE_OBJECT(extra block)
        copy only Parts the real StreamPack replace can write
        bind a named view to these PRIVATE bytes
        pre-admit combined effects; replace; resolve actual issued spans
        # Block name and rank unchanged: other dirents need no write.
    else if default recipe and legal new reservation fits block policy:
        append new private reservation; retarget this Entry's block/rank
        retire old rank, if any
    else if local block exceeds assigned/dead-byte threshold:
        gather <=16 LIVE holder ranks; transfer to a disjoint finished block
        build complete local rank remap; edit just those holder dirents
        retire old block and publish all holder repairs together
    else:
        create/update paged extra-locator representation
        copy one fixed-size locator block; change ancestors only for topology
    if after has no extra list:
        clear the real overflow flag/reference; retire its old rank locally

APPEND_SCHEMA_DEFINITIONS(edit, offered_definitions):
    validate identifiers, layer admission and full format/Part laws
    append immutable definition chunks; retain existing historic chunks
    update schema's bounded directory/count, not every segment or dirent
    capture finished schema Owners but DO NOT install a separate schema packet
    edit.after_schema = select_private(basis, FINISHED schema changes, schema names)
    construct/validate new-representation segments against that after-schema view
    publish schema, segments, directory and extras in the ONE final Core packet
    old readers retain selected before-schema; cached laws are version-qualified

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
    finish ALL mutable destination writes, including holder/bounds repairs
    for each changed directory draft, once:
        make admitted immutable Control from the shared format proof and counts
        sources = certified source(old_selection,p) for unchanged Parts
                  + finished new bounded sources for changed Parts
        capture through Orbital; retain returned finished Owner
        # Capture seals body+padding NOW, before prepare/install.
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
    if capacity/refused:
        release candidates or retain finished Owners with their true dependencies
        refill/wait only outside acceptance; revalidate/recompute before retry
    if accepted:
        Orbital.install(prepared_workspace) once
        # One cut; no allocating Engine cache update or undo commit afterwards.
    release source certificates/drafts; bounded Orbital retirement at boundary
    charge retirement, failed attempts and retained capacities to full history

ABORT(edit):
    Orbital abort settles any accepted packet under its existing exclusion rules
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

## Proposed Orbital work

This section is coordinated with Orbital LEAD. It changes physical retention
and publication cost inside Orbital, not Engine's visibility semantics.

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

## Loom execution and genuine waits

```
READY:
    call the admitted directory/Ikea consumer directly
    no Task, Ring, execution Policy or callback dispatcher per lookup

KNOWN_INDEPENDENT_KEYS:
    reuse the canonical always-inline body in a bounded prefetch_for_each loop
    require a cheap qualified probe/hint body; known keys do not reveal addresses
    never perform a full bind/tree walk in prefetch and repeat it in consume
    use IndependentStream only where useful work/service needs its quantum
    no allocation/admission of one job per key

DEPENDENT_INDEPENDENT_REQUESTS:
    configure reused Ring storage outside the hot loop; charge its footprint
    stable frame owns real Scope/selection/input/output and continuation state
    discover a qualified next address; inline its admitted hint
    musttail yield_unchanged with frame pointer; fuse ready adjacent work first
    finish/retire directly when a result completes
    charge actual admission, refill, bootstrap, output and final drain
    # Ring does not expose/hide the internals of today's opaque Scope.bind.

ACTUAL_WAIT(frame, resource):
    observed = resource.owner_Lane_Signal.FULL_generation
    status = test the ACTUAL resource/readiness predicate
    if ready: continue; if failed: report real error and settle ownership
    record pending wait and observed generation in the stable frame
    retire/remove the native continuation at a returned owner boundary BEFORE
       ending or rebinding anything it borrows; detach alone removes no Ring slot
    keep Scope/frame/input/output through active AND overflow retirement
    end any Borrowed-only access before returning
    retain the real Scope and original dependencies in stationary Task
    hold no accepted Prepared across wait
    return Progress.wait(owner_Lane_Signal, observed)
    # Cleanup may pulse Signal: never refresh observed without rechecking predicate.
    on wake: recheck/repark; a generation change grants no storage/mapping rights
    keep Signal/WaitLink through unenrollment; foreign completion uses retained
       physical-record Message, never a pointer to a destroyable caller Signal
    after real readiness validate mapping/dependencies; finish or recompute stale work
    cancellation releases logical interest, not unretired physical work
    return catalog/destruction work to its assigned owner Lane
```

Scope reuse across separate requests means deliberately retained snapshots. A
fresh-read workload gets fresh Scope/transaction semantics on both sides.
Independent reciprocal throughput and a single dependent latency path remain
separate results.

## Consequential paper counterexamples

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

The measurement consumer must actually use the full dirent and requested Part
bytes. Direct byte-pointer handoff needs the graph negative checks above.
Dropping required binding, old-reader retention or publication work is not an
assembly optimization.
