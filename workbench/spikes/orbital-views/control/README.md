# View control path

These independent fixtures isolate metadata, ownership and lifetime work around
an ordinary native loop. They complement the mapping/publication probes above;
they do not implement a shard protocol or change Orbital's design.
The [completed-cut follow-up](cut.md) tests producer-local progress against the
shared watermark with reader costs included.

Run from the repository on Linux (prefix with `orb -m ubuntu` on the Mac host):

```sh
bash workbench/spikes/orbital-views/control/run.sh
```

The ownership comparison needs **four allowed CPUs**. Each worker pins itself to
a distinct allowed CPU; single-thread cells pin to the first. Do not wrap the
whole script in a one-CPU `taskset`. `SIXDB_RESULTS`, `SIXDB_CONTROL_BUILD`,
`SIXDB_BUILD_JOBS`, `SIXDB_MARCH` and `SIXDB_TUNE` select output/build settings.
The standalone CMake uses the project's pinned Clang/settings. One compiled
mechanism TU serves the correctness and timing callers; variants do not multiply
native kernels across combinations of owner, metadata and lifetime policy.
The existing compile helper records all three TUs separately after measurement,
and `binary-size.txt` records section sizes. These are build-cost receipts for
this arrangement, not evidence of a gain over an unimplemented template matrix.

## Comparisons

All policies in a cell perform the same native stores and maintain the same
exact sum. The independent oracle checks every final word, including untouched
space. Timings exclude input construction and final verification. Six trials
alternate forward/reverse cell order; `summarize.py` rejects unequal work or
missing cells. Wall time per operation is aggregate throughput for threaded
cells, **not individual request latency**. `holds` counts actual reference
acquisitions and is not a modeled time.

| Family | Alternatives and included work | Important limit |
| --- | --- | --- |
| Ownership | One global owner, four group owners, or one owner per object; 1/2/4 pinned threads update eight independent objects each, 64 bytes + sum + release publication | Semantic admission is supplied; no competing writers for one object, disk, mapping or replication |
| Prefix | Each ownership cell repeats with/without an atomic completion bitmap + contiguous watermark; producer IDs are preassigned/interleaved | Local heads and a shard-consistent cut have different semantics; the bitmap retains the whole bounded run |
| Readiness | Ordered set vs bitmap; fresh or already-ready images, 16 adjacent/16 scattered/256 selected pages; two readiness passes + identical 64-byte stores per selected page | No VM calls, no I/O and no sparse hierarchy. A 256-page bitmap is deliberately small |
| Lifetime | One retained execution window, a vector of page holds, or one hold per 8-byte store; identical native work | Trusted ordinary pointers; these counts do not enforce field permissions or sandbox isolation |
| History | Map predecessor lookup, sorted contiguous predecessor lookup, or an already captured exact revision handle at 16/256/4096 retained revisions | Captured handles assume resolution already happened and retain the source. They cannot replace arbitrary epoch lookup or retirement policy |
| Combined | Readiness + damage metadata + lifetime + native stores + exact sum + immutable afterimage + atomic head + contiguous prefix; ordered/page vs bitmap/window | Damage units are **64-byte blocks**, not OS pages. Native store/copy granularity is identical. No persistence or mapping; every head has the same selected layout |

The combined comparison retains staging and publication costs common to both
policies; isolated cells explain which metadata/lifetime costs are being changed.
It uses an immutable afterimage for all changed blocks and publishes its epoch,
bytes and sum together. It does not measure the existing full-part memfd rewrite
or pretend those costs disappear.

## Consequential cases

`probe.cpp` demonstrates the forbidden split-publication state without a C++
data race, then exercises out-of-order installation with a prefix that never
passes a hole. A blocked owner prevents unrelated work under the global policy;
localized owners allow the other object to finish while its shard cutoff waits.
A failed epoch leaves a hole: a real owner must supply a terminal outcome rather
than silently skipping it. Out-of-order completion needs historical selection
at the cutoff; reading each object's latest head is insufficient.

Two disjoint logical fields sharing a native word lose an update with stale
whole-word stores. A masked atomic merge preserves both. This proves a physical
composition obligation; it does not select atomics over ownership or private
projections for real kernels. Lock masks and page-level writable grants alone
are not that envelope. Bytes and summary validity must publish in one stable
descriptor (or an equivalently coordinated mechanism); dirty summaries need a
defined query path before their validity can be advertised.

The pending-source fixture coalesces requests only for the same immutable source
identity/revision, independent of a reused physical slot. A replacement generation
rejects an old completion; cancelled waiters do not cancel other waiters; the
backend owns source and destination until completion even after all waiters
leave. Partial success supplies no CPU-readable lease. It intentionally omits
I/O, retry policy, durable locators and cross-request page unions. The parent
spike's acquisition probes exercise actual native destinations and file adapters.

Unchanged object revisions preserve a captured cache while unrelated shard
epochs advance. This is identity behavior, not evidence that PTEs, cache lines or
TLB entries remain warm.

## What the patterns require

Keep one trusted execution-window hold outside efficient native loops. Reuse
exact captured source handles after epoch resolution. Compact metadata should
track touched ranges/blocks without creating one heap node per item; dense
bitmaps and ordered/range forms have different space costs as the address space
grows. Ownership locality removes unrelated serialization, but does not remove
multi-object admission or a consistent-cut dependency. The fixture deliberately
keeps that dependency visible.

The minimum mechanism obligations are stable captured identities, explicit
readiness/failure, backend completion lifetime, safe physical store composition,
immutable publication containing summary validity, and history selection at a
completed cut. A per-store grant, a global mutator, `std::set`, or repeated map
resolution is not itself one of those obligations. Compiled orchestration can
enforce them once around native windows; useful kernel specialization can stay
local. Neither an efficient native loop nor sparse residency justifies removing
logical admission or historical source lifetime.

Local native and ASan/UBSan probes pass. Target-machine timings and their
captured artifact are recorded by the parent run; local Arm/virtualized timings
are not retained as a Zen5 result. Raw trials belong in ignored build storage or
the captured artifact, not this directory.
