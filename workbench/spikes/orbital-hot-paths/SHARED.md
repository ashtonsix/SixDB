# Shared and growable memory across workers

What should happen when several workers use the same memory and some of it
grows? This note sets the single-class allocator against that question. It
draws on Ashton's [descriptor brief](../memory-descriptors/BRIEF.md), the
[descriptor findings](../memory-descriptors/FINDINGS.md) and
[virtual growth](../memory-descriptors/VIRTUAL.md), the [views findings](../orbital-views/FINDINGS.md),
the [TLB study](../orbital-views/tlb/README.md) and the
[Orbital/Loom design](../../design/orbital/DESIGN.md). These are options with
their known costs, not a selected protocol.

## What the trampolines were for

In the brief, a descriptor is a pointer. Single-choice (virtual) descriptors
point at their payload; multi-choice descriptors point at a one-byte trampoline
naming one of 256 slots, so an allocation can grow into a larger class *without
changing its descriptor*. That is the mechanism for small shared growable
memory: holders keep the descriptor, the owner moves the payload, and every
access resolves through the trampoline.

Production never used that resolution. Every caller holds the raw address, and
Holds refuse relocation, so the only remaining job of the binding plane was
moving a small allocation within its mixed Group during growth. Maintaining it
cost most of the old allocate/release time. The single-class overhaul removes
it, which also removes in-Group relocation: a small allocation now grows within
its class or moves.

The measured cost of resolving a descriptor, separately from maintaining it
([findings](../memory-descriptors/FINDINGS.md#native-access-measurements), Zen 5, 4,096 allocations):

| Access | Direct | Trampoline |
| --- | ---: | ---: |
| Independent 8-byte read | 0.59 ns | 1.99 ns |
| Dependent 16-byte chase | 5.96 ns | 9.83 ns |
| Same, with a busy metadata writer on another core (records layout) | — | 28.31 ns |

So resolution itself is cheap when the binding plane is read-mostly and
co-located with nothing written; it becomes expensive when other workers'
writes share its lines.

## The cases that need an answer

1. **Large shared growable memory** (column chunks, logs, hash tables, scratch
   arenas). Windows already reserve address space and grow in place, so every
   worker's pointer stays valid with zero indirection. Growth is an `mprotect`
   on the owner. Two costs remain: another worker can only read up to a size
   the owner has published, and every protection change interrupts the other
   workers in the address space (TLB study: 7.76 µs batched over 64 pages
   versus 355 µs page by page with three workers). Prepare growth ahead, in
   batches, on a service Lane.
2. **Small memory read by several workers while its owner may grow it.** A Hold
   keeps an address and its footprint; in-place growth stays legal and relocation
   is refused until readers finish. That is the answer today, at the cost of
   sometimes refusing growth.
3. **Small memory that must move while other workers keep a stable handle.**
   This is the case the trampolines served. It needs three things together:
   an indirection the readers resolve, a publication order between the owner's
   move and the readers' resolve (release store, acquire load), and retirement
   of the old Home only after every reader that might have resolved to it has
   finished. The trampoline byte supplied only the first.
4. **Several workers writing and growing the same object.** No raw-memory
   mechanism makes this safe on its own. The Object layer already has the
   shape: private Drafts, one publication, old readers keep old bytes. Physical
   growth belongs to whoever owns the Draft.

## Options for case 3

| Option | Reader cost | Owner cost | Retirement | Notes |
| --- | --- | --- | --- | --- |
| Hold and refuse relocation (today) | none beyond the Hold | growth may fail | existing physical-use counts | Simple; forces copy-to-new-Allocation plus a caller update when growth must happen. |
| One-byte trampolines in mixed Groups (old design) | ~1.4 ns per independent access | full claim/bind on every allocate/release | needs grants/generations; not implemented | Makes every allocation pay for the few that move. |
| Opt-in handle table: a Pool-owned array of 8-byte slots, one per relocatable allocation | one dependent load (≈ trampoline) | a store per move; nothing on ordinary allocations | epoch or Hold-based: old Home released after readers' epochs pass | General across classes, Groups and Windows. Slots can be grouped by owner to keep writes off readers' lines. |
| Promote to a Window on first growth | none | one copy, then in-place growth | existing | Wastes a page minimum; fine for things that really grow. |
| Version through the Object layer | Scope enrolment (3–7 ns) plus bind | publication | existing history reclamation | Right answer when growth is a logical change others must see atomically. |

My reading: the per-allocation tax is what must go, not indirection itself.
Readers who need a stable handle to movable small memory can pay one dependent
load through an opt-in handle; everyone else holds a direct pointer. The handle
table also covers moves the trampoline could not express (between Groups,
between classes and into Windows), and a later compaction pass can use the same
mechanism.

## What multi-worker access asks of the allocator now

- **Foreign release already works** and is now cheaper (32.8 ns on Zen 5,
  108 ns on Graviton versus 77.6 and 181 ns on main). It costs a mutex and
  one Message per Group on the owner, which is fine for occasional returns but
  not for a worker that frees everything another worker allocates. A per-Group
  atomic return list (one CAS per return, one Message per batch) is the usual
  next step.
- **Holds from other workers** go through owner-only `hold()`. Workers that take
  many short read references to the same memory should take one Hold per batch,
  or use Object Scopes, rather than one per access; each Hold is 17–37 ns.
- **Retirement for movable memory** needs a reader-tracking primitive that does
  not write shared lines on every access: per-worker epochs or reader slots, as
  the views study recommended for cut tracking (23.1 versus 3.60 ns per
  publication for shared versus per-producer progress).
- **Shared address space has a TLB cost.** Every `mprotect`, `madvise` or
  `munmap` on memory other workers touch interrupts them. Batch these (the
  overhaul's mapping runs do), keep trim off hot shared Windows, and prepare
  growth ahead.
- **UFFD write-protect COW** (case 4 for large Parts) also needs memory the
  workers can map twice, which means memfd/shmem backing for the Windows that
  may become Views. Private anonymous memory, as now, cannot be shared that way.

## Prototype: opt-in relocatable handles

[relocatable.h](relocatable.h) is built on the public allocation API, with no
allocator changes:

- **Table:** a fixed array of `{address, extent}` slots owned by one Pool's
  Lane. Only allocations that need a stable handle use it.
- **Owner:** moves copy-on-write. It fills a fresh Allocation, publishes it,
  and retires the old one tagged with the current epoch. `grow` copies, or
  grows in place within capacity.
- **Publication order:** the smaller extent is published first. Readers
  re-check the address after loading the extent, so every pair they observe
  lies within that address's payload.
- **Readers:** any worker resolves handles inside a `Section`, which announces
  its epoch on the reader's own line, followed by one `seq_cst` fence.
- **Collection:** `collect()` frees retired Allocations older than every active
  reader's epoch.

[relocatable_bench.cpp](relocatable_bench.cpp) uses 4,096 handles. Every read
checks its payload's header and fill against the handle, so a premature free
or a torn extent counts as an error. The owner replaces random payloads as
fast as it can, alternating 64 and 200 bytes so every replacement moves
between classes. Across 3 trials per host, 2 s each, on a dedicated core per
thread, there were zero errors. The bench also runs clean under
ThreadSanitizer locally. Medians, ns per access
([Zen 5](evidence/zen5-relocatable.csv), [Graviton](evidence/v2-relocatable.csv);
jobs `20261023T040318Z-c5245c7e`, `20261023T040318Z-d669ea2f`):

| Reader access (validated read) | Zen 5 | Graviton |
| --- | ---: | ---: |
| Direct pointer, no indirection | 1.58 | 1.65 |
| Handle, 64 resolves per Section | 2.05 | 2.75 |
| Handle, one resolve per Section | 6.04 | 11.6 |
| Handle, 64 per Section, owner moving ~10–15 M/s, 1 reader | 4.93 | 4.19 |
| Same, 3 readers | 5.31 | 4.19 |
| **Owner move** (allocate + fill + publish + retire + collect), 1 / 3 readers | 67 / 119 ns | 98 / 119 ns |

- **Resolution:** with a batch per Section, a handle adds 0.5 ns (Zen 5) to
  1.1 ns (Graviton) per access over a direct pointer. This is the trampoline's
  cost, paid only by allocations that opt in.
- **Sections:** each costs about 4 ns on Zen 5 and 9 ns on Graviton (the
  fence). Readers should resolve many handles per Section, as a Ring visit or
  a query batch naturally does.
- **Moves:** under extreme move rates, readers slow because moved payloads are
  cold, not because of the protocol. Retirement stayed bounded (peak backlog
  264–2,213 Allocations) and drained to zero once readers left.

Not covered yet:
- readers that stall inside a Section, which hold back retirement;
- a reader-count limit and how readers register with a table;
- sharing tables across Pools, and moves into Windows;
- integrating Holds with handles (a Hold is still owner-only).

## Next

1. Shared Window growth with readers on other cores: publish size, prepare
   ahead, and measure the readers' slowdown from the owner's `mprotect`.
2. A lock-free foreign-return list per Group, against today's mutex, with one
   and three releasing workers.
3. Reusable scratch: per-batch regions against Pool allocation for records
   that share a lifetime.
