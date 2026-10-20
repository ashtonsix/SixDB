# Directory implementation pseudocode

This is the paper candidate for the current review, not a description of a
finished implementation. It uses existing Ikea, Orbital and Loom mechanisms.
The cost limits in [Budget](BUDGET.md) constrain the complete operations below;
an unmeasured helper is not free. Earlier measured controls and producer codecs
remain in [Findings](FINDINGS.md) and their captured sources.

## Inputs and retained state

The supplied five index layers and prefix-defined Segments are unchanged.
Directory storage nodes are a different tree. Its fanout, occupancy, Part size
and balancing policy do not assign record keys or split Segments.

~~~
Config:
    distinct admitted directory, Segment and schema namespaces: u64
    logical key width K; shared physical/logical prefix width J
    require J <= min(6,K)
    native directory format/locator recipe; actual external metadata resource

SegmentID(layer,prefix):
    full ObjectId(Segment_namespace, (u64(layer)<<32) | prefix)
    require layer in 0..4 and prefix in [0,256^layer)
    # L4's UINT32_MAX is valid; end is a separate state.

Directory names within directory_namespace:
    root(layer)               = (1<<60) | layer
    native_hash(layer,group)  = (2<<60) | (u64(layer)<<32) | group
    extras(layer,group)       = (3<<60) | (u64(layer)<<32) | group
    dynamic_node(sequence)    = (4<<60) | sequence
    namespace_counter         = 0
    require sequence < 2^60; never reuse names, including after empty/recreation

Schema names within schema_namespace:
    index_header = 0
    definition(rep_id:u32) = u64(rep_id)+1
    # Externally supplied IDs; no hidden population-sized roster or next-ID claim.

SegmentDelta:
    layer, physical prefix
    expected BEFORE Object/version/realization, or expected absence at original basis
    finished AFTER Segment Owner, or deletion
    full K-byte AFTER logical minimum and maximum
    # These are Segment facts. Record writes and summary corrections are producer work.

Edit:
    caller's original stationary Scope and SAME Store/Core
    offered Segment Owners and independent immutable interpretation dependencies
    request-local final intents, selected paths, affected Part/group plans
    inline full-ID scratch for small attempts; actual Ikea map for larger attempts
    private mutable buffers; finished candidate Owners; actual output changes

Read:
    caller's stationary Scope, or expressly uninterrupted Borrowed operation
    selected index/representation definitions and admitted operation/read laws
    layer, endpoint policies, optional original-prefix-coordinate mask
    saved traversal frames, current leaf/rank and selected hash/extra block
~~~

The short name is unique only inside its admitted namespace. Scratch comparing
arbitrary Objects compares both identity halves; an index-local map may omit a
constant high half only after admitting that namespace. Numeric cut equality
between different Cores is insufficient. No map here is lookup authority,
history, a reader registry or an undo log.

### Request-local scratch geometry

The unfinished Map64 fallback is not the intended implementation: after eight
inline entries it constructs table<64,0,8>, whose single primary bucket has only
eight rows. A 64-distinct-key batch puts56 entries in Swiss even with ideal input.
It has no sizing, primary growth, reservation or selected execution policy.

~~~
SCRATCH_FOR_ATTEMPT(expected_distinct,admitted_key_domain,resource):
    retain small inline lookup while actual distinct count<=8
    lazily choose actual Ikea bucket/encoding geometry when promotion is needed
    use this map's expected DISTINCT work, not all historical directory/schema size
    declare target primary occupancy; inspect actual capacity/spill/assigned bytes
    reserve overflow separately when the admitted workload needs it
    migrate inline values once into the selected private owner
    reuse concrete native dispatch through this bounded attempt
    avoid repeated get+set searches when one admitted modify_or_insert can
       produce the actual new vector index without unsafe updater side effects
    if actual work outgrows geometry: explicitly grow/rebuild and rebind
    charge construction, migration, allocation/refusal and final cleanup
~~~

Plan keys require35 bits for layer plus prefix; definition IDs require32.
Node-name maps retain full admitted u64 identity unless a collision-free shorter
encoding is explicitly established. Do not use a universal bucket count or
truncate a name to make a measured profile applicable.

Let N be offered commands, D their distinct Segment names and A offered
definitions. Before balancing, plan/leaf/extra maps have at most D entries;
definition appends have at most A. Historical-definition scratch covers all
definitions encountered while validating EVERY offer, including cancelled
commands and existing definitions extended by A; it is not bounded by final D
alone. Structural sibling/ancestor scratch needs its actual repair bound.
There is no general64-entry cap. Repeated-key B64 can remain entirely inline.

No future-key prefetch or Ring is assumed for dependent plan construction.
Independent requests can use such mechanisms only with qualified keys, stable
owner/probe lifetimes and measured complete construction/cleanup. A mostly-Swiss
fallback remains a possible labeled control, not the accidental default.

Borrowed state cannot cross publication, polling, reentry, suspension or return.
A Scope, its Worker/Lane and external resource remain alive through every use.
Escaping Parts use Orbital's real export/retention contract. Owning a directory
root alone does not retain independently versioned Segment material.

## Query binding, candidate traversal and continuation

~~~
OPEN_READ(store,scope,config):
    validate that store.bound(scope,schema.index_header) belongs to THIS Core
       distinguish absent from wrong-Core/conflict; reject absent for a read
    admit selected header's K,J,namespaces and native format
    retain/reuse only definition proofs qualified for this selected schema
    return Read borrowing the SAME stationary scope

DEFINITION(read,rep_id,layer):
    reuse a definition already qualified within THIS read
    otherwise select schema.definition(rep_id) through read's token
    reject absence, unsupported descriptor/locator law, or disallowed layer
    admit immutable descriptor and its real dependencies once; cache for this read
    # An owning newer cache entry does not prove old-snapshot visibility or allows.

EXACT_PREFIX(read,layer,prefix,consume):
    validate full layer/prefix domain
    if layer <= 1:
        select ordinary root once
        if absent or original-prefix population bit clear: return miss
        obtain payload slot by original prefix; bounds rank by population rank
        consume qualified entry, not a row
    else if layer <= J:
        group=prefix>>4; suffix=prefix&15
        select actual native_hash(layer,group) once
        if absent: return miss
        borrow its actual immutable Ikea table<4,0,64> owner
        table.find(suffix, temporary64 -> consume entry INSIDE this callback)
    else:
        descend this layer's ordered tree by physical-prefix columns
        at leaf require exact prefix match; gaps are misses
        consume its dirent using the leaf's slot map
    # No ordering or successor search crosses index layers.

SEEK_LOGICAL(read,layer,low,high,low_closed,high_closed,mask):
    validate K-byte endpoints, mask extent and arithmetic before reading mask words
    if range is empty/reversed: return ended
    if mask selects no eligible original prefixes: return ended
    ordered_start = lower_bound(maxima,low) if low_closed
                    upper_bound(maxima,low) otherwise
    if no low: ordered_start=first
    for K>8:
        search the ordered first-eight-byte abbreviation
        refine its equal-abbreviation bracket against FULL remaining key bytes
        # Open edges cannot discard equal abbreviations with larger full keys.
    descend same-layer bounds using that rule, saving frames and next child ranks
    top ordinary tables use their compact ordered bounds directly
    hash layers use a metadata-only ordered presence tree:
        its leaf rows refer to original prefixes, then actual V64 hash values
        the hash's physical visit order is never used for ordered continuation
    stop at the first candidate leaf/rank; defer payload/hash selection until needed

SEEK_PHYSICAL_PREFIX(read,layer,low,high,mask):
    validate endpoints in a widened type and intersect with [0,256^layer)
    reversed range is empty; high is inclusive, including UINT32_MAX at L4
    descend prefix columns; save the SAME kind of continuation frames
    never compute last_prefix+1 in u32

NEXT_CANDIDATE(cursor,consume):
    while a candidate remains:
        r=next ORIGINAL routing rank; advance continuation state
        p=original physical prefix at r
        if physical endpoint exceeded: end
        if logical upper edge exceeded by candidate minimum: end
        if mask excludes p: continue
        if logical lower/upper overlap fails exactly: continue
        select its payload Part/hash group once if not already selected
        consume full identity, bounds and borrowed dirent within this token
        return found
    climb saved frames until a next sibling exists; descend its first possible child
    end when no frame has a successor; do not seek again from the layer root

VISIT(cursor,maximum,consume):
    one bounded native traversal loop calling the same continuation body
    count returned Segment entries, not records
    reuse current leaf/hash/extra selections across returned entries
    ready scalar next is the maximum=1 convenience, not the inner scan loop

DESCEND_INDEX_LAYER(read,target_layer,physical_prefix):
    explicitly invoke EXACT_PREFIX at target_layer with its proper prefix width
    price its actual lookup and material work; no implicit cross-layer successor
~~~

Minima and maxima are each nondecreasing. Adjacent occupied bounds may meet at
equality: preceding maximum <= following minimum, not strict inequality.
A logical equality can therefore return many Segments. Stable enclosing fences
are a separate admitted routing recipe: they must be containing and monotone;
exact misses continue within the candidate run. Bounds change costs and extra
candidate visits are charged to whichever recipe is tested.

A mask uses original prefix coordinates, never filtered ranks or payload slots.
Validate its readable complete word tail, base, bit count and widened end.
Preserve those coordinates on every seek and crossing. Query and mask bytes
outlive the cursor; cursor movement must not retain pointers into its former
inline frame.

## Directory to Part handoff

~~~
MATERIAL_CONTEXT(read,entry,operation):
    def=DEFINITION(read,entry.rep_id,entry.layer)
    if def's already-admitted direct law proves:
         directory BYTE pointers were derived from the actual coherent primary,
         all required geometry/read interpretation is independently available,
         unchanged pointers remain backed by the selected coherent primary,
         and THIS operation needs no authoritative correction/frontier selection:
        return qualified direct context borrowing SAME live Core/token
    otherwise:
        select authoritative Segment/collection through SAME token ONCE
        qualify its geometry, committed/applied extent, correction/read law
        compare actual required locator association under selected schema
        return context reused across all requested ordinals
    # Geometry, pointer equality and an unchanged dirent do not prove pristine data.

PROJECT_MANY(read,entry,ordinals,operation):
    execute inside the entry callback when hash bytes are temporary
    context=MATERIAL_CONTEXT(read,entry,operation)
    validate ordinals against that selected/admitted geometry
    if any ordinal>=6:
        select extras(layer,prefix>>4) once; bind actual StreamPack once
        value=StreamPack.value(original suffix rank=prefix&15)
        validate list length/interpretation once; reuse across ordinals
    for ordinal:
        take inline locator or native-u64 list locator at ordinal-6
        obtain full admitted body+padding/format from qualified context
        invoke actual bounded native/Ikea consumer
    return requested material/output before token ends

EXPORT_PART(read,entry,ordinal):
    obtain actual Orbital BoundSelection from SAME Core/scope
    export via Orbital's supported selected/whole retention policy
    do not return an unprotected pointer merely because a dirent copy is owned
~~~

The direct path names byte addresses, not an old Control, View address slot or
Representation header. A recipe with changed geometry or visible corrections
uses its authoritative context. No per-Part Hold is acquired in an ordinary
already-protected native loop. Unsupported remote/resource locator recipes
are explicit admission refusals in the first native implementation, not invented
synchronous pointer resolvers.

## Private editing

~~~
ON_PRODUCER_CHANGE(change):
    if its actual protocol proves membership, routing, representation,
       ALL locators and directory-owned interpretation unchanged:
        emit NO directory delta; do not route/clone/publish merely to confirm it
        complete actual data/summary/correction work through Orbital as required
    otherwise emit only actual possible Segment-fact changes

BUILD_DIRECTORY_CHANGES(store,basis,deltas,definition_appends):
    admit SAME Core, original index header and configuration
    privately validate/plan supplied definition appends/extensions
    for EVERY offered delta, including one cancelled by a later delta:
        validate layer/prefix/K-byte bounds and full identity
        reselect actual before binding at original basis, once per distinct Segment
        require supplied before object/version/realization or absence to match
        admit before definition under BEFORE schema
        if after exists:
            require a FINISHED native Segment Owner with correct resource/identity
            admit its definition under AFTER schema, including new private definitions
            derive actual inline pointers, full extra list and interpretation
            require minimum<=maximum and compatible shared physical/logical prefix
        save last final intent for this Segment
    coalesce final intents; sort by layer/physical prefix
    route each affected OLD leaf/group once; retain the request-local route
    compare actual before entry and ALL before extras against selected primary
    # Old inline equality alone is not a complete association check.

    plan final extras BEFORE deciding descriptor no-ops
    classify each affected leaf/group as:
        descriptor-only, bounds-only, or membership/structural
    if final facts all equal:
        omit its directory work; do not clone to confirm equality
    apply all final ordinary/hash/ordered intents privately
    normalize only actual structural violations, bottom-up
    propagate only changed child routing extrema/topology
    construct each final changed Object once
    return finished directory/schema/extra changes + original read basis
    # No prepare/install, fresh snapshot, Segment-row editing or Engine MVCC here.

PRIVATE_PART(draft,ordinal):
    reuse already-private destination for that actual Part
    otherwise acquire disjoint writable Loom backing with admitted alignment/padding
    copy needed old bytes once; initialize every other admitted readable byte
    record fresh source; unchanged Parts remain certified source(old_selection,p)
    never write sealed bytes, including same-value/preserving stores

ALLOCATE_NAMES(edit,n):
    select/private-draft namespace_counter once
    reserve checked monotone sequence range, no wrap/reuse
    capture its final 64-byte image once if actually advanced
    counter survives empty directory roots
~~~

Before qualification is tied to the original batch basis. Input validation is
not sequentially repeated publication, and cancellation does not excuse invalid
intermediate commands. Full row ordering and truthful Segment extrema are
producer obligations checked by the independent row oracle; the directory
cannot establish them by rescanning all records.

~~~
EDIT_ORDINARY(layer,final_intents):
    select old root once; derive final population
    if empty: emit real root deletion if previously present; return
    descriptor-only:
        clone only touched admitted payload Part once
        retain unchanged bounds Part
    membership/bounds:
        form final bitmap/count, original-prefix payload slots
        merge final compact prefix/bounds columns once
        clone actual payload Part only when its bytes change
    capture one finished ordinary Object with its exact sources
    # L1 whole-Part control copies 16,448 bytes, not 64.
    # Smaller Part partitioning is a separately priced layout if that fails.

EDIT_HASH_GROUP(layer,group,final_intents):
    compute final size BEFORE cloning; empty -> delete old hash Object
    if values/membership change:
        clone actual Ikea V64 table owner ONCE with real native Storage
        apply final erase/upsert/modify calls to that private owner
        enumerate actual table.allocations; freeze its stable owner/header proof
    otherwise retain the selected actual table/physical primary
    build/retain original-suffix bounds plane as its actual changes require
    capture actual buffers/proof once; no second payload map or suffix-to-slot map
    bounds/membership changes also edit metadata-only ordered presence leaf
    descriptor-only replacements do not change that tree

EDIT_ORDERED_LEAF(final_intents):
    if every intent is descriptor-only:
        for each touched C-slot payload Part:
            copy 64*C bytes ONCE; patch all final 64-byte values at old slots
        retain metadata and other payload sources; capture one final leaf
        return                    # no node-row decoding or ancestor drafts needed
    otherwise:
        merge old ordered rows with final intents once
        retain endpoint references/dirent bytes through original Scope or inputs
        validate neighbours, including across old leaf boundaries
        save final row stream; do not capture or allocate overfull payload slots
        preserve slots and clone only dirty chunks when count/topology stays legal
        seed balancing only for actual overflow/underflow, not mere bounds dirtiness
~~~

## Ordered balancing and payload compaction

Two concrete paper candidates share the algorithm:

| Candidate | Capacity F | Non-root minimum | Normalized fill | C | Locator Part |
| --- | ---: | ---: | ---: | ---: | ---: |
| Narrow | 32 | 8 | 13..26 | 16 | 1,024 B |
| Wide | 128 | 32 | 53..106 | 32 | 2,048 B |

Children use stable full Object names, so moving a child without changing its
routing endpoints/topology does not rewrite parents. All leaves have the same
storage height. The index's five layers do not bound directory-tree height.

~~~
NORMALIZE_LEVEL(edit,height):
    lazy old ordered frontier at this height plus private final row/child streams
       saved paths locate adjacent nodes; do not enumerate the whole layer
    seed runs with nodes whose FINAL count violates [minimum,F]
    join overlapping/adjacent seeded runs before materializing anything
    for each run:
        total=sum(final counts)
        if total==0:
            remove all run nodes and their parent rows; continue
        if this is the WHOLE frontier and total<=F:
            plan these rows directly in fixed root name at this node height
            retire obsolete frontier/ancestor names; preserve lower legal children
            stop higher-level normalization on those retired ancestors
        choose output_count r satisfying:
            ceil(total/target_high) <= r <= floor(total/target_low)
        if no r:
            extend run with an actual adjacent node, even across a parent boundary
            obtain its final stream; union any run touched; repeat
        else:
            distribute near evenly into r nodes within [target_low,target_high]
            record partitions/counts only, without names, buffers or capture
    if a run extension touches an earlier planned run:
        union their original spans, invalidate those partitions and replan
    repeat until all runs at this height have final disjoint feasible partitions
    for each FINAL run:
        reuse old non-root names left-to-right; allocate extra names once
        retire surplus old names
        replace child rows in EVERY affected parent, not only one parent
        record final outputs, without capture yet
    merge resulting parent-row intents once
    propagate changed routing extrema in otherwise legal parents
    repeat at next height only for its actual structural violations
    root internal with one child: copy/adopt contents into fixed root name
       retire old child; repeat contraction
    empty root: delete it
    root overflow: form legal non-root outputs and a higher root at SAME root name
       allocate distinct child names; root never becomes its own child
    require every final non-root node legal before freezing

BUILD_FINAL_LEAF(node,rows):
    if no structural change:
        preserve original slot map; update metadata only if bounds/membership changed
        copy each actually dirty C-slot payload Part once
    otherwise:
        assign live rows to slots 0..count-1; build slot mask and ordered slot map
        copy each live 64-byte dirent once into final bounded Parts
        initialize unused admitted bytes; retain a whole old Part only when its
           complete byte/slot interpretation remains valid
    form ordered physical prefixes, endpoint abbreviations and exact tails
    record final sources and immutable geometry; capture later

BUILD_FINAL_INTERNAL(node,children):
    one metadata Part containing exact prefixes, monotone endpoint columns,
       child names, count, height and admitted reserved bytes
    derive endpoints from CHILD ROUTING endpoints, not a different extrema law
    no leaf payload Parts or record summaries
~~~

Do not redistribute two healthy nodes merely because both are dirty. A batch
leaving two one-entry nodes cannot stop with an illegal two-entry non-root
merge: extend the run or contract the whole applicable root. Repairs crossing
parents must update all affected parent rows before the next height.

Scalar potential for F32 is high occupancy above 26 plus non-root deficiency
below 13. A 33->16/17 split releases 7; underflow at 7 releases at least 6.
Each parent membership event can add one credit. Conservatively,
5*repairs <= initial_potential + original_membership_events - final_potential.
F128 with thresholds 53/106 gives denominator 21. Initial fill potential,
internal events, root conversion, batch repair size and final disposal count.
375/5=75 ns and 1500/21=71.4 ns are target arithmetic, not measurements.
The scalar argument is not a bound on arbitrary bulk deletion latency or
historical retained bytes.

## Extra Parts and schema

The first extra-list candidate assigns exactly sixteen StreamPack ranks to the
original suffixes in a stable sixteen-prefix group. An absent holder is an empty
list at its rank, not an empty Segment. This is a replacement-image algorithm;
it does not pretend an immortal rank supports retirement.

~~~
PLAN_EXTRAS(edit,final_intents):
    for each touched original sixteen-prefix group:
        select old extras Object once, if any; bind actual StreamPack once
        take its sixteen protected list views
        override only final changed/deleted/born holders at rank=prefix&15
        compare actual complete lists; unchanged -> no extra Object change
        if all sixteen final lists empty:
            delete old extra Object if present; continue
        M=max(final_list_bytes); B=sum(final_list_bytes) or explicit reserved bound
        require every length multiple of 8 and admitted by real StreamPack layout
        choose actual Sizes/full-U=R/immortal layout with independent M and B
        obtain actual payload/directory/length/control extents from that layout
        admit all four windows, padding and effect capacity before construction
        initialize private destination and encode the sixteen FINAL lists once
        resolve issued coverage while the named view exists
        capture final four-Part extras Object once with its actual format proof
        # block coordinate=prefix>>4; rank=prefix&15; neither changes on packing.
    include Segment/extra changes in the SAME final packet
    no holder discovery, global rank repair, append-then-pack or tombstone emulation

DEFINITION_APPEND_OR_EXTEND(edit,offers):
    validate every offered id, immutable interpretation, locator law and layer set
    merge repeated offers for same id only when immutable meanings agree
    existing definition may extend allowed layers; it is never reinterpreted
    select existing record through original basis; reject a conflicting meaning
    capture ONE final direct-named record for each actual addition/extension
    preserve all historical definitions for retained readers
    index header changes only for actual index-context initialization/change
    # No D+1 header publication is hidden if IDs are externally supplied.

MIGRATE_OR_PORT(edit,Segment,target_rep):
    producer constructs equivalent FINISHED material using actual target codec
    admit target through AFTER schema, source through BEFORE schema
    derive all actual pointer/bounds/extra changes; use ordinary batch algorithm
    caller publishes material+directory+schema atomically
    # Separate single-Object alternative adoption cannot silently repair other Objects.
~~~

For the chosen native candidate, a u16 StreamPack value limit permits at most
8,191 extra u64 locators (8,197 Parts total). This is an explicit admission limit,
not a general index constraint. A larger recipe needs its own ordinal lookup,
copy/capture and publication algorithm before admission; no truncation.
Small-list budgets apply to the stated bytes/holders, not to the largest legal
one-MiB group rewrite. Direct fixed-length replacement and incremental mutable
packing remain research alternatives in [Design](DESIGN.md#extra-locators).

## Capture, publication and failure

~~~
FREEZE(edit):
    all final topology, bounds, extras and definition work is complete
    for each actual changed Object once:
        retained Parts = source(old_selection,p), with proofs alive through capture
        new Parts = actual admitted private native Allocations/slices
        initialize full readable body/padding and finish all effects
        capture through Orbital using matching physical ownership mode
        on refusal:
            fresh Allocations of THAT failed capture remain unconsumed
            release earlier finished Owners and remaining private buffers once
            preserve basis/proofs through disposal callbacks; return actual refusal
        retain finished Owner; its footprint is now sealed
    produce actual additions/replacements/tombstones; no duplicate full ObjectId
    do not publish uncaptured drafts or retain predecessor Representations as proof

CALLER_PUBLISH(basis,Segment_changes,directory_changes):
    keep actual Segment custody and SAME original basis alive
    join all actual material/directory/schema changes into one preflighted vector
    if vector empty:
        return empty proposal at READ basis, not a new write/current-state receipt
    stationary Prepared uses SAME Core; capacity follows ACTUAL number of changes
    Orbital.prepare(basis,changes,packet)
    if stale:
        release candidates or retain them only for explicit dependency revalidation
        opening a new Scope alone does not validate old afterimages
        return stale/recompute; no reverse replay
    if capacity/unavailable and packet still empty:
        keep original basis/proofs/finished Owners for an admitted retry
        otherwise release and return refusal; no busy progress-free retry loop
    if accepted:
        immediately install ONCE, or abort under Orbital's existing exclusion law
        no allocation, Engine visibility update or second commit follows install
    clear caller-held change Owners/scratch inside the measured interval
    run/pay bounded maintenance and account eventual complete physical settlement

ABORT(edit,packet):
    abort an accepted packet through Orbital; empty refused packet needs no abort
    release caller-held finished Owners and private buffers
    discard scratch; published bytes are unchanged
    charge actual recursive disposal/physical returns, not an invented fixed undo cost
~~~

Only one accepted Orbital packet exists at a time. A stationary packet pins its
Core; prepare requires the current global cut and charges real history/capacity
work. Up to 64 changes use pairwise full-ID duplicate checks; larger packets use
the actual uniqueness scratch. A 64-command directory batch can exceed 64
publication changes. There is no 64/128 cap on general balancing/schema output,
and an atomic batch is never silently split to fit that crossover.

Native fresh Allocations transfer once per capture owner law. Retained sources
certify their real body+padding and acquire physical uses without retaining
ancestor Control/Representation chains. Shared slabs are explicit counted
owners, not repeated consuming transfers. Pool capacity, retained backing and
RSS are separately reported. A bounded history drain can trigger unbounded
last-owner graph disposal; price that actual work.

## Loom execution, admission and genuine waits

~~~
EXECUTE_READY(prepared_operation,inputs):
    borrow one coherent context for this complete bounded native operation
    inline ready decode/search/filter/output; no per-row Task or Hold
    use direct iteration first
    use reused Ring only for actual independent qualified memory dependencies
       with setup, refill, result association and final drain charged

ON_REAL_WOULD_WAIT(retained_operation):
    end Borrowed use before returning
    retain stationary Scope, physical/interpretation sources, query/mask bytes,
       output capacity and complete continuation in a stable admitted Task frame
    request actual Loom/Orbital service with real completion resources
    on resume validate mapping/generation; rebind after any relocation
    if no admitted retained path: return explicit refusal/retry
~~~

No new Engine enrollment, Task/Ring or publication protocol is assumed. The
directory may return a defined not-ready outcome when a future locator recipe
needs I/O. Remote transport/durability are not imputed to resident native tests.

## Bulk construction and auxiliary storage

~~~
BULK_DIRECTORY(sorted_finished_Segments):
    validate/coalesce actual final Segment facts and definitions
    form ordinary/hash groups and sorted leaf streams once
    partition non-root levels into legal near-target nodes; root gets exception
    build levels bottom-up using the SAME format and capture law
    caller publishes at the declared visibility boundary
    if that boundary is atomic: price the complete real packet, not hidden chunks

AUXILIARY_GET_PUT_ERASE(kind,key,value):
    choose actual admitted store representation and native Ikea operation
    select/build/capture through same read/publication mechanisms
    charge actual key/value bytes; no query compilation or unbounded telemetry
       computation is hidden in a small metadata operation
~~~

Persistent cursor bookmarks, plans and telemetry have their own semantic owners.
A live same-Scope cursor above is not automatically a durable bookmark or a
promise that an Orbital local cut survives reopen.

## Consequential paper counterexamples

- Equality runs crossing leaves and hash groups: return all candidate Segments,
  then actual rows; no unique fence or internal duplicate order.
- Equal first-eight-byte abbreviations with unequal long tails, both endpoint
  policies: refine before skipping or stopping.
- Original suffixes 1,14,15 with only 14,15 selected: rank/slot still derives from
  the original representation, not the filtered mask.
- Seventh Part pointer changes with all 64 dirent bytes unchanged: publish and
  select the changed extra Object, preserving late old-reader association.
- Before binding from a different Core with the same numeric cut: reject.
- Invalid command followed by cancellation: still reject; no-op is not admission.
- Two namespaces share low64; maximum L4 prefix; name exhaustion; empty/recreate.
- A fixed pointer/geometry with a changed committed correction extent: select
  authoritative material, not stale base bytes.
- Descriptor/bounds-only edits in healthy neighbours: no structural redistribution.
- Bulk deletion leaves several deficient siblings across parent boundaries:
  extend the final run/update all parents; no illegal non-root leftover.
- Capture/prepare refusal, accepted abort, stale original basis, workspace reuse,
  exported Part after Store reset, and old reader's first discovery after trim.
- A historical definition is cached before a newer layer-admission extension:
  old read still uses the old selected admission, not newer cache visibility.
- Root contraction/growth changes native Kind/Part geometry: finish actual new
  sources at the fixed root name; never publish a self-child or recycle an old name.

## Required cost ledger beside each future measurement

For every public operation record:

~~~
matched peer, snapshot/transaction boundary, output, effective complete ceiling
directory nodes/Parts/hash owners/extra blocks/definition/material selections
full-key/tail comparisons and false candidates
bytes copied/initialized/read; admitted Parts and retained physical source uses
backing acquisitions, proof/descriptor/PMR allocations and history changes
actual material/directory/schema/tombstone packet size and uniqueness crossover
one complete prepare/install; retries/refusals/aborts and local cleanup
current/private/retained/reusable byte inventory, mapped/RSS and final settlement
owner/helper/process CPU; completed useful throughput; separately sampled tails
~~~

No source inspection or instruction model can price all of those runtime costs.
The prototype decision remains conditional on the explicit feasibility review
in [Budget](BUDGET.md#paper-feasibility-and-remaining-uncertainty).
