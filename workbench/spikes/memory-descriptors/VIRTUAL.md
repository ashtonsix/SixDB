# Independent virtual growth

Virtual allocations can grow independently of the finite grid's slot classes.
The experiment compares backing mechanisms while leaving virtual placement,
its arithmetic addressing rule and descriptor format open.

Four quantities stay separate: reserved address space, writable prepared capacity,
initialized/readable prefix, and each operation's admitted read window. Preparing
spare pages does not publish additional logical contents.

```text
test identity ─► current binding ─► reserved/prepared storage
raw grant ──────────────────────► captured storage + admitted extent
```

| Candidate | Growth mechanism | Address promise |
| --- | --- | --- |
| Reserved window | Make more of its existing reservation writable and prepare pages. | Stable within the chosen window. |
| Copying binding | Prepare a larger mapping, copy the initialized prefix, rebind. | Current payload can move; old grants retain their image. |
| Linux `mremap` binding | Extend or move the mapping; copy instead when a raw grant prevents remapping. | Movement requires a binding or a uniquely owned direct reference. |

The test identity is an owner index. The movable candidates are **not drop-in
implementations for copied direct palette-C descriptors**. A direct descriptor
needs its address promise preserved, or a proven unique owner able to update it.
The two-MiB window is a bounded test parameter, not a selected virtual stride.

## Native comparison

On Zen5 (EPYC 9R45, Clang 21.1.8, native 4 KiB pages), the interleaved history
gives these six-sample medians. Time is the average ready cost per growth over
the whole history, including preparation and initialization, not an individual
growth latency percentile.

| Candidate | µs/growth | Explicit copies/history | Mapping operations/history |
| --- | ---: | ---: | --- |
| Window, exact pages | 8.86 | 0 | 16 mmap + 256 mprotect |
| Window, geometric | 8.64 | 0 | 16 mmap + 144 mprotect |
| Copy, exact pages | 25.68 | 36.94 MiB | 256 mmap |
| Copy, geometric | 15.64 | 10.56 MiB | 144 mmap |
| mremap, exact pages | 9.54 | 0 | 16 mmap + 240 mremap |
| mremap, geometric | 9.13 | 0 | 16 mmap + 128 mremap |

Windows retain **32 MiB VA** for sixteen MiB prepared. The movable candidates
finish with **16 MiB VA**; copying briefly owns overlapping mappings, with peaks
around **16.5–16.75 MiB prepared**. Geometric mremap moves payload addresses about
105–106 times per history, making its required address-update mechanism concrete.
Zero explicit copy bytes does not mean zero kernel or page-table work.

Prepared sixteen-byte appends cost **4.18–4.38 ns** across candidates, with no
timed mapping, remapping or copying. Growth within capacity and growth requiring
backing preparation are different scheduling costs. Geometric preparation shifts
some work earlier; its small whole-history advantage does not establish better
tail latency for every append.

These results favor reserved windows when the address promise and VA budget fit,
and spare-capacity growth when copying is needed. Quiescent mremap is a useful
Linux option where movement is legal. They leave window sizing, virtual placement
and retained-reader performance open.
[Retained evidence](evidence/zen5-virtual/artifact.json) contains both orders,
all candidates, work counts and hardware context. The archive preserves captured
sources. All 93 native checks, ASan/UBSan and 72 measured samples pass.

## Growth and readers

An existing raw prefix can survive stable-address append beyond its admitted
extent. Address stability does not preserve bytes changed inside that extent.
For example, a 47-byte body with a granted 64-byte native read window blocks
stable append to 50 bytes. Copying can produce the new body while preserving the
old image, including its padding. Completing the widest grant removes that
restriction even if a narrower prefix reader remains.

The prototype accounts for each active admitted window. Identity release does
not revoke a grant's captured mapping; its final completion releases old backing.
This is a serialized lifetime probe, without concurrent admission/publication,
MVCC or durability. Timed histories have no held grants: retained-reader fallback
is correctness-tested, rather than priced by the growth timings.

## Capacity and preparation

Exact-page preparation follows requested growth. Geometric preparation adds
spare writable capacity, reducing repeated protection changes or replacement
mappings. That can prepare unused pages early. Every newly prepared page is
first-written in this experiment; append then initializes its actual new bytes.
Preparation cost, copy cost and byte initialization remain visible.
Unlike the finite-slot cache, this probe unmaps released virtual backing after
its last grant ends. It does not select a virtual backing-cache policy.

The interleaved history grows sixteen allocations through twenty-two sizes from
one KiB to one MiB. Every candidate ends with sixteen MiB initialized and prepared.
A second history prepares sixty-four allocations, then measures sixteen-byte
appends within their existing page. It asserts that these appends perform no
mapping, remapping or copying.

Ready timing includes initial mapping, preparation, first writes, copying,
superseded mapping retirement and appended initialization. Final whole-prefix
verification and final close are outside timing; within-page preparation and
quiescent rewind are also outside. Owned VA and writable-mapping counters are
distinct from RSS. Process fault deltas include runtime activity and need not
equal explicit page touches.

[virtual-growth.cpp](virtual-growth.cpp) checks independent neighbors, page
boundaries, maximum rejection, admitted padding, partial reads, completion order,
forced remap movement and retained images. Injected metadata-allocation failures
verify OS unmapping and retry. General OS-failure rollback is unclaimed: failed
preparation can retain charged reserved space without publishing a logical prefix.

Build `memory_descriptors_virtual_growth`; `--check` runs its native probes.
[virtual-grow.sh](virtual-grow.sh) measures both orders on a worker, and
[virtual-summary.py](virtual-summary.py) verifies end states, checksums and work
counts before comparing costs.
