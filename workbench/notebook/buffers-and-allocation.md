# Buffers, allocation and object representations

SixDB's memory substrate connects logical state, physical representations and
ordinary native computation. These notes explore the primitives and their
composition; they leave room for different ownership, placement and execution
policies.

## The common problem

SixDB can choose stored information and the programs that use it together.
A signature can avoid fetching a cold body; a different layout can remove a
dependent load; a decoded window can save repeated work. Allocation affects
which choices are cheap. The useful unit of analysis is a representation and
the work needed to read, maintain and retain it under a workload.

| Module | Contribution |
| --- | --- |
| Engine | Combines data distributions, query patterns, static analysis and feedback to choose representations and programs. |
| Ikea | Supplies containers, local operations, subdivisions and traversal policies. |
| Loom | Supplies scheduling and allocation interfaces; chooses preparation, residency, placement and execution timing. |
| Orbital | Provides OS integration, backing, mappings, protection and I/O; owns integrity, availability and durable recovery. |
| Shore | Brings external formats, consumers and extensions, including their projection and access needs. |

These responsibilities meet during preparation. Engine and Ikea describe legal
work and its access requirements; Loom chooses where and when it runs; Orbital
supplies the backing and mappings. The result can be an ordinary native call
over borrowed storage. Observations from execution feed back into plans,
representations and placement without requiring analysis on every call.

The [layout investigation](../spikes/layout-analyser/design.md) develops joint
representation/program analysis; [Orbital's architecture](orbital/reference/ARCHITECTURE.md)
connects durable obligations to replaceable physical resources.

## Vocabulary and relationships

An object has one logical state at a chosen version or position, with potentially
several physical representations. A representation defines how to interpret and
operate on that state; an image is a particular realisation. Replicas can split
images into different parts and keep those parts in different locations.

| Term | Meaning |
| --- | --- |
| Logical reference | Names an application entity, such as a record. |
| Object/state | Stable identity and logical contents at a version or position. |
| Representation definition | Interpretation, geometry and operations for a physical form. |
| Representation image | A particular realisation of state under a definition. |
| Part | Byte coverage within an image, potentially held in several locations. |
| Backing | Storage or reconstruction material supplying bytes. |
| Memory reservation | Local address space and capacity. |
| View | Access to selected state through a representation, with rights and lifetime. |
| Memory descriptor | A local memory locator interpreted through its arena. |

These relationships form a topology: images span allocations, allocations contain
subdivisions, and views can share backing. Object, page, transaction and scheduling
boundaries can differ. Logical references name entities; a view binds chosen
state and representation; memory descriptors locate the prepared storage.

For example, a replica might keep per-record filter signatures in memory while
exact payloads live in cold storage. The signature can settle some questions;
others require further parts. Both concern the same state, but have different
information content, maintenance needs and access costs.

Retaining logical state, retaining an address binding and retaining bytes for an
active operation are distinct. A version may survive eviction; a send may outlive
the computation that produced its buffer. Cache warmth is another consideration.

## Memory descriptors

The [memory-descriptor brief](../spikes/memory-descriptors/BRIEF.md) starts a fresh
investigation. A pointer identifies its arena; that arena selects the reservation
palette and placement. A single-class descriptor addresses payload directly.
A multi-class descriptor addresses a one-byte trampoline selecting a payload
home from an arithmetic layout.

Capacity, initialized extent, traversal position and publication remain separate
facts. The enclosing representation and access domain must supply the usable
extent and protect a resolved address for its operation.
The [spike findings](../spikes/memory-descriptors/FINDINGS.md) compare placement,
native access and the lifetimes around rebinding.
The [grouping exploration](../spikes/memory-descriptors/EXTENSION.md) separates
payload packing, allocation authority and protection width, including 1,024
trampolines and 16 KiB page geometry.

## Stability, ownership and movement

The descriptor forms offer local choices within a broader question: what must
remain stable when storage changes?

| Arrangement | Opportunity | Cost to explore |
| --- | --- | --- |
| Direct descriptor, stable address | Fixed slots, construction and cheap traversal; page-owned reservations can grow within reserved space. | Address-space use and constraints on movement. |
| Stable trampoline, movable payload | Independent growth and relocation without changing every descriptor. | Dependent access and shared metadata traffic. |
| Collection image with clustered subdivisions | Ordered scans, compact planes and bulk rearrangement. | Compaction, rebinding and overlapping generations. |

A useful combination may be clustered images, direct access during retained
bindings, and trampolines at independently movable boundaries. Replacing an image can
preserve logical state and share unchanged backing. Moving backing beneath a
fixed virtual address, changing a payload locator, and rebuilding a representation
are different ways to adapt, with different effects on existing users.

Known unique means the sole usable descriptor; scoped payload borrows and logical
version retention remain separate obligations. A construction owner could
finalise structural metadata before publishing shared descriptors, with RAII
handling abandoned construction. A shared binding preserves copied descriptors,
but a borrowed payload address still names the old storage. CPU and I/O borrows
therefore constrain mutation and reuse.

Useful operation distinctions include reserving versus populating storage,
duplicating a descriptor versus cloning contents, growing capacity versus changing
used extent, and releasing ownership versus reclaiming bytes. The address promise
may last for a call, a view or a long-lived owner; that choice shapes the API.

## Layout, subdivisions and access

Independent reservations can use equal-sized slots from an arena palette. Collections may
prefer a larger allocation whose variable subdivisions are chosen by Engine
and Ikea. This lets the container organise neighbouring bytes around its access
patterns. Point access can favour references beside data; scans can favour dense
planes and physical ordering. Gaps, local permutations and occasional compaction
provide intermediate choices without making each subdivision an independent
allocation.

Clustering can follow co-access, common lifetime, shared permissions or writer
placement. These objectives sometimes disagree: separating exposure classes can
save projection copies while spreading a point read across pages. Packing hot
and cold lifetimes together can leave mostly dead pages unreclaimable. Engine
can weigh these effects alongside query and maintenance cost.

Allocation capacity and an operation's access footprint also differ. A Bec256
body fits in 47 bytes, while one native path uses a 64-byte readable window.
An 8-byte tail plus 56-byte payload fits the body, but does not by itself
grant a 64-byte window across mutable metadata. A containing cluster can
supply overlapping readable windows while exact writers touch only their own
bodies. The [Bec256 bounds](../../ikea/docs/bec/bec256.md#memory-access-bounds)
illustrate how composition can unlock a different execution path.

The [trie-remapping findings](../spikes/trie-remapping/FINDINGS.md) provide another
useful comparison: stable slot labels do not eliminate movement when payloads
remain packed by occupied rank. Lookup, traversal and maintenance need to be
considered together.

## Selection and projected views

A representation owner can describe how a selection becomes accessible bytes.
Fixed layouts may produce ranges directly; variable layouts may first need
lengths, directories or dictionaries. The result can be a short access recipe
with stages that Loom prepares. In the signature example, a predicate first
produces candidate rows, then their locators identify cold payloads to retrieve.
Selection coordinates belong to the relevant representation or mapping.

Shore can request a projection that maps whole permitted pages and copies only
pages needing masks. The representation determines how to hide excluded contents
while preserving usable structure. Some encodings may need a newly constructed
projection. Keeping source positions can preserve existing access paths;
compacting selected rows can make the consumer's traversal cheaper.

Repeated exposure patterns may justify retaining prepared projections and
sharing unchanged backing. This makes projection cost another input to layout
analysis. Rapid view switching, outstanding users and asynchronous I/O need a
common lifetime account, with Orbital implementing the protection and mapping
mechanisms. The [physical brief](orbital/reference/PHYSICAL.md) develops that boundary.

## Scheduling and resource hints

Loom wants the time and resources needed to make a particular access usable.
A cache line, a stream and a dependent pointer chase need different preparation.
For cold material, the cost can include discovery, queueing, transfer, verification,
decoding and mapping. For resident material, placement and sharing matter.

Hints can describe likely cache residence, access shape, reuse, priority and
safe stopping points. Ready work can run directly, short dependent accesses can
be interleaved, and long waits can retain a continuation. The access description
helps Loom choose among these without turning every kernel stage into a task.
The [memory measurements](../spikes/memory-characterisation/FINDINGS.md) show why
costs need context: peer-cached data can be slower than a matched cold control.

Scheduling and allocation can also shape IPC: move work toward data, transfer a
borrow, copy a small result, or batch ownership transfers. Rings, decoded caches,
query outputs and reconstruction scratch can share accounting while using
different policies. Admitted work needs room to finish even when other work is
holding inputs and waiting for outputs.

## Ordinary memory and durable mutation

The xmem pattern remains a useful starting point:

`open → ordinary memory → repeated staging → commit → close`

An object-facing open can establish context or join an existing transaction,
then prepare a representation and view. Most callers need not manufacture a
transaction identity or commit position. Byte-oriented Orbital access lets
consumers retain native representations.

Ikea's local work distinguishes logical destinations, physical stores and
maintenance dependencies. Re-encoding can change many bytes without changing
logical values; changing one value can affect summaries elsewhere. A growing
value may require a new payload, an updated locator and repaired summaries to
become visible together. The owner composes these effects into the transaction.
[Ikea integration](../../ikea/docs/integration.md) contains the concrete mutation
and lifetime account.

Staging records effects; sealing makes bytes stable for checks or I/O. Commit
resolves the transaction; publication exposes the result. These boundaries can
support overlap without requiring a runtime call for every load or store.
Closing a view releases the caller's use; outstanding sends or writes may retain
its bytes. Physical patches belong to a particular representation, so replicas
with different layouts may replay logical operations or convert results instead.

## One file and bootstrap

The storage direction is one database file with a 256 KiB bootstrap region for
metadata and index roots, loaded unconditionally on initialisation. 128 or
512 KiB may prove better tunings. Roots can reach further metadata elsewhere
in the file; remote copies and content-addressed storage remain location options.

Recovery needs access to roots before the ordinary transactional allocator is
available: the bootstrap region provides the starting point for discovering
the rest of the database. File offsets, durable identity and virtual addresses
have different roles. Hole punching returns backing once retained state and
active users no longer need it. Dense packing and effective reclamation may
favour different geometries.

## Useful comparisons

A few compositions can expose the important choices:

| Situation | What it helps distinguish |
| --- | --- |
| Grow a shared allocation with independently positioned views and outstanding borrows | Descriptor stability, local progress and storage lifetime. |
| Replace variable-length bodies or compact a collection during scans | Locality gains versus movement, rebinding and maintenance. |
| Project selected rows from hot signatures and cold payloads | Information needed, acquisition order and exposure cost. |
| Cancel retrieval under pressure or reopen after interrupted publication | Which resources and obligations survive each operation. |

Primitives can acquire implementation fidelity sooner than these compositions.
The comparisons should help choose which stability, ownership and preparation
promises are worth their cost while keeping the wider architecture steerable.
