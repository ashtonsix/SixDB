# Publication proportional to change: Calico and Ikea

This is a source review for the views spike, not an Orbital contract. Current
Ikea is a concrete baseline that can change with the composition; its hooks and
effect representation are not constraints on Orbital. Calico
was inspected at `ac83c82b9a0c8d76bea92829e471ebc0d98b1978`; the files below
were clean. Its design contains useful ambitions that its composed paths do
not fully realise.

Current Ikea source and examples were inspected at SixDB
`7316b1a87c724c5b4fa5822d560353fcfe7917a4`; they were clean. The integration
guide has pending edits, so the account below is grounded in implementation and
executable adapters rather than treating that guide as an established contract.

## What actually exists

| Mechanism | Implemented behaviour | Limit relevant to this spike |
| --- | --- | --- |
| Damage declaration | [`Xmem::stage`, `stage_many`](../../../../calico/xmem/include/xmem/xmem.h) records exact ranges after ordinary stores. Attached workers share one draft and ledger; partitions are disjoint, and commit waits for them to drain. | Ordinary staging records locations rather than capturing byte-stable patches. Deferred shipping reads the working mapping at commit. Eager shipping requires the implicated wire page to be final, including other workers' partitions. |
| Durable instance commit | `ship_range` and the final commit part copy each declared range's **whole implicated pages** into payloads. [`MultiRegionCoordinator`](../../../../calico/xmem/include/xmem/mregion.h) copies those payloads again when routing by region. | Multiple fragments in one page can ship that page repeatedly. A 64-byte declaration is not a 64-byte durable write. |
| Incremental certification | [`PageRollup`](../../../../calico/xmem/include/xmem/rollup.h) rehashes damaged 256-byte blocks, reuses other block hashes, then hashes the complete hash vector. [`FoldMachine`](../../../../calico/xmem/include/xmem/fold.h) seeds this baseline and periodically rehashes all blocks. | Payload work is incremental after seeding; metadata scans, vector construction and root hashing still cover all blocks. A fresh facade handle starts with every block dirty. Its non-eager stage marks damage; the hash occurs during shipping, not immediately after the store. |
| Sparse codec | `delta_encode` implements XOR runs, merging small gaps and falling back to raw bytes. `delta_apply` reconstructs from the predecessor. | The encoder scans both complete pages. The decoder copies the complete predecessor before applying runs. The maintained commit/fold paths do not invoke this codec; current call sites are tests. Alignment and runs-versus-bitmap selection remain research, despite stronger design prose. |
| Physical version mint | [`FaultRegion::service_wp`](../../../../calico/xmem/include/xmem/fault.h) copies one hardware page to a fresh backing offset and remaps the writer. Old mappings keep their old bytes. `mint_copy_rollup` can compute baseline hashes during that mandatory copy. | This is a separate mechanism, not wired into facade `open`. Facade mappings use one `MAP_SHARED` offset; semantic pins accept current versions and do not supply historical byte isolation. Backing allocation is monotonic. |
| Residency transfers | `shadow_begin_transfer` captures descriptors and window incarnations; the worker copies bytes; owner-side finish checks that the transfer is still current. [`ServingImage`](../../../../calico/xmem/include/xmem/serving_image.h) is explicitly derived, process-lifetime state. | Admission, handoff and refill use 64 KiB windows. They can amplify a small change and are not durability or restart recovery. |

## Current Ikea: data units, coverage and capture

[`ikea/effects.h`](../../../ikea/include/ikea/effects.h) records **issued stores**,
including preserved neighbouring bytes, rather than differences discovered by
comparing versions. `source_write_journal` records the actual named leaf source,
plane and byte span. It borrows preallocated storage and source identities;
it owns neither payload bytes nor a version, and is not a complete delta API.
Only the immediately preceding compatible span is coalesced. Substituted
children report their actual sources, even when their parents or fields differ.

Change granularity belongs with the data unit and its representation. SeriesPack's
[footprints](../../../ikea/include/ikea/seriespack/author/mutation.h)
depend on transposed residuals, tile geometry and separate head planes. Its
[assignment](../../../ikea/include/ikea/seriespack/detail/mutation/assignment.h)
walk shares the expression used for native stores and coverage. Packed stores
can preserve inactive lanes while rewriting their bytes. Public SeriesPack
binding conservatively rejects overlapping physical fields; its trusted helper
and [TuplePack groups](../../../ikea/include/ikea/tuplepack/author/composition.h)
can compose disjoint bit fields sharing bytes under enclosing isolation.
Concurrent preserving stores still need protection for issued bytes.

Bec256 demonstrates why byte damage is insufficient: empty and full sets both
have zero-byte bodies and emit no body stores, yet decode to opposite values.
The [codec](../../../ikea/src/bec/bec256.cpp) uses the caller's population to
interpret those bodies. Its [prepared encoding](../../../ikea/include/ikea/bec/bec256.h)
owns transient replacement bytes; population, length, location and summaries
remain separate owner state. Unit reconstruction and certification must include
the required interpretation, not simply a hash or diff of body bytes.

Useful performance patterns are already implemented in
[physical mutation](../../../ikea/include/ikea/seriespack/author/mutation.h)
and [composed mutation](../../../ikea/include/ikea/seriespack/detail/mutation/composed.h):
freeze placement once, describe maximal contiguous store runs outside native
loops, and assemble complete substituted striped tiles in registers before
storing each half-stripe once. Unit, coverage, scheduling, SIMD, VM-page and I/O
grains can differ. Checked calls preflight the full command before writing; effect
capacity is conservative and can exceed the number of coalesced records emitted.
Avoid turning these bulk paths back into per-store durable callbacks.

**The before-write hook cannot capture a postimage.** Hooks cannot allocate,
fail or suspend, and cover successive groups rather than providing a whole-call
barrier. At a completed call, unit-specific code can form reconstructible change
material: captured regions, a replacement encoding or another justified unit
operation. Resolve borrowed source identities while their views remain live.
Before the next mutation, the material needs immutable storage or an ownership
guarantee preventing change. Repeated writes require preserved order or an
equivalent final capture. Copied coverage records alone do not preserve a cut.

The [SeriesPack adapter](../../../ikea/examples/seriespack/integration.cpp)
retains the lease, view, input, progress, effects and private `sum_change` across
64-row stops. That sum is a modulo-u64 replacement contribution, not a generic
summary-repair policy. The [ownership probe](../../../ikea/test/seriespack/integration/ownership.cpp)
resolves source identities before destroying views and exercises stale replies,
backend-held leases, conflict and data/summary visibility. These are serialized
teaching owners, not a durable Orbital integration. They intentionally distinguish
private-copy cancellation from dirty in-place cancellation.

TuplePack's [observations](../../../ikea/include/ikea/tuplepack/author/maintenance.h)
bracket child writes and can depend on untouched fields. The
[Bec256 adapter](../../../ikea/examples/bec/integration.cpp) admits body and
directory updates before either writes. Persistent metadata and summaries need
their own coverage and coordinated visibility.

Ikea/owner code defines unit-specific damage, encoding, capture, reconstruction
and meaningful content checks. [Engine](../../../engine/README.md) binds these
to logical identity, schema, locks, descriptors, indexes and summary laws.
Orbital preserves qualified recovery material, outcome and source lifetimes,
and transports it; a generic byte codec cannot supply those unit semantics.
[Loom](../../../loom/README.md) budgets leases and retained work and schedules
bulk windows. An Ikea kernel call need not be a task or a publication unit.

## Summaries: publish validity with data

The [aggregate-maintenance closeout](../aggregate-maintenance/CONCLUSIONS.md)
retains two mechanisms worth revisiting, without selecting either: sorted
location-addressed count/sum corrections, and a bounded append buffer with
optional dirty-prefix filtering. Its [semantic analysis](../aggregate-maintenance/design.md)
separates additive count/sum from extrema. Removal of the last min/max witness
has no scalar inverse. A removed extremum can leave a loose pruning bound;
an unaccounted addition can make it unsafe. Exact extrema need surviving
evidence, repair or descent. Count/sum also need defined overflow, NULL/presence
and reduction semantics; Ikea's modulo-u64 recipe does not establish SQL laws.

The useful invariant crosses parts: a reader gets matching data, base-summary
cut and visible corrections, or a valid bypass. Compaction must change the base
and correction coverage together, preventing gaps and double counting. Retained
readers constrain cancellation of opposing corrections. The old probes expose
current state only at quiescent batch boundaries; they do not implement these
MVCC, concurrent-publication or recovery obligations. Persisting summary bytes,
correction material or bypass metadata contributes ordinary physical damage,
but their *logical validity* is a separate owner obligation.

The performance signal is mixed. Sorted location runs removed 71% of interior
adjustments under uniform writes yet cost about 24 times resident-array eager
maintenance. The lighter [dirty-buffer probe](../aggregate-maintenance/dirty-buffer/FINDINGS.md)
paid every ancestor update during replay: with four writers, 64 hot locations
and 4,096 updates per batch, its full cycle was about 29 ns/update with an
8 KiB filter and 22 without, versus 71 for private-shallow eager. Conditional
word-local marking, 64-record reservations and batched replay helped; filter
marking/reset and repeated dirty scans could erase the gain. Dispersed workloads
favoured eager. These are ARM-VM summary-only timings, not physical write
amplification, measured cold-line traffic or a Zen5 selection.

A [dirty-prefix negative](../aggregate-maintenance/shared-dirty-buffer.md)
proves clean only with compatible publication and generation state. A positive
is uncertainty, not a correction. Buffer exhaustion and dirty-read pressure are
policy inputs; Bloom reset cannot race with records its bits protect. The probe
uses barriers and batches without reservation holes, so relaxed marking atomics
do not constitute a production publish protocol.

Keep three facts distinct: projected lock permission, issued-byte coverage and
summary validity. Coverage does not grant authority. A postimage containing
preserved neighbours cannot be replayed over newer independent neighbour writes
without compatible ownership or a valid merge. This matters when connecting
Ikea's local stores and contributions to Orbital's one outcome over parts.

## Keep the performance patterns, recalibrate the boundaries

**Use unit knowledge instead of rediscovering change.** Ikea can exploit known
store coverage or emit a complete replacement encoding when that is cheaper.
Capture while the affected unit is stable and likely resident. Repeated staging
can spread port work over the epoch, but a pointer plus a range is not an
immutable change if a writer can alter it before consumption. Calico's coarse
page-final rule avoided this race while stopping independent work sharing that
page. Finer capture can remove that restriction; its copy and ownership costs
still need measurement.

**Reuse unchanged material and established metadata.** A new logical version
need not recreate every unchanged byte. The same applies to hash baselines.
Calico demonstrated the ingredients separately; it did not compose general
history lookup, serving mappings, physical reuse and incremental persistence.
The [publication comparands](publication/README.md) price these costs with one
byte-cut oracle. Shared pages, blocks and afterimages are experiments, not a
universal Orbital format or evidence that every unit needs the same material index.

**Batch at the boundary that incurs cost.** Merge duplicate/overlapping damage,
carry resolved locations where appropriate, and submit a useful batch. Calico's
`stage_many` removes repeated ledger locking for one prepared write, but later
page rounding and routing still multiply data movement. A single coarse batch
should not force unrelated writers or port requests to stop together.

**Charge distinct kinds of amplification.** A hardware page can be the minimum
unit of COW or protection without being the minimum durable patch. Logical
change can also differ from physical change: inserting eight bytes into a
packed representation can move almost an entire page. Encoding, hash blocks,
wire records, residency windows and compaction each have a separate useful
grain. Measure all of them alongside elapsed time.

**Let the unit choose its change encoding.** Exact afterimages are one option,
not the definition of an effect. Whole-unit replacement, deterministic replay,
or format-aware changes may fit another unit better. XOR, alignment and
dictionary compression can reduce port bytes but introduce source dependencies
and CPU work. Different replica layouts need compatible replay or conversion.
Calico's indexed-ingest experiment usefully replicated canonical events and
built indexes locally instead of shipping every derived index mutation.

**Bound reconstruction debt.** Small publications can accumulate costly reads
and recovery. Retention, a materialised base and compaction must preserve the
exact source versions needed by live readers and patches. Completion or
cancellation of a request must not allow reuse while a backend still accesses
its bytes. Cache eviction is a separate decision from discarding recoverable
material. Temperature hints describe reuse value, not permission to retire a
version.

## Evidence and omissions

The current [Calico YCSB comparison](../../../../calico/workbench/benchmarks/ycsb/README.md)
is a useful point-operation reference with a restricted configuration. Its
[`transactional_preset`](../../../../calico/foyer/include/foyer/descriptor.h)
disables all six summary policies; fast update recipes in
[`session_impl.h`](../../../../calico/foyer/src/session_impl.h) explicitly decline
maintained summaries. General summary-enabled updates in
[`ingest.h`](../../../../calico/engine/include/engine/ingest.h) do snapshot ancestor
rows and commit dirty summary replacements with the leaf. The winning preset
therefore does not price that maintenance, rather than proving it is free.
Secondary-index mutations also have explicit unsupported cases. The campaign
uses one client and memory-backed storage with durability barriers disabled;
delete, point-miss, concurrent, cold and larger-than-memory behavior are outside
its result.

Useful preparation is real: bounded cached bindings retain cursors/layouts,
qualified by commit watermarks, generations and mutation receipts. Stable-layout
recipes avoid repeated planning; representation changes fall back. Carry that
pattern into SixDB's regular composition, with the same maintained state and
safe fallback. Price growth, fallback frequency and deferred work through steady
state; a guarded warm-path result does not establish whole-profile performance.

The [retired mechanism report](../../../../calico/xmem/archive/pre-standard-matrix-2026-08-09/REPORT.md)
reports 5–58× cheaper sparse certification and 3–8% dense penalties; an
eight-byte shift made unaligned XOR occupy 3,534 bytes of a 4 KiB page, while
the experimental aligner emitted 12 bytes. These are mechanism observations,
not whole-publication results. The original `xmem/spike/certcost/probe.c`, at
`d84f05199d1892628275c4e1eece58bbcc295119`, hashes damage plus range metadata:
it does not maintain the canonical full-state root used by later `PageRollup`.
The delta probe uses synthetic pairs, repeated resident buffers and an
equal-size, single-gap aligner. Its direction is useful; its timings do not
price history, port completion, compaction or recovery.

Calico's [physical reopen ledger](../../../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md)
is unusually valuable because it identifies the unfinished composition.
Witness logs survive crashes, but full physical region reopen, rooted allocator
state, transaction namespace/incarnation binding and unknown-tail discovery
were not connected to the facade. Full cold-service recovery therefore cannot
be inferred from the commit crash tests. The views spike should exercise
publication and reopening as one operation history, including interrupted
publication and retained readers, before claiming these ingredients compose.
