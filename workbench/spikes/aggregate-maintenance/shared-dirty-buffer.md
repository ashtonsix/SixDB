# One collection buffer with a dirty-prefix filter

Ashton's candidate, 2026-09-07: append aggregate-changing updates to one
collection-wide buffer; mark the location and its prefixes at 8-bit strides
in a small shared Bloom filter. Reads test dirtiness cheaply. Buffer exhaustion
and dirty-read pressure trigger flushing. This analysis assumes “8b” means
eight bits and that the writers share memory; the ownership scope within a
distributed collection remains open. The [first implementation and measurements](dirty-buffer/FINDINGS.md)
now test several variations; the analysis below records the starting reasoning.

The attraction is **idempotent dirtiness instead of repeated arithmetic on
ancestor summaries**. One append preserves the change; small metadata protects
clean reads while cold summary lines wait for batch maintenance. This responds
to the [first probe's construction-cost result](FINDINGS.md): sorting and
indexing every incoming batch may already be too expensive.

## Update and read contract

The buffer needs a logical location and sufficient aggregate contributions,
including field identity and visibility information where required. Existing
mutation-log information might supply these. Beforeimages still cost work.
“Changes an aggregate” must not require reading every ancestor first;
conservatively mark possibly affected summaries.

Hash the keyspace, prefix length, and canonical prefix. A field-agnostic mark
is conservative, but can make a query dirty because an unrelated field changed.
Adding field identity improves selectivity at the cost of more marks and space.

A negative test proves clean only with compatible publication/generation state.
A positive test supplies neither a correction nor an exact dirty verdict.
The first dirty-read comparand should scan a bounded buffer of committed
changes. Scan once for several requested regions where possible. Root
count/sum needs no location filtering. Caching a correction for a stable
prefix/generation, descending, or requesting a flush are other options.

Direct tests only work for marked prefix lengths. A 13-bit summary could
conservatively test its enclosing 8-bit prefix, or test a cover of finer marked
prefixes. Hashing an unmarked 13-bit identity permits a false clean result.
Arbitrary ranges also need a cover or conservative enclosing region.

The [aggregate semantics](design.md#separate-the-aggregate-semantics) still apply:
removing the last min/max witness may require live-base inspection, and pending
additions must extend pruning bounds. This filter is internal maintenance
metadata, separate from the [secondary-summary question](../../notebook/secondary-summaries.md).

## Where contention moves

Eager count/sum repeatedly modifies shallow shared summaries. Those lines may
be resident yet expensive to acquire from other cores. Dirty bits only change
from zero to one within an epoch: **load the atomic word, then OR only if the
required mask is missing**. Concurrent first setters still need atomic OR;
ordinary load/OR/store can lose bits. Unconditional atomic OR forfeits much
of the advantage. A positive at one prefix does not justify skipping marks
for other prefixes: Bloom collisions do not encode their relationship.

The kernel's [false-sharing discussion](https://www.kernel.org/doc/html/latest/kernel-hacking/false-sharing.html)
illustrates read-before-write bit setting and line separation. Here, already-set
shallow marks could become mostly reads. That is an inference, not a measured
SixDB result. Unrelated deep-prefix writes can still invalidate their lines.

The append allocator is a likely replacement hot spot: one atomic tail
increment per record remains a shared writable line. Compare per-record
reservation with small cache-aligned producer spans within the same logical
buffer. Spans amortize allocation and separate payload writers, but introduce
holes: reserved space is not committed data, and a stalled producer may block
a contiguous publication frontier. Charge publication as well as reservation.

Dirty-read counters, flush ownership, and generation state can also contend.
Collecting read pressure locally and combining it occasionally avoids another
global increment on every positive lookup. Separate writer-heavy allocation
state from reader-heavy metadata.

## Cache-line budget

Let `H` be the marked prefix count and `k` the filter's bit probes per identity.
A 64-bit location marked at lengths 8 through 64 gives `H = 8`; root dirtiness
can follow pending-buffer state.

| Mechanism | Foreground address work, excluding flush |
| --- | --- |
| Eager summaries | About `H` summary locations: shallow shared lines and potentially cold deep lines; layout and field count determine actual lines |
| Ordinary Bloom filter | Up to `kH` bit locations, potentially separate lines, plus append and publication |
| Cache-line-blocked filter | One filter line per prefix, potentially several atomic word operations within it |
| Word-blocked filter | One word mask per prefix and one OR when missing, at a space/error trade-off |

These are address-work budgets, not measured misses. A resident filter may
avoid DRAM fetches while suffering cache-to-cache transfers. Prefix hashing
and probes also cost CPU even when bits are already set. A smaller filter
trades footprint against collisions, false positives, and unrelated writers
sharing lines.

[Parquet's split-block filter](https://parquet.apache.org/docs/file-format/bloomfilter/)
is a concrete locality precedent: a probe selects a 256-bit block with eight
32-bit words. It does not supply a concurrent update protocol; block locality
does not imply one atomic operation. [Putze, Sanders, and Singler](https://publikationen.bibliothek.kit.edu/1000025357)
is a further reading lead on cache-, hash-, and space-efficient filters.

A small variant preserves one buffer: infer root dirtiness from pending work,
put the 256 first-byte prefix marks in an exact bitmap, and use the filter
deeper down. The bitmap needs 32 payload bytes, with suitable line isolation.
It avoids hashing that level and deep-filter collisions on its line. First
setters and resets can still contend. Compare against the literal design.

## True dirtiness and saturation

For `U` independently uniform 64-bit locations since a full flush, a given
`8d`-bit prefix is touched with probability `q_d = 1-(1-256^(-d))^U`.
Expected distinct marked identities are `D = sum(d=1..8, 256^d * q_d)`.
Repeated prefix insertions do not add identities. These are occupancy
calculations, not benchmark results.

| Updates | First-byte prefixes truly dirty | Two-byte prefixes truly dirty | Expected distinct identities |
| ---: | ---: | ---: | ---: |
| 64 | 22.16% | 0.10% | 505 |
| 256 | 63.28% | 0.39% | 1,953 |
| 1,024 | 98.18% | 1.55% | 7,411 |
| 4,096 | approximately 100% | 6.06% | 28,802 |

A perfect filter therefore stops protecting most broad regions quickly under
dispersed writes, while remaining useful deeper down. Skew concentrates true
dirtiness and changes both contention and identity count. Root stays
conservatively dirty while aggregate-affecting work is pending, even if some
contributions would cancel.

For an ordinary Bloom filter the independent-hash approximation is
`f = (1-exp(-kD/m))^k`, for `m` bits. With four probes and 8 KiB, the estimated
false-positive rate among absent identities rises from 1.75% at 1,024 updates
to 46.9% at 4,096. With 32 KiB, the latter is about 1.6%. Substituting expected
`D` is a sizing approximation; blocked variants need their own error measures.
Size for distinct prefixes, not records. Observed dirty-read probability is
approximately `q+(1-q)f`; improving `f` cannot fix high true dirtiness `q`.

## Read pressure, flush, and publication

A positive lookup should not automatically flush the collection. Broad reads
could otherwise force near-eager maintenance plus append/filter work; false
positives would also gain the power to force a flush. Accumulate actual
correction cost, coalesce concurrent flush requests, and compare with estimated
flush cost. Capacity remains a hard limit. Small-buffer scans are a useful
starting comparand before adding an adaptive policy.

Flush still pays for ancestor maintenance. Replaying each record to every
ancestor may improve locality without reducing logical work. Coalescing must
use a cheaper construction path than the first prototype to repay its cost
on resident arrays. Measure the full cycle, including reset and final drain.

Keep bits monotone within a generation. An ordinary Bloom filter cannot safely
clear a flushed prefix because other pending prefixes may share its bits.
Leaving marks set is correct but loses selectivity. Whole-generation retirement
avoids this deletion problem.

A first correctness model can pause publishers, drain/update the base, clear,
and resume. Overlapping writes with flush requires active/frozen generations
or an equivalent synchronized protocol. They may share an allocation, but
filter lifetime and visibility still need separation. Never reset marks while
records they protect remain pending.

The record and all required marks must be available before a mutation becomes
visible to a reader that might trust the base. Readers need a compatible base
cut, committed log range, and filter generation. False dirtiness during
publication is acceptable; false cleanliness is not. Memory ordering, retained
snapshots, recovery, and stalled writers still need a concrete protocol.

## Focused experiment

Compare eager atomic count/sum, append-only as a component cost, and append
plus prefix marking. Isolate unconditional versus read-before-OR, ordinary
versus blocked masks, and record versus span reservation. Append-only does
not offer equivalent query semantics; it exposes a component's cost.
Keep visibility guarantees equal: atomic updates to count and sum separately
do not publish an atomic summary pair. A first contention probe can isolate
writers, then check complete batches against an oracle; concurrent readers
require the publication protocol to be included on both sides.

Vary writers across physical cores/cache domains, summary-array residency,
uniform/hot/repeated locations, and fresh versus populated filters. Count ORs,
reservations, and logical lines separately from measured cache misses and
cache-to-cache transfers. Record true-dirty and false-positive rates alongside
speed: a saturated filter can skip most ORs while forcing every read dirty.
Include broad and narrow reads, full drained-cycle
cost, and tail latency so read-triggered flush churn is visible. Attribute
append, marking, scan, flush, and reset costs without timing instrumentation
in the main hot path. Where supported, `perf c2c` can identify contested lines;
check event support on the actual machine. The earlier single-thread ARM-VM
timings cannot settle these coherence questions.
