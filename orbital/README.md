# Orbital

Orbital implements allocation, preparation and worker services. Versioned
Objects, Representations and Views are experimental in the
[MVCC study](../workbench/spikes/orbital-mvcc/README.md); the Stage 2 attempt was
withdrawn so measured performance, metadata ownership and interfaces can develop
together. Journal, Port and distributed execution remain in the
[combined Orbital/Loom design](../workbench/design/orbital/DESIGN.md).

Start with [Loom's caller path](../loom/README.md). A Loom Worker adopts an
application thread, owns a local allocation Pool and handles completion Messages.
Ready Ikea work uses ordinary functions over qualified bytes. Longer waits use
Tasks; independent memory dependencies can use a typed Ring.

## Allocation

[allocation.h](include/orbital/allocation.h) exposes these lifetimes:

- Arena owns the shared byte budget and allocator records.
- Pool owns reusable storage on one stable Lane.
- Allocation owns mutable storage; Hold captures an address and full byte footprint.

Loom selects a Pool for new allocation. Growth and capture follow the storage's
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
binding identity. Large allocations use reserved virtual windows. Both expose a
resolved ordinary pointer; native access needs no repeated trampoline resolution or scheduler
entry. Owner-local selection caches suggest candidates; geometry and actual page
state authorize each claim. Owning records stay at stable addresses.
Each Group embeds 256 four-byte identity records and shares release advice.
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
it does not prefault or initialize an uninitialized body. Ikea separately
qualifies format, row extent and native transfer shape. Bind once around stable
native work and rebuild address-qualified bindings after relocation or extent
change. Raw zeroing does not establish initialized encoded values.

Growth preserves already readable body and padding; it never initializes bytes.
`GrowthRequest` changes extent, reserve and alignment. Holds prevent relocation,
but permit in-place growth. The writer initializes new body or padding after any
overlapping reader finishes; a disjoint prefix reader can remain live. Physical
retention grants no permission to change contents.

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
HOT/WARM/COLD advice guides reclamation; timestamps reject stale advice and
omitted fields preserve earlier hints. Advice does not prove cache residency.
Explicit trim reclaims idle ranges on one Arena helper and returns budget after
owner completion. Dedicated page bitmaps keep admitted membership separate from
owner scratch; whole-run success permits exact refunds without pressure-time
container growth. The retained-byte floor and cold-first selection preserve
useful inventory; fully idle groups and windows can retire their metadata too.
Failed runs keep their complete membership, exclusion and charge until retry.
`open_arena` can place the helper on a CPU; its default inherits caller affinity.

The budget counts prepared payload, retained allocation metadata and unresolved
preparation bounds, including unresolved pages after a failed syscall. Reserved
virtual space, mapped bytes and RSS differ. Arena/Pool setup, controller containers
and allocator-library overhead are outside this limit; statistics report concrete
allocation-record estimates, not whole-process memory. Shutdown waits for actual
users, returns, reclamation and notifications. Closed Pool statistics remain
inspectable until `remove_pool` invalidates the handle; Loom removes it before
releasing the Arena. Legal shared-budget refusal enrolls its Pool for one future
refund notice. Observe capacity generation before the attempt and recheck after
waking; local changes also pulse it. Notifications grant no storage.

## Build and exercise

```sh
orb -m ubuntu cmake -S . -B build/orbital-stage1/clang -G Ninja -DCMAKE_BUILD_TYPE=Release
orb -m ubuntu cmake --build build/orbital-stage1/clang --target orbital_validate loom_validate
```

[The maintained consumer driver](../workbench/benchmarks/orbital/README.md) runs
SeriesPack/TuplePack point operations, scans, appends and dependent reads against
actual LMDB. It owns retained comparisons and the costs this allocation stage
omits. Its mutable page layout is comparison machinery; it supplies no Object
identity or versioning. [Prior research](../workbench/notebook/orbital/reference/README.md)
explains the allocation, access and execution choices without establishing runtime
contracts from prototype behavior. The opt-in MVCC study retains Object and
caller histories as checks for competing implementations.
