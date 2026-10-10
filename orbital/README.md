# Orbital

Orbital implements allocation, preparation, worker services and an embedded
single-owner Object catalog. The [MVCC study](../workbench/spikes/orbital-mvcc/README.md)
retains the rejected first implementation, competing layouts and the disposable
TuplePack/SeriesPack driver used to develop and measure the selected catalog.
Journal, Port and distributed execution remain in the
[combined Orbital/Loom design](../workbench/design/orbital/DESIGN.md).

Start with [Loom's caller path](../loom/README.md). A Loom Worker adopts an
application thread, owns a local allocation Pool and handles completion Messages.
Ready Ikea work uses ordinary functions over qualified bytes. Longer waits use
Tasks; independent memory dependencies can use a typed Ring.
The [ObjectStore caller](../workbench/spikes/orbital-mvcc/callers/object_module.md)
exercises coherent reads across suspension, publication and catalog teardown,
then keeps an escaping Part alive through foreign use and owner-Lane completion.

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

## Embedded Objects

[object.h](include/orbital/object.h) provides `ObjectStore<Physical>`. The material
parameter supplies interpretation and conversion compatibility.
[representation.h](include/orbital/representation.h) owns immutable physical
Representations, Part borrows, capture/rollback and escaping Part lifetime.
Orbital also owns opaque Object identity, completed logical versions,
coherent discovery, publication and selection of an equivalent Representation.
It does not inspect records or decide clone, container or navigation policy.

A stationary `Scope` enrolls at one completed local cut and protects late
discovery, including absence. `Prepared` is stationary caller-owned storage:
preparation reserves a finished batch; installation publishes every change at
one cut without allocation or owner disposal. Aborting uninstalled work retains
exclusion until all private owners and entry pins settle. Local cuts are neither
`SerialPosition` nor `Epoch`, and this embedded protocol does not imply durable
admission or consensus completion.

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
pins its Core, including empty, refused, installed and aborted packets. Resource allocation/deallocation callbacks must not reenter
this catalog; the supported physical-owner disposal callbacks are a different
boundary. Allocation failure refuses preparation; diagnostics and
refusal injection can remain in the caller's resource. The selected layout keeps
the current version in its Object directory entry and uses 16-byte historical
nodes with separately charged 8-byte physical-pointer directory slots and
distinct active-cut retention. Competing layouts stay in the study.

Representations embed the caller's static Control without per-Part extent
metadata or read callbacks. The driver Control contains a Shape pointer and
record count: admitted geometry derives byte extents. Shape construction,
count rules, TuplePack/SeriesPack laws and cloning remain with the caller.
Orbital holds the proof and its dependencies through physical capture and
escape. A selected-copy escape asks that same Control to copy only the needed
interpretation into the Part's allocation.

Capture reserves metadata before validating the sources, then transfers their
existing Allocation uses without a retain/release round trip. No source is
consumed on refusal; metadata exhaustion can precede an invalid-source error.
Geometry queries observe immutable proof. Existing Holds keep their own uses,
and the transferred use preserves the allocation's pin and final return route.

The implementation keeps these compilation seams:

- [storage.cpp](src/object/storage.cpp) compiles directory, history-slab and debt-storage allocation once in Orbital. [representation.cpp](src/object/representation.cpp) compiles non-format physical-owner operations.
- [representation_impl.h](src/object/representation_impl.h) compiles capture, release, selection and Part escape once per admitted Control. The fixture retains only its interpretation operations and measurements in [shape.cpp](../workbench/spikes/orbital-mvcc/metadata/orbital/shape/shape.cpp).
- [store_impl.h](src/object/store_impl.h) and [history_impl.h](src/object/history_impl.h) compile publication, conversion and maintenance once for each concrete material's explicit-instantiation TU. The driver example is [catalog.cpp](../workbench/spikes/orbital-mvcc/metadata/composed/catalog.cpp).
- Scope enrollment, ordinary selection and private storage layout remain visible for short reads. A deliberately coupled `object_detail::Access<Physical>` specialization lets one native composition fuse catalog selection with its own interpretation/kernel; Orbital names no driver type or runtime callback.

All catalog calls and material-owner destruction run on the same owner. Physical
Allocation's foreign final-return support does not make Object operations concurrent.
The driver keeps whole-resource accounting, structural counters and the matched
LMDB comparator; extraction alone is no claim of performance parity.

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
