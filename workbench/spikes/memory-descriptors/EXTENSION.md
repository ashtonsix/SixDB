# Grouping, allocation and retirement

Packing, allocator authority and read protection are different units. Wider
packing may save backing without requiring one core to own every allocation or
one reader to retain the whole group. The extension compares 16, 64, 256 and
1,024 trampolines, retaining sixteen identities and sixteen homes/class/grid.
Virtual placement remains open; no production policy is selected.

```text
packing group    ┌─ grid ─ grid ─ grid ─ … ─┐   shared backing pages
allocator owners └─ owner ─┘ └─ owner ─ … ─┘   mutation authority
read protection       home / grid / group       retained byte use
```

## Density and coupling

The [packing sweep](packing-sweep.py) checks all 5,120 possible small homes in
each of 32 geometries, using 4 KiB and 16 KiB pages. It compares consecutive grid
images, ascending/reordered class runs and class-page padding; ownership varies
independently. Mixed, sparse, homogeneous and delayed-retirement snapshots share
the same coordinates. Counts price covered pages and cache lines, not RSS or time.

For all 1,024 live 16-byte homes, reordered class packing on 16 KiB pages needs:

| Packing group, trampolines | Payload pages | Allocator owners/page with 64-trampoline authority |
| ---: | ---: | ---: |
| 16 | 64 | 1 |
| 64 | 16 | 1 |
| 256 | 4 | 4 |
| 1,024 | 1 | 16 |

The last row is the intended 1,024 × 16 B = 16 KiB case. Four 4 KiB pages can
back the same payload bytes; 16 KiB pages do not reduce that minimum by
themselves. The modeled persistent dense control page adds 4 KiB or 16 KiB.
Allocation metadata is excluded from this small-home geometry model.

Larger groups have counterexamples. A mixed dense 64-allocation snapshot uses
one 16 KiB page with consecutive grid images but five with class runs spanning
1,024 trampolines. Class order improves some homogeneous alignments without
making one order best for every mixed history.

With one held 16-byte old home after all others retire, the modeled
home/grid/1,024-group retirement policies keep 1/16/1,024 homes. This conservative
retention follows the chosen policy, not an inherent requirement of wide protection.
All occupy one 16 KiB page in the wide class
layout, so the extra retention is initially invisible in page counts. On 4 KiB
pages, the widest hold keeps four pages instead of one. Legal-home availability
and page return consequently need separate accounting.

Dense trampoline cache lines span four grids. Sixteen-trampoline owners share
those lines; aligned 64-trampoline owners do not. Payload pages can still span
many such owners. Coordinated page release, projection and COW need the full
overlap set, regardless of which core allocates a slot. Full counters and
ownership profiles are in [packing-sweep-results.json](packing-sweep-results.json).

## Native candidates

`grouped.{h,cpp}` implements four finite-A layouts and four packing widths, with
larger classes in private ranges. A table-based resolver supports every layout.
Bitmap occupancy and lengths use 64 bytes/grid. The later
[allocation tuning](TUNING.md) derives page liveness during trim/report instead
of keeping eager per-page user counts. Quiescent release reuses a home immediately, retains empty mapped
capacity for allocation, and exposes explicit empty-granule trimming.

The native grid-major baseline dedicates one mapping granule/grid. The geometry
model also packs consecutive 4 KiB grid images within a larger page; it therefore
explores a candidate beyond that native baseline. Control pages are per packing
group natively, versus one dense persistent page in the model. Compare matching
units rather than treating their footprints as interchangeable.

On a 4 KiB Linux host, the 16 KiB candidate aligns reservations and maps/faults
four native pages together. Its arithmetic and reclamation granularity are real;
its TLB, fault costs and kernel COW remain those of 4 KiB hardware. This is
explicitly labeled a modeled mapping granule, not a native 16 KiB measurement.

Native access compares direct pointers to exactly the same payloads with
trampoline resolution, over 64 and 4,096 live 64-byte allocations. Point and scan
costs are reciprocal throughput; dependent chase exposes serial access cost.
Prepared allocation/touch/release excludes mapping and retains its checks.
No retained reader, rebinding or MVCC protocol is priced by these loops.

The separate allocation comparison uses libc, pinned mimalloc 3.5.3 and snmalloc
0.7.5, local free lists/bitmaps, shared mutex/atomic bitmaps and region reset.
All allocate the same 16/64/96-byte requests, touch both ends and free in the same
permutation, with 1/64/1,024 live allocations. Region reset is valid here because
all users end together. It does not implement independent free. Prepared pools
have fixed capacity; libraries can replenish and internally cache memory.

The two-core comparison uses two concurrent self-freeing callers. Its aggregate
reciprocal throughput includes start skew and join/teardown; it is not remote-free
latency. Custom pool state is shared; libraries may use their own thread caches.
The simple shared bitmap searches from its first word, deliberately
exposing contention rather than representing every shared policy. Both library
adapters include an ordinary function-call boundary, vendor release checks, no
malloc interposition and the pinned compiler. The benchmark retains independent
correctness checks and puts libc into multithreaded-process mode before all cases.
Concurrent checks use distinct thread values and verify that simultaneously held
allocations have distinct addresses.
Footprints distinguish requested/usable live bytes, prepared capacity and policy
metadata; zero library pool/metadata counters mean unmeasured, not no overhead.
Library caches and RSS are unmeasured. Pair timings include payload,
pointer-array and checksum work, and are not an isolated instruction cost.
Pool policy counters charge link/bitmap arrays, locks or the region cursor;
object handles and heap overhead are excluded.
The descriptor pair loop touches one word; the generic loop touches both ends
and checks a permuted live set. Their numeric difference is not allocator overhead.

## Measured signal

The [retained Zen 5 run](evidence/zen5-extension/artifact.json) uses an EPYC 9R45
`c8a.large`, two cores, Clang 21.1.8 and `x86-64-v4`/Zen 5 tuning. Each case has
three 20 ms repetitions in each order: 2,880 grouped samples and 522 allocator
samples. The script completed in 79 seconds. Native address/reuse checks and
payload checks pass; local grouped/pool probes also pass ASan/UBSan.

For 4,096 live 64-byte allocations, reordered small-class packing on native
4 KiB pages gives these median costs across six samples:

| Packing width | Mapped control + payload | Point read, ns/allocation | Serial chase, ns/step |
| ---: | ---: | ---: | ---: |
| 16 | 2,048 KiB | 2.43 | 13.35 |
| 64 | 512 KiB | 2.19 | 12.23 |
| 256 | 320 KiB | 1.64 | 10.13 |
| 1,024 | 272 KiB | 1.50 | 9.14 |

These changes include control-page density and payload placement. At width
1,024 the matched direct chase is 5.51 ns/step. A hot 64-allocation cohort gives
1.20–1.22 ns for independent trampoline reads across the layouts/widths, below
the 2.50 ns reciprocal-throughput target. Neither measure is a per-access p50 or
tail percentile. The [full summary](evidence/zen5-extension/grouped-summary.csv)
keeps all layouts and modeled 16 KiB cases. Before the
[allocation tuning](TUNING.md), prepared descriptor pairs cost 16.28–18.28 ns
across configurations; their mapping count remains unchanged.

The generic 64-byte, 64-live history gives:

| Mechanism | Median ns/allocation-free pair |
| --- | ---: |
| Region, bulk reset | 1.41 |
| Local free list | 2.47 |
| snmalloc | 3.20 |
| mimalloc | 3.65 |
| Local bitmap | 4.20 |
| Shared atomic bitmap | 9.67 |
| libc | 14.85 |
| Shared mutex bitmap | 24.06 |

mimalloc's order medians are 3.38 and 3.93 ns; collapsing them hides an order
effect. With two concurrent callers, atomic/mutex pools give 33.15/100.51 ns
per aggregate pair, versus 1.50/1.72 for snmalloc/mimalloc using their own caches.
This exposes the cost of the deliberately centralized candidates, without
ranking all shared allocators or remote-free designs. The
[allocator summary](evidence/zen5-extension/allocation-summary.csv) retains sizes,
live-set sizes, order effects and memory counters. Libraries supply fast small
allocation in this history; an arithmetic descriptor still needs its binding, occupancy
and page accounting priced in the intended composition.

## Retirement and progress

The [retirement model](retirement-policies.py) compares 120 cases across authority
width, protection and direct/shared versus batched retirement, with batch sizes
1/8/32. Ninety-six finish safely; 24 size-only cases intentionally expose stalled
partial batches. Another twenty histories contain ten safe alternatives, six
safety failures and four progress failures. These are bounded deterministic
histories, with work counts and byte-ticks, not latency or CPU rankings.

Completion must identify the acquired home/domain rather than re-resolve a reused
descriptor. Duplicates need single-application accounting; lost notifications
need reliable delivery or reconciliation against separately observable completion
authority. Neither timeout nor cancellation proves that byte use ended.

Idle owners need service without future allocations. Queue pressure must not
require allocation from the storage that queued completions would release.
Handoff must preserve outstanding authority and late-completion routing. Direct
shared retirement avoids that queue but pays shared mutation costs elsewhere.

Pressure flushes are not monotone in every memory metric: one history reduces
retained slot byte-ticks while increasing retained page byte-ticks by moving the
next partial-batch deadline. A region escape likewise trades copying and a brief
peak increase against retaining a whole region. Promotion must happen before
handoff; a later copy cannot redirect an already escaped raw pointer.

The [results](retirement-policies-results.json) retain these failures and safe
counterparts. Together, the probes favor keeping page arbitration, allocation
authority and physical completion explicit while exploring their policy choices.
