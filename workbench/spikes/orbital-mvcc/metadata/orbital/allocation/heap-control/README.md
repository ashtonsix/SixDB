# Exact metadata heap backing

Selected on 2026-10-10 for simpler resource ownership/cleanup and smaller explicit
charge. The current implementation retains **full-span trim with three bitmaps**;
payload geometry, placement, hot access and packing are unchanged. This is not
a selection of the older bounded16 policy. [Current source identity and
receipts](current-selection.json) record the exact baseline, patch hash, eight
postimages, checks, CLI inputs and every retained timing repetition.

The control block contains byte bindings, Grids, per-page state and three trim
scratch bitmaps. They need at most 8-byte alignment and no metadata protection
or discard operations. One exact-size nothrow heap allocation replaces the
page-rounded mapping. The existing preparer initializes it after reserving the
payload; owner completion still gates availability. Group/FiniteGroup sizes,
embedded reply ownership and hot pointers remain unchanged.

Heap ENOMEM is `capacity`, including deferred replies; payload mmap failure is
`io`. Metadata is freed once, while failed payload unmap retains its reservation
and full saved charge until successful retry. This removes the second fallible
metadata unmap. Pending sparse-VA statistics count only planned payload bytes.

## Requested bytes and qualification

At 256 identities, control is `align8(256 + 16*62 + payload_pages)` plus three
`ceil(payload_pages/64)`-word bitmaps. Larger native pages are modeled only.

| Page | Palette | Previous mapping | Exact heap request | Saving |
|---|---|---:|---:|---:|
| 4 KiB | A | 4,096 | 2,128 | 1,968 |
| 4 KiB | B | 4,096 | 2,776 | 1,320 |
| 16 KiB | A | 16,384 | 1,480 | 14,904 |
| 16 KiB | B | 16,384 | 1,648 | 14,736 |
| 64 KiB | A | 65,536 | 1,320 | 64,216 |
| 64 KiB | B | 65,536 | 1,368 | 64,168 |

The executed 4 KiB AArch64 census has Group 1,384 B, FiniteGroup 448 B and
separate live bitmaps 80/144 B. Total Group metadata charge falls
6,008→4,040 B/A and 6,072→4,752 B/B. These are **requested bytes, not RSS or
malloc usable extent**. They exclude allocator headers/retained arenas,
Arena/Pool setup, the first 8-byte owning-vector capacity and Object metadata.
Heap calls may lock or perform VM work; free need not return pages to the OS.
Incidental page/cache-line alignment is lost; required 8-byte alignment remains.

Clang 21.1.8 native checks pass allocation, finite, lifetime, preparation,
reclamation and budget. ASan+UBSan pass finite, lifetime, preparation and
reclamation, with leak detection. Both palettes exercise synchronous/deferred
heap ENOMEM after real payload reservation, cancellation, pin conservation,
failed payload unmap and retry; the existing partial-payload failure check is
retained. Allocation runtime/checks are instrumented; reused Ikea fixture
archives are native. This is not full Orbital/Loom or TSan qualification.

## Local feedback

Three alternating pairs per case, unpinned OrbStack AArch64, identical inputs
and flags within each pair. Cold clocks cover the first allocation in 24 fresh
Groups; warm clocks cover 65,536 packed allocate/touch/release operations in
batches of 16. Requests are 64 B/alignment 16 for A, 112 B/alignment 64 for B.

| Median ns | Baseline | Heap |
|---|---:|---:|
| Cold first allocation, A | 6,378 | 3,951 |
| Cold first allocation, B | 6,213 | 4,028 |
| Warm allocate/touch/release, A | 67.10 | 69.67 |
| Warm allocate/touch/release, B | 67.32 | 68.33 |
| Composed point read | 45.84 | 47.24 |
| Composed scalar update | 1,739.79 | 1,723.79 |
| Composed batch-64 update, per request | 538.18 | 477.99 |

The unchanged selected MVCC driver uses 64 Tuple Objects × 16 rows, 8,192 reads
or 513 updates; batch 64 includes a short final publication. Writes retain the
original reader and then drain/poll normally. All 18 runs pass independent row,
outcome and retained-reader checks. Both write arms copy 188,784 B; versions and
active allocations move 64→128→64 and released contexts are zero. Live capacity
is 24,576→49,152→24,576 B. Catalog/Schema bytes, payload mappings and allocation
counts match; the one Palette B Group saves 1,320 B throughout.

Cold and batch observations favor heap backing; warm allocations and composed
reads are slightly slower. These short, variable observations establish no
speed or RSS advantage. The resource simplification and explicit charge motivate
the selection; the mixed results remain in [current-selection.json](current-selection.json).
Private reproduction inputs are `build/reviews/orbital-current-heap-control/`;
the adjacent receipt identifies commands, artifacts and their hashes without
turning private outputs into another maintained implementation.

## Historical controls

[`heap-control.patch`](heap-control.patch), its [cut](heap-control-cut.json),
[test adaptations](qualification-tests.patch) and [qualification](qualification.json)
belong to the older bounded16-plus-heap experiment. Its six native suites and
[trim comparison](../trim-batch16/comparison-qualification.json) remain historical;
they do not qualify a new trim policy here. The separate
[heap-only control](heap-only.md) retains the original full trim-span vectors
and has smoke/census qualification only. Its 5,256/6,792 B totals describe that
older inventory, not the selected three-bitmap layout. Historical patches and
receipts are unchanged; use current production source and the selection receipt
for this implementation.
