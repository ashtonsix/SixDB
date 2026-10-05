# Allocation, retirement and memory use

Allocation needs several competing mechanisms: owner-local or shared bitmap
homes for independently growing reservations, dense storage for stable sizes,
and regions for shared lifetimes. Their useful unit need not be the same size.
The [extended comparison](EXTENSION.md) varies grouping and packing, measures
prepared allocation/free, and probes retirement failures. It leaves production
policy and virtual placement open.
The [allocation tuning](TUNING.md) measures occupied-set alternatives and improves
the checked descriptor path while exposing the cost moved into trim/report.

## Options and prior art

| Option | What it buys | Principal cost for SixDB |
| --- | --- | --- |
| General allocator with direct pointers | An existing allocation/free implementation and arbitrary sizes. | Shared relocation needs an additional binding; ordinary returned addresses do not implement the arithmetic grid. |
| Owner-local fixed-size slabs | Dense backing and simple reuse for stable-size allocations. | Movement/growth changes addresses or needs another binding; owner caches can strand memory. |
| Owner-local bitmap grids | Explicit legal destinations and local bookkeeping, with stable trampoline identities. | Class exhaustion, retained homes and page packing require policy. |
| Concurrent shared bitmaps | Any worker can allocate/free without routing through an owner. | Contended mutations and cache-line ownership transfers; an alternative to measure if owner routing dominates. |
| Lifetime regions | Bump allocation and bulk retirement, with subdivisions chosen by the container. | One escaping user can retain otherwise dead storage. |

Existing allocators supply mechanisms worth adapting, without establishing a
performance ranking for our workload. [mimalloc](https://microsoft.github.io/mimalloc/index.html)
separates local and concurrent free lists and shards them by allocator page;
[snmalloc](https://github.com/microsoft/snmalloc) batches remote deallocations back
to an owning allocator. [TCMalloc](https://google.github.io/tcmalloc/design.html)
uses local front-end caches, batched transfers and a page backend.
[jemalloc](https://jemalloc.net/jemalloc.3.html) exposes arenas, extent hooks and
decay/purge controls. These suggest local reuse plus bounded replenishment and
release, rather than a syscall for every small allocation or free.

A library can still serve ordinary runtime bookkeeping or supply a comparison
path. Reusing a library for backing is a separate decision: it must leave
Orbital control over mappings, protection and backing lifetime. Two independent
layers caching the same supposedly free memory would also need one budget.

## Units and authority

Keep three choices independent: the grids packed together, the allocator's
authority domain, and the domain retaining an operation's bytes. The sweep
compares 16, 64, 256 and 1,024 trampolines for packing and ownership, with home,
grid or wider protection. Every grid still has sixteen identities and sixteen
homes per class. Sixty-four dense trampoline bytes fit one cache line; 1,024
16-byte homes fit one 16 KiB page. Both are useful affinities, not universal units.

A local owner serializes allocator changes; a shared allocator admits them
directly. Consumers need not all execute on an allocation owner. Allocation and
Loom routing should favor dominant access and co-access where useful, without
preventing read fanout or migration. Larger packing can coexist with smaller
owners, but shared pages need coordinated release; disjoint cache lines do not
establish disjoint COW or mapping units.

An identity-availability mask and fifteen finite-class occupancy masks need
32 bytes per grid before retention, mapping and other metadata. Occupied means
current, retained or reserved for an unfinished move. Extra lifetime state
should follow actual users and retained homes; the probe's dense 256-home owner
registry is not the implementation to optimize around. Admission and acquisition
still need real accounting, even when their metadata lives outside the hot zone.

An occupied list is a useful sparse alternative to materializing free coordinates.
The finite-home bitmap already records occupancy in 30 bytes. A byte-coordinate
list reserved for sixteen occupied homes uses 17 bytes; one prepared for all 240
uses 241 bytes. The smaller list needs an overflow policy because current,
retained and pending homes can outnumber the sixteen current identities.
Lists can enumerate sparse homes cheaply, but allocation must still find a gap
in the requested class and arbitrary release must find its entry. Occupancy is
the authority; a small ready-free cache can independently accelerate allocation.

Keep a bounded set of allocation candidates per owner, indexed by available
identity and destination class. Prefer compatible affinity/lifetime, then reuse
existing backing where useful. Avoid scanning every grid on a hot allocation.
Do not open a fresh private pool for every query, shard or thread; empty groups
and budget can be reassigned. Ownership transfer must preserve one authority and
the route for outstanding completions.

Fixed-size reservations should retain a dense direct/slab candidate. An object
whose size and address really remain stable need not pay for independent growth.
Regions serve genuine shared lifetimes, including scratch and representation
subdivisions. Engine/Ikea can own the subdivisions inside one allocation.
An escaping result either retains its region or moves into separately retained
storage; a completed initiating task does not by itself permit reset.

## Small-slot packing

The explored grid-major A layout dedicates a small payload page to each grid.
Sixteen current 16-byte payloads use 256 bytes of that page; sixteen 96-byte
payloads use 1,536 bytes. Both still require one page. Smaller slots can reduce
cache traffic and initialized bytes, but this layout hides their backing benefit.

Interleave small-class homes across grids while preserving each grid's identities
and legal coordinates. A class-major address uses a class base, a grid contribution
and a slot contribution. LUT offsets need not follow ascending class order.
For example, across four grids the possible class runs occupy 1, 2, 3, 4 and
6 KiB. Ordering them as `{64,16,48,32,96}` fills a 16 KiB aperture while keeping
the homogeneous 64-byte run page-aligned. It need not add padding to every class.

The [geometry comparison](small-packing.py) checks five layouts and all 120
class orders. On 4 KiB pages, for 64 current reservations filling four grids:

| Slot class | Original payload pages | Reordered four-grid packing |
| --- | ---: | ---: |
| 16 B | 4 | 1 |
| 32 B | 4 | 1 |
| 48 B | 4 | 1 |
| 64 B | 4 | 1 |
| 96 B | 4 | 2 |

The reordered candidate keeps the same 16 KiB possible-home span. Its homogeneous
counts match class-page padding, which needs 24 KiB. This is geometry, not measured
RSS or speed; covered cache-line counts are unchanged in these snapshots.
The [retained counters](small-packing-results.json) include mixed and sparse cases.
One mixed snapshot favors ascending over reordered packing, three pages versus
four; whole-control-page interleaving worsens another from four to six pages.

Vary packing from one grid through 1,024 trampolines. Wider packing can use backing
more densely under sparse load. When pages cross authority boundaries, page
release, COW and projection couple those owners.
Cache-line affinity and page ownership are different units. Page services must
consider every overlapping current, retained and pending home; clearing one
grid's bitmap cannot authorize discarding a page shared with another.

Keep the current larger-class/private-range candidate and within-grid compaction
comparison. Interleaving small homes adds to that exploration. Cross-grid
compaction remains absent, and virtual placement remains open. Changed address
calculation and access cost must be measured before choosing the physical layout.

## Freeing and reuse

Distinguish ending usable descriptor aliases, completing an operation's byte use,
reusing a home, and returning backing. These need not happen together. A completion
releases the home or domain acquired earlier, never whatever the trampoline
currently resolves to. Cancellation alone releases nothing still being used.
Even without an active grant, the logical owner must authorize reuse: contents
needed by a historical view or future reconstruction must survive elsewhere.

Compare direct shared retirement with local authority receiving remote records
in batches. Size-only draining can strand a final partial batch indefinitely;
age and pressure service need an idle-owner wake path. A full queue must preserve
retirement information without waiting for memory those same records would release.
Establish protection before publishing work to a remote consumer: delayed
completion batching is safe, but delayed unprotected acquisition is not.

Do not store free-list or queue links in payload bytes still retained by readers,
hashing or I/O. Such intrusive reuse is legal only after their last user ends.
Identity and home reuse also remain distinct during delayed retirement.

## Backing and pressure

Local reuse should normally avoid OS work. Cache some prepared empty capacity,
with per-owner and shared caps; replenish and release in batches. Release cold
empty backing more aggressively under pressure, retaining address reservations
where their stability remains useful. Loom chooses policy; Orbital performs
mapping, reclamation and backing operations subject to recovery obligations.

Charge current capacity, retained homes, pending copies, queued retirements,
empty cached pages, metadata and shared-region slack. Keep physical residency,
virtual reservation and cache footprint as separate measures. Per-owner caps
alone do not bound the machine's combined cache.

Reserve legal destinations and completion resources before starting moves.
A spare physical page does not resolve a class with no legal free coordinate.
Do not wait holding resources whose release requires the same allocation to finish.
Defer new work or use an explicitly supported alternative; never overwrite a
retained home or silently change a shared descriptor's address promise.

The useful comparison is a complete history: same-core churn, producer/consumer
freeing, partial batches with idle owners, one long reader, sparse owners, mixed
sizes, regional escape and sustained pressure. Measure allocation/free cost,
useful access, queue delay, retained/peak bytes and return-to-OS behavior together.
Bare malloc/free results can inform the mechanism, but do not price SixDB's
additional retention and binding obligations.
