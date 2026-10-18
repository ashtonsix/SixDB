# Orbital

Orbital owns storage and Object lifetimes beneath the database. Its current
services are allocation and reclamation, physical Representations and Parts,
an embedded single-owner Object catalog, OS threads and completion queues,
and Linux platform reports. Journal, Port and distributed execution remain in
the [combined design](../workbench/design/orbital/DESIGN.md).

Callers supply the meaning of the bytes: formats, container layouts, keys,
navigation, cloning and conversion algorithms. Orbital retains their physical
owners and interpretation dependencies, and publishes the material they supply.
For example, a hashtable owner decides which planes to allocate and which bytes
remain useful; Orbital provides allocation, retention and reclamation. That
owner's storage adapter stays with the container or its caller.

[Loom](../loom/README.md) supplies scheduling and Pool selection around these
services; Orbital can also be called directly on an adopted Lane. The
[ObjectStore caller](../workbench/spikes/orbital-mvcc/callers/object_module.md)
shows their composition through suspension and escaping Part lifetime. This and
the [MVCC study](../workbench/spikes/orbital-mvcc/README.md) are integration
consumers, not definitions of Engine's data structures.

## Reported platform

[platform.h](include/orbital/platform.h) exposes a cold Linux inventory of allowed
CPUs, distinct identities and advertised features, cache geometry and physical
memberships, SMT/package/NUMA relationships, and page options. Unreadable or
malformed fields remain unknown. Allowed affinity stays separate from reported
sharing membership; advertised capabilities and page options confer no execution
or allocation guarantee. Discovery changes no affinity or global policy.
[Loom](../loom/docs/hardware.md) owns conditioned experiments and policy derived
from these reports. The [platform check](test/platform.cpp) covers malformed and
limited topology, heterogeneous CPUs and actual Linux reporting.

## Allocation

[allocation.h](include/orbital/allocation.h) exposes these lifetimes:

- Arena owns the shared byte budget and allocator records.
- Pool owns reusable storage on one stable Lane.
- Allocation owns mutable storage; Hold captures an address and full byte footprint.

The caller selects a Pool for new allocation. Growth and capture follow the storage's
existing owner and run on its assigned thread. Release can run elsewhere: one
reserved return Message per packing group or virtual window brings final returns
back to the Lane. Actual users keep storage alive after Allocation release.
A Group keeps one active Arena pin while any identity is live or awaiting its
owner return; each live Window keeps its own pin. Additional Holds update the
identity's checked physical-use count and keep its address and captured footprint
alive. A Hold supplies no immutability, selected version or access permission.

Small allocations use finite Groups, each with a fixed palette of size classes.
The one-byte binding selects a home whose address, capacity and page range derive
from Group geometry. Quiescent growth can change the home without changing its
binding identity. A large allocation uses a Window: private contiguous virtual
storage with a writable prefix and optional extra address space for growth.
`size` is the caller's body, `capacity` is accessible bytes, and `reserved_bytes`
is retained address space; neither capacity nor reservation measures resident RAM.
Both backends expose an ordinary pointer; native access needs no repeated
trampoline resolution or scheduler entry. Owner-local selection caches suggest candidates; geometry and actual page
state authorize each claim. Owning records stay at stable addresses.
Each Group embeds 256 four-byte identity records and shares release advice.
The finite cutoff is 3 KiB. Native 4 KiB pages retain the original A/B palettes;
16 KiB pages adjust two upper B classes, and 64 KiB pages use B's 256-byte minimum
for fresh Groups. The [palette study](../workbench/spikes/allocation-palettes/README.md)
owns the measured trade-offs. Reuse may select another Group's palette, so equal
requests need not have equal capacities. Natural alignment follows reserved capacity
(`gcd(capacity, 64)`); explicit alignment is checked separately.
Bindings, placement grids, page state and trim bitmaps share one stable heap
block, sized to its contents without base-page rounding. Preparation allocates
and initializes this control block before the owner exposes the Group. Payload
reservation and protection still use virtual memory. Heap refusal reports
`capacity`; failed payload unmapping retains the Group's charge until cleanup succeeds.
The owning Group or Window's Domain immediately precedes its identity records;
the immutable slot identity recovers that return route. Pool-owned Group and
Window inventories grow with demand; their movable ownership handles preserve
the records' addresses through queued work and physical retention.

The private allocation submodule keeps each backend's transactions together:

- [allocation.cpp](src/allocation/allocation.cpp): physical lifetimes, growth and Pool-wide trim/close.
- [arena.cpp](src/allocation/arena.cpp): shared budget, notifications and the reclamation worker.
- [group.cpp](src/allocation/group.cpp), [window.cpp](src/allocation/window.cpp): allocation, preparation and reclamation for each backend.
- [finite.cpp](src/allocation/finite.cpp), [finite.h](src/allocation/finite.h): Group geometry, bindings and backing-page state.

Ordinary allocation confirms writable mapping and initializes readable padding;
it does not prefault or initialize an uninitialized body. The caller separately
qualifies format, logical extent and native transfer shape. Bind once around stable
native work and rebuild address-qualified bindings after relocation or extent
change. Raw zeroing does not establish initialized encoded values.

Growth preserves already readable body and padding; it never initializes bytes.
`GrowthRequest` changes extent, reserve and alignment. Holds prevent relocation,
but permit in-place growth. The writer initializes new body or padding after any
overlapping reader finishes; a disjoint prefix reader can remain live. Physical
retention grants no permission to change contents.

`relinquish_tail` contracts the writer's body/padding without moving bytes or
running VM work; the same extent can mark inherited surplus reclaimable. Existing
Holds retain their full captured footprints. Finite homes remain packed; a live
Window can return whole tail pages while retaining VA. Growth into dispatched
reclamation conflicts until owner completion; growth inside its retained prefix
remains available.

## Preparation, reuse and settlement

[Background preparation](include/orbital/preparation.h) uses the same Pool and
geometry. A caller-selected Lane maps and touches the accepted extent, then can
run a trusted format constructor over exclusive body bytes and initialized
padding. The original owner publishes an ordinary Allocation; its caller adopts
any private format metadata and effects. Keep the request, constructor context
and service route stable through physical settlement. Touching pages alone
establishes neither encoded values nor continued cache residence.

Failed finite cleanup keeps the request and charge in `repair_wait`;
`retry_cleanup` schedules one explicit attempt. Failed or cancelled windows
return to charged Pool inventory for trim or close. Cancellation ends demand
without refunding unfinished physical work.

Reuse takes the first compatible prepared Window; finite Groups keep their
ready-candidate cache and search prepared homes before unprepared homes.
An exact reservation-size index skips undersized Windows while preserving their
return order. Its nodes are admitted before a fresh mapping; returns allocate none.
HOT/WARM/COLD advice guides reclamation; timestamps reject stale advice and
omitted fields preserve earlier hints. Advice does not prove cache residency.
Explicit trim reclaims unused Group pages, wholly idle Windows and relinquished
tails on one Arena helper, returning budget after confirmed owner completion.
The first real Window Hold admits a small charged array of reader endpoint
counters. Native-page bins conservatively round endpoints up to powers of two;
retiring a wide Hold permits later trim even while prefix Holds continue.
Revisit maintenance after reader retirement; it does not trigger trim itself.
The [live-tail study](../workbench/spikes/allocation-live-tail/README.md) owns the
costs and comparisons. Dedicated page bitmaps keep admitted membership separate from
owner scratch; whole-run success permits exact refunds without pressure-time
container growth. The retained-byte floor and cold-first selection preserve
useful inventory; fully idle groups and windows can retire their metadata too.
Failed runs keep their complete membership, exclusion and charge until retry.
`open_arena` can place the helper on a CPU; its default inherits caller affinity.

The budget counts prepared payload, retained allocation metadata and unresolved
preparation bounds, including unresolved pages after a failed syscall. Reserved
virtual space, mapped bytes and RSS differ. Arena/Pool setup, controller containers
(including inventory vectors and the reservation-size index)
and allocator-library overhead are outside this limit; statistics report concrete
allocation-record estimates, not whole-process memory. Shutdown waits for actual
users, returns, reclamation and notifications. Closed Pool statistics remain
inspectable until `remove_pool` invalidates the handle; Loom removes it before
releasing the Arena. Legal shared-budget refusal enrolls its Pool for one future
refund notice. Observe capacity generation before the attempt and recheck after
waking; local changes also pulse it. Notifications grant no storage.

## Embedded Objects

[object.h](include/orbital/object.h) provides `ObjectStore<Physical>`. The material
parameter supplies interpretation and conversion compatibility.
[representation.h](include/orbital/representation.h) owns immutable physical
Representations, Part borrows, capture/rollback and escaping Part lifetime.
Orbital also owns opaque Object identity, completed logical versions,
coherent discovery, publication and selection of an equivalent Representation.
It does not inspect records or decide clone, container or navigation policy.

Start with the [material guide](docs/material.md), then its small
[caller](test/extension.cpp) and [material TU](test/extension_material.cpp).
The [Stage 2 handover](../workbench/spikes/orbital-mvcc/OUTCOMES.md) maps the
implementation, evidence and lessons for the next real Ikea/Engine caller.

A stationary `Scope` enrolls at one completed local cut and protects late
discovery, including absence. `Prepared` is stationary caller-owned storage:
preparation reserves a finished batch; installation publishes every change at
one cut without allocation or owner disposal. Aborting uninstalled work retains
exclusion until all private owners and entry pins settle. Local cuts are neither
`SerialPosition` nor `Epoch`, and this embedded protocol does not imply durable
admission or consensus completion.

Only one packet can be accepted at a time, and preparation requires the current
global cut even when intervening publications changed other Objects. Finished
material can wait in an `Owner` before acceptance without excluding publication.
Keep its original basis or independently validate its dependencies after waiting:
opening a fresh Scope does not make a result computed from older inputs valid.
An accepted packet is a short publication reservation; holding it across I/O
would prevent other publication.

`conversion` seals one selected Object/version and its immutable source. `adopt`
can replace that version's physical alternative without changing logical time;
the converter owes exact logical equivalence. `bound` seals a primary selection;
`bound_alternative` seals and retains the exact alternative selected now. Either
can export a Part after later adoption or publication while the issuing Scope
lives; the escaping Part then retains its own physical and interpretation
lifetime. A `Borrowed` selection instead requires one uninterrupted owner
operation, with no publication, reentry, suspension or escape.

The metadata resource is an ordinary `std::pmr::memory_resource` and must outlive
all derived lifetimes. Every `Prepared` packet constructed from a live Store
pins its Core, including empty, refused, installed and aborted packets. Resource
allocation/deallocation callbacks must not reenter this catalog; the supported physical-owner disposal callbacks are a different
boundary. Allocation failure refuses preparation; diagnostics and
refusal injection can remain in the caller's resource. Layout comparisons and
metadata costs stay in the study.

Representations embed the caller's static Control without per-Part extent
metadata or read callbacks. Control supplies immutable admitted geometry and
interpretation lifetime; Orbital retains it through physical capture and escape.
A selected-copy escape asks that same Control to copy the needed interpretation
into the Part's allocation. Format validation, value operations and conversion
algorithms remain with the caller.

Capture reserves metadata and validates sources before consuming any fresh
Allocation; refusal preserves those Allocations. Metadata exhaustion can precede
an invalid-source error. Capture can also retain backing supplied by certified
sources from Parts or protected Selections, alongside fresh Allocations. The
[material guide](docs/material.md#compose-retained-and-new-bytes) owns the address,
interpretation and write-authority obligations.

The implementation keeps these compilation seams:

- [storage.cpp](src/object/storage.cpp) compiles directory, history-slab and debt-storage allocation once in Orbital. [representation.cpp](src/object/representation.cpp) compiles non-format physical-owner operations.
- [instantiate/representation.h](include/orbital/instantiate/representation.h) compiles capture, release, selection and Part escape once per admitted Control.
- [instantiate/object.h](include/orbital/instantiate/object.h) compiles publication, conversion authority and maintenance once for each concrete material's explicit-instantiation TU. Actual material conversion runs in the caller.
- Scope enrollment, ordinary selection and private storage layout remain visible for short reads. A deliberately coupled `object_detail::Access<Physical>` specialization lets one native composition fuse catalog selection with its own interpretation/kernel; Orbital names no driver type or runtime callback.

The opt-in instantiation headers use Orbital's public include path and library.
Concrete material definitions and instantiations remain with their caller.

All catalog calls and material-owner destruction run on the same owner. Physical
Allocation's foreign final-return support does not make Object operations concurrent.

## Build and exercise

```sh
orb -m ubuntu cmake -S . -B build/orbital-stage1/clang -G Ninja -DCMAKE_BUILD_TYPE=Release
orb -m ubuntu cmake --build build/orbital-stage1/clang --target orbital_validate loom_validate
```

`orbital_validate` includes format-independent logical Object histories and
physical Representation checks. The Object material instantiates cold catalog
operations in a separate TU; its caller checks coherent discovery, publication
refusal and abort, conversion authority, and retained lifetimes. These Object/Representation
checks require no spike or Ikea format. The opt-in study also runs its interrupted
maintenance histories against the selected catalog, alongside composed
TuplePack/SeriesPack and Loom integration checks and performance comparisons.

[The maintained consumer driver](../workbench/benchmarks/orbital/README.md) runs
SeriesPack/TuplePack point operations, scans, appends and dependent reads against
actual LMDB. It owns the allocation-only comparisons; the
[MVCC comparison](../workbench/spikes/orbital-mvcc/performance/README.md) includes
the selected Object path. The allocation driver's mutable page layout is
comparison machinery; it supplies no Object identity or versioning.
[Prior research](../workbench/notebook/orbital/reference/README.md)
explains the allocation, access and execution choices without establishing runtime
contracts from prototype behavior. The opt-in MVCC study retains Object and
caller histories as checks for competing implementations.
