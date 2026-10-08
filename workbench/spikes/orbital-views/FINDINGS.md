# Performance findings for embedded Orbital

The first fixture established useful lifetimes and recovery, but embedded costly
choices: full snapshots, one admitted mutation, per-page preparation, ordered-set
readiness and waits for derived material. The follow-up compares alternatives
across publication, concurrency, native binding, residency and actual I/O. These
are recommendations from experiments; Ashton's [design workspace](../../notebook/orbital/ASHTON-SCRATCH.md)
remains untouched.

Ashton's embedded target is competitiveness with LMDB on all/most point
operations, in both latency and throughput, including the maintenance those
operations require. Its baseline can retain producer input in memory and admit
events in epoch batches; file-durable admission is a stronger configuration.
The [consolidated design](../../design/orbital/DESIGN.md) owns the proposed operations and lifetimes. The isolated
timings below do not establish LMDB competitiveness.

## Recommendations

1. **Complete an epoch when its stable version has a valid recovery basis.**
   Derived material writeback can trail. An admitted operation must retain enough
   input to reproduce its result; an ID and operation name are insufficient.
   If the admitted intent already supplies a reproducible recovery basis,
   publication needs no second output-persistence wait. The [semantic proof](systems/intent_probe.cpp)
   executes retained memory inputs and crash-durable file inputs through the same
   Ikea/owner law; epoch batches can share capture and publication. Keep recovery
   dependencies until safely superseded. Bound material queues, log growth and
   reconstruction debt separately. Batch queued work without imposing fill wait
   on an idle point operation. In file mode, sync service dominated byte-copy work
   on the worker's gp3 disk. Account for queue and batch-fill wait too.
2. **Let Ikea data units define useful change and reconstruction.**
   SeriesPack knows tiles and planes; TuplePack knows packed units and preserved
   neighbours; Bec256 knows replacement bodies and their interpretation metadata.
   Orbital can retain and transport their qualified recovery material without
   prescribing one patch codec, material grain or logical fingerprint. Preserve
   unchanged material and certification where the unit permits it; retain simple
   whole-copy options for small or dense changes. Shared pages, finer blocks and
   patch chains expose different costs in this experiment, rather than selecting
   a universal Orbital delta format. Budget metadata, capture, reads, compaction
   and retained dependencies alongside changed bytes.
3. **Retain useful native bindings and keep the trusted loop ordinary.**
   Take a cached view only when borrowers and recovery permit it. Otherwise copy
   or COW according to coverage and reader demand. A reused raw allocation does
   not preserve contents, permissions or source identity. Batch contiguous
   preparation and VM changes. Keep one execution-window lifetime around the
   native loop; avoid source lookup, refcounts, grants, dispatch and capture
   callbacks around each store. Loom can use unit-specific access requirements
   without breaking Ikea's bulk traversal into tiny tasks. A resident embedded
   operation should use the ready binding directly, without unnecessary queueing
   or a syscall. Specialization and caches preserve the same mutation/maintenance
   obligations as the ordinary path.
4. **Localize preparation without inventing independent visibility.**
   An unrelated object should not wait behind another object's material or disk
   work. Local heads can progress independently; shard-consistent reads still
   need a completed cut and historical selection. A shared cutoff can become
   a hot cache line. With ordered local completion and known epoch assignment,
   separate producer progress and derive the cut on demand; that trades writer
   traffic for reader scans. The measured counters use supplied fixed interleaved
   IDs; local order alone cannot locate the next global hole. Local out-of-order
   completion needs local hole tracking. Before native
   stores, establish their physical conflict envelope: disjoint field locks do
   not make overlapping packed stores safe. Install interpretation and summary
   validity/corrections with compatible data.
5. **Separate logical parts from OS resources and residency policy.**
   A part or revision need not own an FD and VMA. Share backing resources with
   independently retireable extents where appropriate; one live fragment must
   not accidentally retain a huge dead chunk. Reserve addresses independently
   of touched bytes. Retain HOT/WARM homes when useful and budget that inventory.
   Reclaim cold storage off the foreground path, measuring subsequent faults and
   reuse as well as release. Hints do not prove cache residence. Background VM
   changes can still interrupt other workers in the address space.

## Composition evidence

The [semantic-intent probe](systems/intent_probe.cpp) uses actual TuplePack
writes with interpretation, membership, count and sum. File admission precedes
execution; fresh processes recover after admission, interrupted private work and
installation while a derived transfer is stalled. Owner-defined checkpoints
preserve a promised old cut and retry cursor before prefix retirement; reopening
then scans only the suffix. The memory case separates producer positions from
multi-command epochs and publishes one final head per batch. These are small
single-owner proofs, not a selected journal wire format.

The [binding probe](systems/binding_probe.cpp) extracts raw scratch reserve/grow,
retained native windows and generic captured memory/file sources. Backend threads
post results; the owner settles readiness. Failed or cancelled acquisitions expose
no new window, late completion retains its old target and cannot replace a newer
binding, and final retirement permits reuse. The earlier
[native composition](systems/composition.cpp) remains a comparison using durable
afterimages and actual successful/failed background writes. Its stores precede
journal persistence, so it is not the admitted-intent sequence.

For example, **Bec256 empty and full sets both have zero-byte bodies**, yet
decode to opposite values. Population and length are caller-owned interpretation
metadata. The [unit-boundary probe](systems/unit_boundary.cpp) checks that actual
encodings emit no body effects, demonstrates the insufficient body-only outcome,
then retains the old cut and reopens the new one with descriptor/summary captures.
It passes native and ASan/UBSan on the local Arm VM, without adding timing claims.
Body-only damage or certification cannot distinguish these values. SeriesPack
tiles and TuplePack preserving stores likewise prevent a universal byte/page
delta from defining the complete mutation. The [source review](prior-publication.md)
records the actual interfaces and obligations. They can change.

Today's hooks run before stores and borrow source identities. The owner resolves
identities while live and captures after a completed local frontier while bytes
are stable. A byte afterimage containing preserved neighbours cannot overwrite
independently newer neighbour state without compatible exclusion or merge.
Summary damage needs separate material; summary validity needs owner semantics.
Count/sum corrections and min/max repair have different laws, as the
[aggregate review](prior-publication.md#summaries-publish-validity-with-data) explains.

## Evidence and decision boundaries

These are medians of six pinned trial means on a reused **Spot c8a.2xlarge,
Zen5 / EPYC 9R45**, Clang 21.1.8, 4 KiB Linux pages. Phases have different
comparands: adding their medians would not predict end-to-end latency.
Threaded ns/publication measures aggregate wall-time throughput, not individual
request latency.

| Comparison | Observed result | Implication and limit |
| --- | --- | --- |
| 64 B change / 1 MiB; identical incremental content root | Full snapshot 79.4 µs; shared two-level pages 1.45 µs | Preserve unchanged material; this excludes journal I/O and retains every cut. |
| 128 dispersed byte writes / 1 MiB | 4 KiB pages copy 512 KiB of predecessors; 256 B blocks copy 32 KiB. Front cost 64.8 vs 54.7 µs | 16× less predecessor copying buys only 1.18× CPU improvement here; finer metadata/allocations matter. |
| 64 B change / 16 MiB; identical root | Flat page directory 39.4 µs; two-level sharing 2.72 µs | Unchanged metadata can dominate too. The root vector still scales; this is not a fully logarithmic index. |
| Latest-only retention after 16 half-part changes / 1 MiB | Uncompacted chain retains about 9 MiB; collapse every 8 retains about 1 MiB | Debt survives retirement of external readers. Counts are requested allocation bytes, not RSS; 8 is a comparand, not policy. |
| First 64 B recovery read after those changes | Forward replay 6.83 µs / 16 patch visits; latest-cover replay 0.61 µs / 1 | Avoid obsolete payload. Both index the entire history first; buffered recent-file reads do not price cold storage. |
| 16 contiguous changed 64 B blocks | Ordered metadata + page holds 936 ns; bitmap + one window 85.7 ns | Keep metadata/lifetime work outside the native loop. No mapping or persistence is included. |
| Four producers, cut query every 16 updates | Shared progress 23.1 ns/publication; producer counters 3.60 ns | Supplied interleaved IDs and ordered local completion permit moving work toward readers. Query-every-update costs 30.3 vs 24.0 ns; warm readonly query costs 3.27 vs 4.55 ns. |
| Two mutations, 16 KiB binding, no old reader | Per-page copy/preparation 29.0 µs; retained mapped copy 1.15 µs; eligible take 0.82 µs | Reuse prepared bindings. Immutable expected sources are prebuilt; these are binding episodes, not durable publication. |
| 1,024 immutable 16 B parts | Separate backing: 1,024 FDs, 1,025 VMA delta; groups of 128: 8 FDs, 9 VMA delta | Logical parts need not multiply OS resources. Separate backing actually exhausted the worker's 1,024 soft FD limit. |
| 64 B events on ext4/root gp3 | Sync each: 2.00 ms/event; batches of 64: 33.6 µs/event | Durable service dominates tiny copy choices here. Arrival/batch-fill wait, frame construction and quorum costs are excluded. |

The matrix varies change density/fragmentation, part size, certification, retained
cuts, owner count, completion/query cadence, readiness, reader timing, binding
strategy, backing granularity and journal batching. Detailed comparands and limits
live with [publication](publication/README.md), [control](control/README.md),
[residency](residency/README.md) and [resource/journal service](systems/README.md).
[Main evidence](evidence/exploration/README.md) and the
[successful follow-up](evidence/exploration-followup/README.md) retain summaries,
ranges and work counters. Raw trials, captured sources and binaries are recoverable
from their verified S3 archives; both report recipes were regenerated and compared.

Other important costs remain visible. A shared sealed chunk holding dispersed
live fragments can retain 64 MiB for 6.44 MiB of useful bytes; a common file with
independent extent retirement need not have that coupling. `MADV_DONTNEED` made
release/reuse costly through later faults; `MADV_FREE` behaved differently on
the Arm VM and Zen5 worker. Neither is a universal cheap-reclaim rule.
The [TLB study](tlb/README.md) found 64-page protection batching at 7.76 µs
versus 355 µs page-by-page with three same-process workers. Moving the syscall
to a helper does not remove its disturbance of those workers.

## From the spike to an implementation

The LMDB comparison belongs at the complete point-operation boundary: hit/miss,
insert, replacement, deletion and read-modify-write, with transaction cost and
the same key/value sizes, reader lifetimes, durability and batching. Measure
individual latency and sustained throughput separately. Reads need real key
resolution and version-correct cache behavior; writes include allocation,
representation/index/summary maintenance, publication and required durability.
Also observe deferred work and memory/storage growth through steady state and
drain, so background maintenance cannot disappear beyond the timing interval.
LMDB's [own API documentation](https://github.com/LMDB/lmdb/blob/mdb.master/libraries/liblmdb/lmdb.h)
describes durability-changing flags and how old readers constrain page reuse;
those are comparison conditions, not incidental tuning choices. A bare KV
comparison and one with SixDB-specific summaries/indexes answer different
questions; expose the extra work. This spike has no complete SixDB key index
or matched LMDB operation harness and makes no competitiveness claim.

The [Calico point-path review](prior-publication.md#evidence-and-omissions) confirms
that summary-enabled updates did perform joint maintenance, but its current YCSB
winning preset disables the summary policies and fast recipes decline them.
Guarded retained preparation is worth carrying forward; omitted supported work
must not become the price of its speed.

The original seven composed probes remain lifetime/replay oracles. Their full-part
memfd rewrite, redundant staged journal bodies and single global mutation are
experimental baselines. Earlier [complete-cycle timings](evidence/patterns/README.md)
exposed that sealing amplification. The expanded composition establishes compatible
recovery and background-material semantics, without claiming a production optimum.

File admission and checkpoints here exercise a stronger persistence provider
than the memory baseline.

The design still needs production implementations of concurrent read-guard
reclamation, allocator failure handling, stable locator/extent reuse and bounded
lazy restart. Checkpoint and retry proofs exercise one adoption and a trusted
compiled interpreter. Supplied masks and projection buffers do not establish an
actual WASM/Firecracker sandbox. The experiments do not select network/quorum
durability, a cryptographic format or one page/cache model for all hardware.
