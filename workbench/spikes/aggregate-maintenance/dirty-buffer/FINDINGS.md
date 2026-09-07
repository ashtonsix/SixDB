# First dirty-buffer findings

These findings cover the second probe. The [spike closeout](../CONCLUSIONS.md)
combines them with the earlier partitioned-delta comparison.

2026-09-07. **There is a favorable region: multiple writers repeatedly updating
a small set of locations.** An 8 KiB word-blocked filter with read-before-OR
and 64-record reservations beats eager maintenance there, even after paying
for reads, full replay, and reset. The experiment also finds that a bounded
buffer without any filter can be cheaper in these workloads.

The results do not favor replacing eager everywhere. Direct non-atomic eager
updates win the one-writer cases in the broader screen. With dispersed updates,
eager with private shallow summaries beats every buffered version tested.

## Evidence

The [32 KiB screen](evidence/local-arm-20260907/filter32/summary.md) contains
85 comparisons with five repetitions each. The final
[8 KiB comparison and no-filter controls](evidence/local-arm-20260907/filter8-and-controls/summary.md)
contains 30 comparisons with five repetitions each. Minimum measurement times
are 0.04 seconds per repetition. [Raw evidence and provenance](evidence/local-arm-20260907/README.md)
link to verified S3 bundles containing the source snapshots; compact samples
and counters remain in Git. Correctness checks cover every changed prefix,
every query against an afterimage oracle, and all final summary cells; the
small scenario also passes ASan/UBSan with one and four writers.

These are Linux/OrbStack ARM results, Clang 21.1.8 with ARM LSE atomics enabled.
Writers are pinned to guest CPUs; physical placement is not established.
Four-writer dispatch/barrier time is included. Cache-miss events were unsupported,
so hardware traffic is not directly measured. See the [measurement contract](README.md).
Some repetitions are noisy; the tables retain spreads and the conclusions
below avoid relying on small differences.

## Which version wins?

Median full-cycle ns/update, four writers. In this table both buffered versions
flush on the first dirty read; without a filter, all pending data is treated
as dirty. Every epoch is fully drained. The stronger eager baseline gives each
writer private root and first-byte partials and uses shared atomics deeper down.

| Workload | Eager, private shallow summaries | 8 KiB Bloom + spans | Buffer + spans, no filter |
| --- | ---: | ---: | ---: |
| 64 hot locations; 1,024 updates, 4 queries/batch | 125.0 | 85.3 | 74.0 |
| 64 hot locations; 4,096 updates, 4 queries/batch | 70.9 | 29.3 | 22.0 |
| 64 hot locations; 1,024 updates, 64 root queries/batch | 123.5 | 83.5 | 72.4 |
| Dispersed locations; 1,024 updates, 4 queries/batch | 124.1 | 236.0 | 148.6 |
| Dispersed locations; 1,024 updates, 64 point queries/batch | 123.5 | 240.0 | 151.0 |

[Source CSV](evidence/local-arm-20260907/filter8-and-controls/summary.csv).
The larger hot batch is about **2.4× faster with the Bloom variant** and
**3.2× faster without the filter** than the stronger eager baseline. Plain
shared-atomic eager was 98.5 ns/update in that row, with a noisy 20% repetition
spread; the private-shallow baseline is the more useful comparison.

## What mattered

**Idempotence needs locality.** In the hot 1,024-update case, one accounting
run records 65,536 ORs with unconditional word marking versus 4,111 with
conditional marking. On dispersed writes, conditional marking still needs
58,443 ORs. Dirtiness only becomes cheap after marks are already present.
These OR counts include races between first setters and are not hardware
cache-transfer counts. [Accounting](evidence/local-arm-20260907/filter8-and-controls/accounting.csv)

**Reservation granularity matters.** At 8,192 total updates, spans reduce
shared reservations from 8,192 to 128. In the 32 KiB hot screen, conditional
per-record reservation took about 128 ns/update, while spans took about
100 ns. Word-local masks substantially outperform four scattered probes.
The exact first-byte bitmap did not consistently improve the span version.
[Screen](evidence/local-arm-20260907/filter32/summary.md)

**Reset is real work.** Conditional marks save foreground ORs but do not make
retirement free. In the hot 1,024-update span case, the 32 KiB filter reset
phase cost about 20 ns/update; the 8 KiB version cost about 5 ns. Replay itself
was about 6 ns. This is consistent with a shared-line cost, but the VM could
not supply the counters needed to establish its hardware cause.

**Dirty reads can erase the win.** With 64 root queries per hot batch, scanning
for each positive read costs about 46 ns/update and brings the Bloom span
cycle to 127 ns, versus eager's 124 ns. Flushing on the first positive read
reduces that cycle to 84 ns. This tests a threshold of one positive read at a
quiescent batch boundary, not an adaptive flush policy with live writers.

**The filter must earn its cost.** No-filter append plus scan is about
76 ns/update in the small hot batch; append plus immediate read-triggered
flush is about 74 ns. At the larger hot batch they are about 25 and 22 ns.
Both are below the Bloom variants. With dispersed writes and four queries,
no-filter scanning is about 130 ns, close to but still above eager's 124 ns.
With 64 point queries it rises to 210 ns. The filter reduces correction reads,
but its marking cost is too high to repay in these dispersed scenarios.

## Interpretation and limits

This supports a cheap shared append path with amortized reservations and
batch maintenance for contended hot locations. Bloom marking is an optional
cost whose benefit depends on how much clean-read work it saves. The simpler
conservative buffer deserves to remain a comparand.

The timings charge every ancestor update during replay: there is no coalescing
or deferred debt. They also use precomputed locations/beforeimages, one exact
count/sum field, quiescent reads, and no primary-row work or durability. Cells
are 128 bytes apart; summary allocation/residency and the barrier mechanism
are explicit experimental choices. Different locality cases change mutation
mix too. These are neither physical write-amplification measurements nor a
production concurrency protocol, and do not establish a Zen5 winner.

The [original candidate](../shared-dirty-buffer.md) remains useful, with a
sharper question: when does the clean-read benefit repay the dirty index's
marking and reset costs? Retained snapshots and overlapping readers/writers
could change both the maintenance budget and the value of that index.
