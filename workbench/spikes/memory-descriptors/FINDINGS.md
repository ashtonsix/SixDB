# Memory-descriptor findings

The finite geometry is workable, but a trampoline does not supply the lifetime
or logical-version protocol around it. The strongest early signals favor
examining grid selection before adding more relocation machinery, and separating
view-local binding state from live allocator authority. These are spike findings,
not a selected allocator or a shared module contract.

## Geometry and page ownership

For 4 KiB pages, sixteen homes each of `{16,32,48,64,96}` exactly fill one page.
The prototype keeps these small A homes fixed during compaction. Larger A
classes, and all finite B classes, can occupy their own page-aligned ranges.
The reservation apertures are sparse virtual address space; only overlapping
active pages need backing. The shared small page contains eighty possible homes,
but the grid still has only sixteen current owners. Exact packing of the address
layout does not mean all those homes can hold current payloads together.
In this arrangement, sixteen 16-byte or sixteen 96-byte current reservations
both require that one payload page. Smaller classes therefore do not save
backing by themselves. The [allocation exploration](ALLOCATION.md#small-slot-packing)
adds interleaved small homes across grids, keeping the existing placement and
larger-class comparisons as separate evidence.

| Finite grid | Packed class ranges | Private large-class ranges |
| --- | ---: | ---: |
| A | 40 pages | 41 pages |
| B | 69 pages | 71 pages |

These spans include every possible finite home, rather than the sixteen current
owners. Native independent address checks cover 9,600 combinations across both
palettes, both arrangements and both directory layouts, including control-page
boundaries. Virtual homes are deliberately unassigned.

The sixteen-owner bound guarantees a vacant destination in a different class
when only current homes occupy the grid. It does not guarantee a spare home in
the current class. Retained old homes can exhaust another class too: one modeled
pressure case holds sixteen current and sixteen old homes across two classes,
blocking further replacement until readers complete. A policy must account for
this pressure; the byte coordinate alone cannot solve it.

## Placement and compaction

The [placement model](placement.py) compares 192 deterministic combinations:
two palettes, two arrangements, four allocation policies, compaction on/off and
six histories. It records backed pages, copies, slack and retained homes.
“Page-steps” sums backed pages at event boundaries; it is a logical occupancy
measure, without a time or throughput interpretation.

New allocations can exploit existing pages without changing descriptor address
rules. A sparse-history witness opens eight grids with append-first placement
and six with resident-first placement. Private large ranges can also make useful
local compaction straightforward: two scattered 3,072-byte homes occupy four
pages, then two after copying 6,144 bytes within their class/grid. Sixteen such
homes fill twelve pages.

In the 900-event A churn trace, resident-first without compaction reduces payload
page-steps from 17,764 with packed ranges to 13,791 with private large ranges
(22.4%), while peak backing falls from 41 to thirty pages. This comparison does
not include TLB effects, lookup or allocator-search costs. Small-home compaction
is disabled in both arrangements, so the model does not establish its value.

The longer histories qualify that example. For A churn with private ranges and
compaction enabled, resident-first uses 13,791 payload page-steps and finishes
with seven pages. Accurate lifetime hints use 12,000 page-steps but finish with
ten; deliberately wrong hints use 22,874 and finish with thirteen. A good
end-state count and a good lifetime-weighted count can prefer different choices.

In the A sparse-then-new trace, resident-first plus compaction changes private
ranges from 7,910 to 7,617 page-steps at 18,432 copied bytes. With packed ranges,
the same greedy rule changes 8,215 to 8,468 page-steps at 239,616 copied bytes,
and the final payload pages increase from 32 to 33. Immediate page release can
therefore make later allocation worse. Cross-grid compaction is absent; the
results give reasons to improve placement and movement criteria first.

These histories are synthetic, not a production workload distribution. The model
backs whole reservation capacities, uses greedy compaction and conservatively
retains descriptor identities with raw grants. Its dense directory has 256 grids
per page; native metadata experiments use 64. Full counters and trace hashes are
in [placement-results.json](placement-results.json).

## Acquisition, uniqueness and generations

Three obligations need separate treatment: preserving a logical state,
preserving a usable descriptor identity, and preserving bytes for an active
operation. A raw read can retain old bytes after the descriptor has rebound.
Once all descriptor aliases have ended, its identity can be reused while that
raw read remains active. Conversely, one surviving descriptor does not imply
that no CPU or I/O operation is using its payload.

The native probe acquires a physical grant under the owner latch and returns an
ordinary address, readable extent and physical generation. Completion releases
the grant; cancellation alone does not. Another option protects a whole grid
before resolving. Both work in the bounded model, but coarse protection retains
unrelated retired homes: a native witness keeps one extra page until the scope
ends. Neither mechanism selects the production synchronization or scheduling API.

The [lifecycle model](lifecycle.py) explores seventeen bounded cases: nine safe
variants and eight deliberately unsafe variants, totaling 1,606 states and 3,150
transitions. Every reachable safe state has a completion path. Unsafe witnesses
include loading before retention, releasing on cancellation, checking zero
readers without closing acquisition, inferring exclusivity from a sole head,
coordinate reuse and competing destination claims. This is sequentially
consistent finite exploration, not a weak-memory or unbounded-progress proof.

Native checks also preserve an admitted 64-byte read window around a 47-byte
body reserved in a 64-byte home, while appending to fifty bytes. The append
relocates within the same class
instead of modifying padding still visible to the old reader. A read's access
footprint can exceed the logical initialized prefix; the primitive needs that
information to decide whether in-place append is safe.
The largest admitted extent is conservatively retained until the last grant to
that home completes.

A physical generation distinguishes bindings for an already acquired grant. It
does not make a stale descriptor safe to reopen, or determine which logical
version a query wants. Raw-pointer copies within an operation need not each be
counted, provided the operation's grant covers their full lifetime and footprint.

## COW and composition

The Linux `memfd` probe gives two private mappings distinct bindings and payloads
after private writes, preserving an unrelated neighboring payload. It also
demonstrates that updating a trampoline privately copies the entire control
page: live authority placed beside that trampoline becomes a stale private copy.
Separating that authority from view-local state preserves one current source.
`MAP_PRIVATE` alone does not establish an immutable snapshot against later writes
to the shared source; that requires a backing/version discipline.

Metadata zones are consequently more than a space budget. View bindings, free
homes, active-operation retention, scheduling hints and durable bookkeeping
have different update and retention needs. Engine supplies logical version and
visibility choices; Loom prepares and retains resources; Orbital supplies backing,
mapping and physical retirement; Ikea consumes admitted native memory. Semantic
ownership can be split while the physical layout groups compatible accesses.

Publishing one initialized byte coordinate is one binding change. It does not
atomically publish a whole representation, several allocations, a transaction
or durable state. A snapshot cannot resolve both old and new logical versions
through one shared mutable byte without another view/version mechanism. A
no-reader gate permits physical mutation only when the logical owner has also
authorized it and any required old state remains recoverable.

## Native access measurements

Mostly core-local access remains a heuristic objective, with a 2.50 ns median
reciprocal-throughput target for the hot protected path. Sixty-four dense
trampolines span four grids and one cache line; the [extension](EXTENSION.md)
also compares 16, 256 and 1,024, including 16 KiB mapping granules. Allocation
and scheduling should consider dominant access, co-access and ownership alongside
page occupancy and lifetime grouping. Nearby metadata should preserve that
locality even when several modules own its meaning. The placement model does
not yet evaluate this group-level objective. Native baseline reads already use
one core with immutable trampolines; they do not measure the benefit of an
ownership heuristic under rebinding.

Here “median” means the median batch cost across repetitions, not the median
latency of individually timed accesses. At 256 allocations, independent plane
access costs 1.71 ns/allocation; the serial chain costs 7.66 ns versus 3.59 ns
direct. At 4,096, the serial costs are 9.83 ns versus 5.96 ns. These include the
head-array lookup, resolution and payload loads. Their roughly 4 ns difference
is an observed chain penalty, not an isolated resolver-latency measurement.

The [retained Zen 5 run](evidence/zen5/artifact.json) completed on a two-core
`c8a.large`, AMD EPYC 9R45, using pinned Clang 21.1.8, `x86-64-v4` and `zen5`
tuning. CPU 0 reads; CPU 1 optionally saturates aligned telemetry stores beside
each grid's read-only hint. The run takes five sequential repetitions in each
of two orders, over 256, 4,096 and 65,536 allocations. All 960 timing samples
pass independently computed payload checksums; the corrected native probes pass
on this host and locally under ASan/UBSan.

Both layouts put 64 grids in a control page. **Planes** use a dense 1 KiB
trampoline zone followed by 3 KiB of side metadata. **Records** use one 64-byte
record per grid: sixteen trampoline bytes and 48 side bytes. Direct and trampoline
paths share payload addresses within each arena; the two layouts have matching
placement and page budgets, without requiring identical virtual addresses.
The writer changes telemetry, leaving hints, trampolines
and payloads intact; shared cache lines still transfer between cores.

At 4,096 allocations, these are median nanoseconds per allocation access across
ten samples. Protection is acquired before the timed loops. Independent reads
and scans report amortized throughput cost; dependent chase exposes serial access.

| Access | Layout | Idle metadata | Busy metadata writer |
| --- | --- | ---: | ---: |
| Random 8-byte read, direct address | Planes | 0.59 | 0.59 |
| Random 8-byte read, trampoline | Planes | 1.99 | 1.99 |
| Random 8-byte read, trampoline | Records | 2.10 | 8.86 |
| Dependent 16-byte chase, trampoline | Planes | 9.83 | 9.84 |
| Dependent 16-byte chase, trampoline | Records | 10.13 | 28.31 |
| Point read plus hint | Planes | 3.18 | 10.48 |
| Point read plus hint | Records | 3.13 | 13.49 |

The idle direct chase is 5.96 ns. A 64-byte scan is about 1.77–1.84 ns through
either path at this size, so indirection cost depends strongly on the consumer.
Records slightly improve the idle joint read here, but co-locating frequently
written metadata has a much larger adverse effect. Separate planes isolate
trampoline-only access; reading their neighboring hint still incurs contention.
At 65,536 allocations, dependent trampoline access under the writer is 28.37 ns
for planes and 47.10 ns for records. This makes update frequency and writer
placement important inputs to physical zoning, alongside point-read locality.
The joint workload visits a quarter-sized subset, so subtracting the full point
workload's cost would not isolate the cost of reading a hint.

The writer is intentionally extreme, around 0.8–3.3 billion stores/second in the
4,096-allocation cases; its achieved rate varies with contention. This establishes
a stress mechanism, not an expected application rate or universal layout winner.
Timings are repeated warm-loop costs, without per-operation tail percentiles or
assigned cache-residence tiers. Order medians and ranges remain in
[summary.csv](evidence/zen5/summary.csv), alongside every original sample.

The 65,536-allocation case maps 16.25 MiB of control/payload pages and reserves
656.25 MiB of virtual address space for its possible homes. Its separate owner
registry consumes about 40.75 MiB, plus descriptor and ordering arrays. These
costs are exposed in the CSV rather than hidden in a nominal 4 MiB payload count.
Footprints are mapped capacities and `sizeof`-based bookkeeping estimates,
without allocator overhead or measured RSS.

The native safety registry is intentionally verbose and charged separately.
Per-operation borrow timings include its mutex and bookkeeping; they do not
establish the cost of every possible retention protocol. Writer-mode borrow
comparisons also change process threading state, so they do not isolate cache-line
contention from any resulting change in mutex behavior. The tests cover finite
reservations, actual mappings and delayed CPU readers. They do not implement
transaction commit, crash recovery, asynchronous device I/O or the virtual class.

The retained selection keeps both layouts, all workloads, repetitions, modes,
footprint counters and host/check receipts. Exact captured sources, full logs
and disassembly remain recoverable through the artifact reference.
Regenerate the summary with `summarize.py` or the
[artifact report helper](../../tools/artifacts.md#run-a-report-recipe).
