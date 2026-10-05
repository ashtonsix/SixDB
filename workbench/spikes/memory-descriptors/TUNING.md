# Occupied sets and the allocation path

The checked finite descriptor path is substantially cheaper after tuning. Keep
bitmap occupancy for mutation; occupied lists are useful sparse enumeration
candidates. Moving page-liveness accounting out of allocation/free improves warm
histories, while aggressive backing reclamation remains a separate tradeoff.

## Native results

On a pinned Zen5 core (EPYC 9R45, Clang 21.1.8, native 4 KiB pages), reordered
class packing with 1,024 trampolines gives these six-sample medians. Times include
the named payload work; the two fixtures have different bodies.

| Workload | Frozen, ns/pair | Tuned, ns/pair |
| --- | ---: | ---: |
| Selected grid, 64 B, touch8/free | 16.74 | 9.56 |
| Matched endpoint touches, 64 live | 20.86 | 12.66 |
| Same, counters every batch | 30.01 | 16.53 |
| Same, trim every eight batches | 24.95 | 18.21 |
| 1,024 live, replace 10%, counters every batch | 37.45 | 23.47 |

Selected-grid improvement spans every measured geometry: roughly 37–48% across
four layouts, four widths and two granules. Owner metadata for one wide group
falls from 12,600 to 7,344 bytes; report/trim needs 2,624 transient bytes.

Several aggressively trimmed histories regress: the single-live 64 B case is
about 28% slower, dominated by discard/remap/fault work; two rolling cases lose
about 5–6%. Warm-path improvement does not establish a universal trim cadence.
Keep cached capacity and physical backing return as distinct policy choices.

The matched 64-live peers remain faster: local free list **3.32 ns**, bitmap
**4.72 ns**, snmalloc **3.58 ns**, mimalloc **3.96 ns**; libc takes **15.82 ns**.
These peers return direct pointers and do not provide the descriptor's binding
and lifetime semantics. The remaining gap favors keeping a dense direct/slab
candidate for stable allocations. It does not justify imposing a binding on
every allocation.

Longer read confirmation uses five 100 ms samples per order. At 4,096 live
allocations, resolved point throughput is **1.47 ns** and dependent chase
**9.04 ns**, versus frozen **1.48 ns / 9.07 ns**. It finds no meaningful regression
in this configuration; short broad-sweep timings show some order sensitivity.

For sixteen occupied homes spread across classes, same-class release/reclaim
costs **1.39 ns** with masks, **8.74 ns** with a sorted sixteen-entry list, and
**9.66 ns** unordered. One-home enumeration favors a list: **0.83–0.90 ns**
versus masks' **3.38 ns**. At sixteen homes, whole-set enumeration is about
**7.5–7.7 ns** for all three. This supports masks for the mutable authority and
lists where sparse traversal earns its cost, rather than a universal free list.

[Retained evidence](evidence/zen5-tuning/artifact.json) keeps every summarized
geometry, matched peer/history, occupied-set competitor and order. Original
grouped samples and full captured sources remain recoverable from its archive.
Release checks, ASan/UBSan, independent page-union/straddler checks and all
7,650 broad samples pass; the focused read comparison adds 300 samples.

## What changed

Allocation selects a size class once, decodes the selected grid with shifts, and
updates its identity and home masks. Release checks the descriptor and decodes
its group with exact reciprocal multiplication. Cold mapping preparation stays
outside the warm path. The geometry and checked API remain the same.

The previous path also updated page-use counts on each allocation/free. The new
path derives the live-page union from occupied homes when reporting or trimming,
including homes crossing a page boundary. It removes two bytes per possible
payload granule from owner metadata, but adds scanning and temporary scratch to
report/trim. Full class masks mark one contiguous interval. Empty mapped backing
remains cached until explicit trim. The prototype allocates scratch during the
call; pressure-safe reclamation would need reserved or supplied scratch.

This is a serialized, finite-A prototype: release requires descriptor aliases
and byte users to have ended. It has no retained old homes, relocation, remote
retirement, MVCC or durability. Those homes would have to enter the page-liveness
union; an empty current-identity mask must not hide them.

## Occupancy choices

Free homes outnumber current occupied homes in the arithmetic grid. The bitmap
already records the occupied set without storing a coordinate for each free home.

| Representation | Prepared bytes | Physical-home capacity |
| --- | ---: | ---: |
| Fifteen occupied masks | 30 | 240 |
| Sorted/unordered occupied list | 241 | 240 |
| Sorted/unordered sparse list | 17 | 16 |
| Sorted/unordered sparse list | 65 | 64 |

The sixteen-home list saves thirteen bytes of occupancy metadata; this does not
automatically save an allocator cache line or a payload page. Retained and pending
homes can exceed sixteen even with only sixteen current identities. A sparse list
therefore needs a spill/growth policy before serving that lifecycle.

Sorted lists search class-local entries and shift entries on mutation. Unordered
lists append or swap cheaply, but scan to find class gaps and release entries.
A ready-free cache can accelerate allocation independently of authoritative
occupancy. Neither representation authorizes reusing bytes still in use.

## Comparison method

[tune.sh](tune.sh) recovers the measured pre-tuning source using
[its artifact reference](tuning-baseline.json), then compiles it in a separate
namespace. Shared descriptor definitions and the original benchmark body must
match the archived source. Both implementations run on one pinned CPU, in both
orders, with three repetitions. These are batch-average reciprocal throughputs,
not latency percentiles.

The original benchmark covers all four layouts, widths 16–1,024, native 4 KiB and
modeled 16 KiB granules, reads, and prepared selected-grid allocation/free. The
matched fixture uses reordered class packing, width 1,024, and identical endpoint
touches and payload checks for descriptors, local pools and allocator libraries.
It compares live sets 1/64/1,024 and rolling replacements of 10%/90%. Library
backing and metadata are unmeasured; custom pool and descriptor storage is charged.

Counters-per-batch and trim-every-eight-batches expose deferred work. Full-batch
trim includes repeated discard, remapping and faults. Fixed-size rolling histories
mostly reuse the same current-home set, so their trim cost is primarily scanning.
Preparation is outside timing; later faults caused by timed trim remain inside.

The occupied-set fixture separately measures same-class release/reclaim and
enumeration at 1/4/16/64/128/240 occupied homes. Reclaim immediately fills the hole
just freed, a favorable steady state. Independent probes cover arbitrary holes,
mixed mutations, bounded-list overflow and full physical-home pressure.
